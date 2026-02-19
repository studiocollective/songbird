#include "BirdLoader.h"
#include "libraries/theory/note_parser.h"
#include "libraries/sequencing/utils/time_constants.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

// --- Plugin keyword → display names ---
struct PluginInfo {
    juce::String pluginId;
    juce::String pluginName;
};

static PluginInfo pluginFromKeyword(const std::string& keyword) {
    // Arturia classic emulations
    if (keyword == "synths")   return { "arturia.pigments",      "Pigments" };
    if (keyword == "mini")     return { "arturia.mini-v",        "Mini V" };
    if (keyword == "cs80")     return { "arturia.cs-80-v",       "CS-80 V" };
    if (keyword == "prophet")  return { "arturia.prophet-v",     "Prophet V" };
    if (keyword == "jup8")     return { "arturia.jup-8-v",       "Jup-8 V" };
    if (keyword == "dx7")      return { "arturia.dx7-v",         "DX7 V" };
    if (keyword == "buchla")   return { "arturia.buchla-easel-v","Buchla Easel V" };
    // Drums & bass
    if (keyword == "kick")     return { "sonicacademy.kick-2",   "Kick 2" };
    if (keyword == "drums")    return { "softube.heartbeat",     "Heartbeat" };
    if (keyword == "bass")     return { "futureaudioworkshop.sublab-xl", "SubLab XL" };
    return {}; // empty = no external plugin
}

static const PluginInfo CONSOLE_1 = { "softube.console-1", "Console 1" };

// Search the scanned plugin list for a plugin matching the given display name
static std::unique_ptr<juce::PluginDescription> findPluginByName(
    te::Engine& engine, const juce::String& name)
{
    if (name.isEmpty()) return {};

    auto& list = engine.getPluginManager().knownPluginList;
    auto lowerName = name.toLowerCase();

    // Exact match first
    for (const auto& desc : list.getTypes())
        if (desc.name.toLowerCase() == lowerName)
            return std::make_unique<juce::PluginDescription>(desc);

    // Substring match (e.g. "Kick 2" matches "Sonic Academy Kick 2")
    for (const auto& desc : list.getTypes())
        if (desc.name.toLowerCase().contains(lowerName))
            return std::make_unique<juce::PluginDescription>(desc);

    return {};
}

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

// --- Internal: parse a channel block (lines within a ch...until next ch/sec/arr/end) ---
// Returns a BirdChannel with UNRESOLVED notes — just the raw pattern/velocity/note data.
// We can't resolve yet because bar count comes from the arrangement.

struct UnresolvedLayer {
    std::vector<int> pattern;
    std::vector<std::vector<int>> noteGroups;
    std::vector<int> velocities;
};

struct UnresolvedChannel {
    int channel = 0;
    std::string name;
    std::string plugin;
    std::vector<UnresolvedLayer> layers;
};

// Resolve an unresolved channel into a BirdChannel given a bar count
static BirdChannel resolveChannel(const UnresolvedChannel& uch, int bars) {
    BirdChannel ch;
    ch.channel = uch.channel;
    ch.name = uch.name;
    ch.plugin = uch.plugin;

    int seqLen = bars * TICKS_PER_BAR;
    for (auto& layer : uch.layers) {
        auto resolved = BirdLoader::resolveNotes(layer.pattern, layer.noteGroups, layer.velocities, seqLen);
        ch.notes.insert(ch.notes.end(), resolved.begin(), resolved.end());
    }
    return ch;
}

// --- Main parser ---

BirdParseResult BirdLoader::parse(const std::string& filePath) {
    BirdParseResult result;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        result.error = "Failed to open file: " + filePath;
        return result;
    }

    // Read all non-empty, non-comment lines
    std::vector<std::string> allLines;
    {
        std::string line;
        while (std::getline(file, line)) {
            allLines.push_back(line);
        }
    }

    // Parser state
    int lastDur = dur::q;
    int lastNote = 60;
    int lastVel = 80;

    // Current channel being built
    bool inChannel = false;
    UnresolvedChannel currentUCh;
    std::vector<int> currentPattern;
    std::vector<std::vector<int>> currentNoteGroups;
    std::vector<int> currentVelocities;

    // Section state
    std::string currentSectionName;  // empty = top-level
    std::vector<UnresolvedChannel> currentSectionChannels;

    // Top-level unresolved channels (for files without sections)
    std::vector<UnresolvedChannel> topLevelChannels;

    // Sections map: name → list of unresolved channels
    struct SectionDef {
        std::string name;
        std::vector<UnresolvedChannel> channels;
    };
    std::vector<SectionDef> sectionDefs;

    bool inArrangement = false;

    // Flush current velocity/note layer into current channel
    auto flushLayer = [&](bool clearPattern = true) {
        if (!currentPattern.empty() && !currentNoteGroups.empty() && !currentVelocities.empty()) {
            UnresolvedLayer layer;
            layer.pattern = currentPattern;
            layer.noteGroups = currentNoteGroups;
            layer.velocities = currentVelocities;
            currentUCh.layers.push_back(std::move(layer));
        }
        currentNoteGroups.clear();
        currentVelocities.clear();
        if (clearPattern)
            currentPattern.clear();
    };

    // Flush current channel into appropriate container
    auto flushChannel = [&]() {
        flushLayer(true);
        if (inChannel && !currentUCh.layers.empty()) {
            if (currentSectionName.empty())
                topLevelChannels.push_back(currentUCh);
            else
                currentSectionChannels.push_back(currentUCh);
        }
        inChannel = false;
        currentUCh = UnresolvedChannel();
        currentPattern.clear();
        currentNoteGroups.clear();
        currentVelocities.clear();
    };

    // Flush current section
    auto flushSection = [&]() {
        flushChannel();
        if (!currentSectionName.empty() && !currentSectionChannels.empty()) {
            sectionDefs.push_back({currentSectionName, currentSectionChannels});
        }
        currentSectionName.clear();
        currentSectionChannels.clear();
    };

    for (size_t lineIdx = 0; lineIdx < allLines.size(); lineIdx++) {
        auto& rawLine = allLines[lineIdx];
        std::string trimmed = ltrim(rawLine);

        // Skip empty lines and commas (chunk separators)
        if (trimmed.empty() || trimmed == ",")
            continue;

        auto tokens = splitLine(trimmed);
        if (tokens.empty() || tokens[0][0] == '#')
            continue;

        // --- Arrangement block ---
        if (tokens[0] == "arr") {
            flushSection();
            inArrangement = true;
            continue;
        }

        // If we're inside an arrangement block, read indented entries
        if (inArrangement) {
            // Arrangement entries are indented (rawLine starts with whitespace)
            if (rawLine.size() > 0 && (rawLine[0] == ' ' || rawLine[0] == '\t')) {
                if (tokens.size() >= 2) {
                    try {
                        int bars = std::stoi(tokens[1]);
                        result.arrangement.push_back({tokens[0], bars});
                    } catch (...) {}
                }
                continue;
            } else {
                // Not indented — arrangement block is over
                inArrangement = false;
                // Fall through to process this line normally
            }
        }

        // --- Section definition ---
        if (tokens[0] == "sec") {
            flushSection();
            currentSectionName = (tokens.size() > 1) ? tokens[1] : "unnamed";
            continue;
        }

        // --- Global bars ---
        if (tokens[0] == "b" && tokens.size() > 1) {
            try { result.bars = std::stoi(tokens[1]); } catch (...) {}
            continue;
        }

        // --- Channel ---
        if (tokens[0] == "ch") {
            flushChannel();
            inChannel = true;
            if (tokens.size() > 1) {
                try { currentUCh.channel = std::stoi(tokens[1]) - 1; } catch (...) {}
            }
            currentUCh.name = (tokens.size() > 2) ? tokens[2] : ("Track " + std::to_string(currentUCh.channel + 1));
            continue;
        }

        // --- Plugin ---
        if (tokens[0] == "plugin" && tokens.size() > 1) {
            currentUCh.plugin = tokens[1];
            continue;
        }

        // --- Pattern ---
        if (tokens[0] == "p") {
            flushLayer(true);
            currentPattern = parsePattern(tokens, lastDur);
            continue;
        }

        // --- Velocity ---
        if (tokens[0] == "v") {
            if (!currentNoteGroups.empty()) {
                flushLayer(false);  // keep pattern
            }
            currentVelocities = parseVelocities(tokens, lastVel);
            continue;
        }

        // --- Notes ---
        if (tokens[0] == "n") {
            auto groups = parseNotes(tokens, lastNote);
            currentNoteGroups.insert(currentNoteGroups.end(), groups.begin(), groups.end());
            continue;
        }

        // Skip: sw, m, cc, d, _d, etc. (not yet implemented)
    }

    // Flush anything remaining
    flushSection();  // also flushes channel

    // --- Resolve notes ---
    if (!sectionDefs.empty() && !result.arrangement.empty()) {
        // Section-based file: resolve per arrangement entry
        // Build sections from sectionDefs
        // First, compute total bars
        int totalBars = 0;
        for (auto& entry : result.arrangement)
            totalBars += entry.bars;
        result.bars = totalBars;

        // Build a map of section name → section def
        std::map<std::string, SectionDef*> sectionMap;
        for (auto& sd : sectionDefs)
            sectionMap[sd.name] = &sd;

        // Collect unique channel names across all sections to create tracks
        std::map<std::string, int> channelOrder; // name → track index
        for (auto& sd : sectionDefs) {
            for (auto& uch : sd.channels) {
                if (channelOrder.find(uch.name) == channelOrder.end()) {
                    int idx = static_cast<int>(channelOrder.size());
                    channelOrder[uch.name] = idx;
                }
            }
        }

        // Initialize result channels (one per unique channel name)
        result.channels.resize(channelOrder.size());
        for (auto& [name, idx] : channelOrder) {
            result.channels[idx].name = name;
            result.channels[idx].channel = idx;
        }

        // Walk arrangement entries and resolve notes at correct beat offsets
        double beatOffset = 0.0;
        for (auto& entry : result.arrangement) {
            auto it = sectionMap.find(entry.sectionName);
            if (it == sectionMap.end()) continue;

            auto& sd = *it->second;
            for (auto& uch : sd.channels) {
                auto resolved = resolveChannel(uch, entry.bars);

                // Find the target channel by name
                auto orderIt = channelOrder.find(uch.name);
                if (orderIt == channelOrder.end()) continue;
                auto& targetCh = result.channels[orderIt->second];

                // Copy plugin if not yet set
                if (targetCh.plugin.empty())
                    targetCh.plugin = resolved.plugin;

                // Offset all resolved notes by current beat position
                for (auto note : resolved.notes) {
                    note.beatPos += beatOffset;
                    targetCh.notes.push_back(note);
                }
            }

            beatOffset += entry.bars * 4.0; // 4 beats per bar
        }

        // Build sections list for JSON/UI
        double sectionStart = 0.0;
        for (auto& entry : result.arrangement) {
            BirdSection sec;
            sec.name = entry.sectionName;
            // Channels are merged into result.channels; sec.channels left empty
            result.sections.push_back(sec);
            sectionStart += entry.bars;
        }

    } else if (!topLevelChannels.empty()) {
        // Legacy file (no sections): resolve with result.bars
        for (auto& uch : topLevelChannels) {
            result.channels.push_back(resolveChannel(uch, result.bars));
        }
    }

    // Debug output
    for (auto& ch : result.channels) {
        DBG("BirdLoader: Parsed channel " + std::to_string(ch.channel) +
            " '" + ch.name + "' with " + std::to_string(ch.notes.size()) + " notes");
    }
    if (!result.arrangement.empty()) {
        DBG("BirdLoader: Arrangement with " + std::to_string(result.arrangement.size()) +
            " entries, " + std::to_string(result.bars) + " total bars");
    }

    return result;
}

// --- Populate te::Edit ---

void BirdLoader::populateEdit(te::Edit& edit, const BirdParseResult& result, te::Engine& engine) {
    // Clear existing tracks
    for (auto* track : te::getAudioTracks(edit))
        edit.deleteTrack(track);

    double bpm = 120.0;
    edit.tempoSequence.getTempos()[0]->setBpm(bpm);

    auto fourBarsTime = te::TimePosition::fromSeconds((60.0 / bpm) * 4.0 * result.bars);

    auto& transport = edit.getTransport();
    transport.setLoopRange(te::TimeRange(te::TimePosition(), fourBarsTime));
    transport.looping = true;

    // Pre-look-up Console 1 description once
    auto console1Desc = findPluginByName(engine, CONSOLE_1.pluginName);
    if (console1Desc)
        DBG("BirdLoader: Found Console 1 — " + console1Desc->fileOrIdentifier);

    // Create a track per channel
    for (size_t i = 0; i < result.channels.size(); i++) {
        auto& ch = result.channels[i];

        edit.ensureNumberOfAudioTracks(static_cast<int>(i + 1));
        auto* track = te::getAudioTracks(edit)[static_cast<int>(i)];
        if (!track) continue;

        track->setName(juce::String(ch.name));

        // Add instrument plugin based on bird file `plugin` keyword
        auto pluginInfo = pluginFromKeyword(ch.plugin);
        bool instrumentLoaded = false;

        if (pluginInfo.pluginId.isNotEmpty()) {
            // Try to find the plugin in the scanned list
            if (auto foundDesc = findPluginByName(engine, pluginInfo.pluginName)) {
                auto extPlugin = edit.getPluginCache().createNewPlugin(
                    te::ExternalPlugin::xmlTypeName, *foundDesc);
                if (extPlugin) {
                    track->pluginList.insertPlugin(*extPlugin, 0, nullptr);
                    instrumentLoaded = true;
                    DBG("BirdLoader: Loaded '" + foundDesc->name +
                        "' (" + foundDesc->pluginFormatName + ") for track '" + juce::String(ch.name) + "'");
                }
            } else {
                DBG("BirdLoader: Plugin '" + pluginInfo.pluginName +
                    "' not found in scanned list — falling back to FourOsc");
            }
        }

        if (!instrumentLoaded) {
            // Fallback to built-in synth
            if (auto synth = edit.getPluginCache().createNewPlugin(
                    te::FourOscPlugin::xmlTypeName, {})) {
                track->pluginList.insertPlugin(*synth, 0, nullptr);
            }
        }

        // Add Console 1 channel strip to every track (if found)
        if (console1Desc) {
            auto stripPlugin = edit.getPluginCache().createNewPlugin(
                te::ExternalPlugin::xmlTypeName, *console1Desc);
            if (stripPlugin) {
                track->pluginList.insertPlugin(*stripPlugin, -1, nullptr);
                DBG("BirdLoader: Added Console 1 to track '" + juce::String(ch.name) + "'");
            }
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
            " — " + juce::String(static_cast<int>(ch.notes.size())) + " notes" +
            (ch.plugin.empty() ? "" : " [plugin: " + ch.plugin + "]"));
    }

    DBG("BirdLoader: Loaded " + juce::String(static_cast<int>(result.channels.size())) +
        " tracks, " + juce::String(result.bars) + " bar loop");
}

// --- Serialize track notes as JSON for the UI ---

juce::String BirdLoader::getTrackNotesJSON(te::Edit& edit, const BirdParseResult* parseResult) {
    auto tracks = te::getAudioTracks(edit);
    juce::String json = "{\"tracks\":[";

    for (int t = 0; t < tracks.size(); t++) {
        auto* track = tracks[t];
        if (t > 0) json += ",";

        // Look up plugin info from parse result
        juce::String pluginField;
        juce::String channelStripField;
        if (parseResult && t < static_cast<int>(parseResult->channels.size())) {
            auto info = pluginFromKeyword(parseResult->channels[t].plugin);
            if (info.pluginId.isNotEmpty()) {
                pluginField = ",\"plugin\":{\"pluginId\":" + juce::JSON::toString(info.pluginId) +
                              ",\"pluginName\":" + juce::JSON::toString(info.pluginName) + "}";
            }
            // Console 1 on every channel
            channelStripField = ",\"channelStrip\":{\"pluginId\":" + juce::JSON::toString(CONSOLE_1.pluginId) +
                                ",\"pluginName\":" + juce::JSON::toString(CONSOLE_1.pluginName) + "}";
        }

        json += "{\"id\":" + juce::String(t) +
                ",\"name\":" + juce::JSON::toString(track->getName()) +
                pluginField + channelStripField +
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

    json += "],\"sections\":[";

    // Add sections from arrangement
    if (parseResult && !parseResult->arrangement.empty()) {
        int barOffset = 0;
        for (size_t i = 0; i < parseResult->arrangement.size(); i++) {
            auto& entry = parseResult->arrangement[i];
            if (i > 0) json += ",";
            json += "{\"name\":" + juce::JSON::toString(juce::String(entry.sectionName)) +
                    ",\"start\":" + juce::String(barOffset) +
                    ",\"length\":" + juce::String(entry.bars) + "}";
            barOffset += entry.bars;
        }
    }

    json += "],\"totalBars\":" + juce::String(parseResult ? parseResult->bars : 1) + "}";
    return json;
}
