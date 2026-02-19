#include "BirdLoader.h"
#include "libraries/theory/note_parser.h"
#include "libraries/sequencing/utils/time_constants.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

// --- Tick-to-beat conversion ---
// TICKS_PER_WHOLE_NOTE = 384, one whole note = 4 beats, so 1 beat = 96 ticks
static constexpr double TICKS_PER_BEAT = 96.0;

static double ticksToBeats(int ticks) {
    return static_cast<double>(ticks) / TICKS_PER_BEAT;
}

// --- String helpers ---

static std::string ltrim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    return (start == std::string::npos) ? "" : s.substr(start);
}

std::vector<std::string> BirdLoader::splitLine(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// --- Pattern parsing ---
// Converts pattern tokens (w, q, x, xx, _, _q, etc.) to tick durations
// Negative values = rests, positive = note-on durations

std::vector<int> BirdLoader::parsePattern(const std::vector<std::string>& tokens, int& lastDur) {
    std::vector<int> pattern;
    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "_") {
            // Rest of same length as last duration
            pattern.push_back(-std::abs(lastDur));
        } else {
            lastDur = dur_from_string(tokens[i]);
            pattern.push_back(lastDur);
        }
    }
    return pattern;
}

// --- Velocity parsing ---
// Handles absolute values, repeats (-), and relative offsets (+N/-N)

std::vector<int> BirdLoader::parseVelocities(const std::vector<std::string>& tokens, int& lastVel) {
    std::vector<int> velocities;
    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "-") {
            velocities.push_back(lastVel);
        } else if ((tokens[i][0] == '+' || tokens[i][0] == '-') && tokens[i].size() > 1) {
            int newVel = lastVel + std::stoi(tokens[i]);
            newVel = std::clamp(newVel, 0, 127);
            lastVel = newVel;
            velocities.push_back(newVel);
        } else {
            lastVel = std::stoi(tokens[i]);
            velocities.push_back(lastVel);
        }
    }
    return velocities;
}

// --- Note parsing ---
// Handles MIDI numbers, note names (C4), chord names (@Cm7),
// repeats (-), and relative offsets (+N/-N)

std::vector<std::vector<int>> BirdLoader::parseNotes(const std::vector<std::string>& tokens, int& lastNote) {
    std::vector<std::vector<int>> noteGroups;
    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "-") {
            noteGroups.push_back({lastNote});
        } else if ((tokens[i][0] == '+' || (tokens[i][0] == '-' && tokens[i].size() > 1))) {
            int newNote = lastNote + std::stoi(tokens[i]);
            lastNote = newNote;
            noteGroups.push_back({newNote});
        } else if (is_chord_name(tokens[i])) {
            auto chordNotes = midi_from_chord_name(tokens[i]);
            if (!chordNotes.empty()) {
                lastNote = chordNotes[0];
                noteGroups.push_back(chordNotes);
            }
        } else if (is_note_name(tokens[i])) {
            int midi = midi_from_note_name(tokens[i]);
            if (midi >= 0) {
                lastNote = midi;
                noteGroups.push_back({midi});
            }
        } else {
            // MIDI number
            try {
                lastNote = std::stoi(tokens[i]);
                noteGroups.push_back({lastNote});
            } catch (...) {
                // skip invalid tokens
            }
        }
    }
    return noteGroups;
}

// --- Note resolution ---
// Expands pattern + note groups + velocities into concrete BirdNote events
// This mirrors Sequencer::gen_notes_sequence logic

std::vector<BirdNote> BirdLoader::resolveNotes(
    const std::vector<int>& pattern,
    const std::vector<std::vector<int>>& noteGroups,
    const std::vector<int>& velocities,
    int sequenceLength)
{
    std::vector<BirdNote> result;
    if (pattern.empty() || noteGroups.empty() || velocities.empty())
        return result;

    int ticks = 0;
    size_t patIdx = 0;
    size_t noteIdx = 0;
    size_t velIdx = 0;

    while (ticks < sequenceLength) {
        int dur = pattern[patIdx];

        if (dur > 0) {
            // Note-on: emit all notes in the current group
            for (int pitch : noteGroups[noteIdx]) {
                BirdNote n;
                n.pitch = pitch;
                n.beatPos = ticksToBeats(ticks);
                n.duration = ticksToBeats(static_cast<int>(dur * 0.9)); // 90% gate
                n.velocity = velocities[velIdx];
                result.push_back(n);
            }
            noteIdx = (noteIdx + 1) % noteGroups.size();
            velIdx = (velIdx + 1) % velocities.size();
        }

        ticks += std::abs(dur);
        patIdx = (patIdx + 1) % pattern.size();
    }

    return result;
}

// --- Main parser ---

BirdParseResult BirdLoader::parse(const std::string& filePath) {
    BirdParseResult result;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        result.error = "Failed to open file: " + filePath;
        return result;
    }

    // Read entire file into chunks (separated by blank lines)
    std::vector<std::vector<std::string>> chunks;
    {
        std::vector<std::string> currentChunk;
        std::string line;
        while (std::getline(file, line)) {
            std::string trimmed = ltrim(line);
            if (trimmed.empty() || trimmed == ",") {
                if (!currentChunk.empty()) {
                    chunks.push_back(std::move(currentChunk));
                    currentChunk.clear();
                }
            } else {
                currentChunk.push_back(line);
            }
        }
        if (!currentChunk.empty())
            chunks.push_back(std::move(currentChunk));
    }

    // Process each chunk
    // State for building channels
    int currentChannel = -1;
    std::string currentName;

    // Per-channel accumulators
    std::vector<int> currentPattern;
    std::vector<std::vector<int>> currentNoteGroups;
    std::vector<int> currentVelocities;

    // Parser state
    int lastDur = dur::q;
    int lastNote = 60;
    int lastVel = 80;

    auto flushNotes = [&](bool clearPattern = true) {
        if (!currentPattern.empty() && !currentNoteGroups.empty() && !currentVelocities.empty()) {
            int seqLen = result.bars * TICKS_PER_BAR;
            auto resolved = resolveNotes(currentPattern, currentNoteGroups, currentVelocities, seqLen);

            // Find or create channel
            BirdChannel* ch = nullptr;
            for (auto& c : result.channels) {
                if (c.channel == currentChannel) {
                    ch = &c;
                    break;
                }
            }
            if (!ch) {
                result.channels.push_back({currentChannel, currentName, {}});
                ch = &result.channels.back();
            }
            ch->notes.insert(ch->notes.end(), resolved.begin(), resolved.end());
        }
        // Reset note groups and velocities for next v/n block
        currentNoteGroups.clear();
        currentVelocities.clear();
        if (clearPattern)
            currentPattern.clear();
    };

    for (auto& chunk : chunks) {
        for (auto& rawLine : chunk) {
            std::string trimmed = ltrim(rawLine);
            auto tokens = splitLine(trimmed);

            if (tokens.empty() || tokens[0] == "#" || tokens[0][0] == '#')
                continue;

            if (tokens[0] == "b" && tokens.size() > 1) {
                try { result.bars = std::stoi(tokens[1]); } catch (...) {}
            }
            else if (tokens[0] == "ch") {
                // Flush previous channel's pending notes (clear everything)
                flushNotes(true);

                // Start new channel
                if (tokens.size() > 1) {
                    try { currentChannel = std::stoi(tokens[1]) - 1; } catch (...) {}
                }
                currentName = (tokens.size() > 2) ? tokens[2] : ("Track " + std::to_string(currentChannel + 1));
            }
            else if (tokens[0] == "p") {
                // New pattern = flush previous pattern's notes
                flushNotes(true);
                currentPattern = parsePattern(tokens, lastDur);
            }
            else if (tokens[0] == "v") {
                // If we already have note groups, flush the current v/n block
                // but KEEP the pattern for the next v/n block
                if (!currentNoteGroups.empty()) {
                    flushNotes(false);  // don't clear pattern
                }
                currentVelocities = parseVelocities(tokens, lastVel);
            }
            else if (tokens[0] == "n") {
                auto groups = parseNotes(tokens, lastNote);
                currentNoteGroups.insert(currentNoteGroups.end(), groups.begin(), groups.end());
            }
            // Skip: sw, m, cc, var, mix, sec, arr (future work)
        }
    }

    // Flush last channel
    flushNotes(true);

    // Debug output
    for (auto& ch : result.channels) {
        DBG("BirdLoader: Parsed channel " + std::to_string(ch.channel) +
            " '" + ch.name + "' with " + std::to_string(ch.notes.size()) + " notes");
    }

    return result;
}

// --- Populate te::Edit ---

void BirdLoader::populateEdit(te::Edit& edit, const BirdParseResult& result) {
    // Remove existing tracks (except the global/tempo track)
    auto existingTracks = te::getAudioTracks(edit);
    for (int i = existingTracks.size() - 1; i >= 0; i--)
        edit.deleteTrack(existingTracks[i]);

    // Set BPM (use the Edit's current BPM, or default 120)
    auto fourBarsTime = edit.tempoSequence.toTime(
        te::tempo::BarsAndBeats{ result.bars, te::BeatDuration() });

    auto& transport = edit.getTransport();
    transport.setLoopRange(te::TimeRange(te::TimePosition(), fourBarsTime));
    transport.looping = true;

    // Create a track per channel
    for (size_t i = 0; i < result.channels.size(); i++) {
        auto& ch = result.channels[i];

        edit.ensureNumberOfAudioTracks(static_cast<int>(i + 1));
        auto* track = te::getAudioTracks(edit)[static_cast<int>(i)];
        if (!track) continue;

        track->setName(juce::String(ch.name));

        // Add built-in synth plugin
        if (auto synth = edit.getPluginCache().createNewPlugin(
                te::FourOscPlugin::xmlTypeName, {})) {
            track->pluginList.insertPlugin(*synth, 0, nullptr);
        }

        // Create MIDI clip spanning the cycle
        te::TimeRange clipRange(te::TimePosition(), fourBarsTime);
        auto* clipBase = track->insertNewClip(
            te::TrackItem::Type::midi,
            juce::String(ch.name),
            clipRange, nullptr);

        auto* midiClip = dynamic_cast<te::MidiClip*>(clipBase);
        if (!midiClip) continue;

        auto& seq = midiClip->getSequence();

        // Add all resolved notes
        for (auto& note : ch.notes) {
            seq.addNote(
                note.pitch,
                te::BeatPosition::fromBeats(note.beatPos),
                te::BeatDuration::fromBeats(note.duration),
                note.velocity,
                0, nullptr);
        }

        DBG("BirdLoader: Track " + juce::String(ch.name) +
            " — " + juce::String(static_cast<int>(ch.notes.size())) + " notes");
    }

    DBG("BirdLoader: Loaded " + juce::String(static_cast<int>(result.channels.size())) +
        " tracks, " + juce::String(result.bars) + " bar loop");
}

// --- Serialize track notes as JSON for the UI ---

juce::String BirdLoader::getTrackNotesJSON(te::Edit& edit) {
    auto tracks = te::getAudioTracks(edit);
    juce::String json = "[";

    for (int t = 0; t < tracks.size(); t++) {
        auto* track = tracks[t];
        if (t > 0) json += ",";

        json += "{\"id\":" + juce::String(t) +
                ",\"name\":" + juce::JSON::toString(track->getName()) +
                ",\"notes\":[";

        auto clips = track->getClips();
        int noteCount = 0;
        for (auto* clip : clips) {
            auto* midiClip = dynamic_cast<te::MidiClip*>(clip);
            if (!midiClip) continue;

            auto& seq = midiClip->getSequence();
            for (int n = 0; n < seq.getNumNotes(); n++) {
                auto* note = seq.getNote(n);
                if (noteCount > 0) json += ",";
                json += "{\"pitch\":" + juce::String(note->getNoteNumber()) +
                        ",\"beat\":" + juce::String(note->getStartBeat().inBeats(), 3) +
                        ",\"duration\":" + juce::String(note->getLengthBeats().inBeats(), 3) +
                        ",\"velocity\":" + juce::String(note->getVelocity()) + "}";
                noteCount++;
            }
        }

        json += "]}";
    }

    json += "]";
    return json;
}
