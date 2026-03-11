#include "BirdParser.h"
#include "libraries/theory/note_parser.h"
#include "libraries/sequencing/utils/time_constants.h"

#include <sstream>
#include <algorithm>
#include <cmath>

// --- Tick-to-beat conversion ---

double ticksToBeats(int ticks) {
    return static_cast<double>(ticks) / TICKS_PER_BEAT;
}

// --- String helpers ---

std::string ltrim(const std::string& s) {
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
            int parsed = dur_from_string(tokens[i]);
            if (parsed == 0) {
                DBG("BirdLoader: Unknown duration '" + tokens[i] + "', defaulting to 'q'");
                parsed = q;
            }
            lastDur = parsed;
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
            try {
                int newVel = lastVel + std::stoi(tokens[i]);
                newVel = std::clamp(newVel, 0, 127);
                lastVel = newVel;
                velocities.push_back(newVel);
            } catch (...) {}
        } else {
            try {
                lastVel = std::stoi(tokens[i]);
                velocities.push_back(lastVel);
            } catch (...) {}
        }
    }
    return velocities;
}

// --- Note parsing ---
// Handles MIDI numbers, note names (C4), chord names (@Cm7),
// repeats (-), and relative offsets (+N/-N)

std::vector<std::vector<int>> BirdLoader::parseNotes(const std::vector<std::string>& tokens, int& lastNote) {
    std::vector<std::vector<int>> noteGroups;
    
    // Check if ALL tokens (after "n") are plain MIDI numbers or note names (no relative offsets, repeats, @chords).
    // If so, they all form a single simultaneous chord group.
    // Example: `n 41 48 53 56` -> one chord group {41, 48, 53, 56}
    // Example: `n @Cmaj7` -> one chord group from the chord parser
    // Example: `n 60 - +2` -> three separate sequential steps (mixed syntax = sequential)
    bool allPlainNotes = tokens.size() > 1;
    for (size_t i = 1; i < tokens.size() && allPlainNotes; i++) {
        const auto& t = tokens[i];
        if (t == "-") { allPlainNotes = false; break; }
        if (t[0] == '+') { allPlainNotes = false; break; }
        if (t[0] == '-' && t.size() > 1 && !std::isdigit(t[1])) { allPlainNotes = false; break; }
        // Relative offsets like `-2` start with '-' followed by a digit — that's sequential too
        if (t[0] == '-' && t.size() > 1 && std::isdigit(t[1])) { allPlainNotes = false; break; }
        // @chords are already self-contained multi-note groups — keep them sequential
        if (is_chord_name(t)) { allPlainNotes = false; break; }
    }
    
    if (allPlainNotes && tokens.size() > 2) {
        // Multiple plain notes on one line → one chord group
        std::vector<int> chord;
        for (size_t i = 1; i < tokens.size(); i++) {
            const auto& t = tokens[i];
            if (is_note_name(t)) {
                int midi = midi_from_note_name(t);
                if (midi >= 0) chord.push_back(midi);
            } else {
                try {
                    int midi = std::stoi(t);
                    chord.push_back(midi);
                } catch (...) {}
            }
        }
        if (!chord.empty()) {
            lastNote = chord[0];
            noteGroups.push_back(chord);
        }
        return noteGroups;
    }
    
    // Original sequential parsing — one group per token
    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "-") {
            noteGroups.push_back({lastNote});
        } else if ((tokens[i][0] == '+' || (tokens[i][0] == '-' && tokens[i].size() > 1))) {
            try {
                int newNote = lastNote + std::stoi(tokens[i]);
                lastNote = newNote;
                noteGroups.push_back({newNote});
            } catch (...) {}
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
            try {
                lastNote = std::stoi(tokens[i]);
                noteGroups.push_back({lastNote});
            } catch (...) {}
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
    const std::map<std::string, std::vector<std::string>>& stepAutomations,
    const std::vector<int>& timingOffsets,
    int swingPercent,
    int humanizeTicks,
    int sequenceLength,
    PatternState& state)
{
    std::vector<BirdNote> result;
    if (pattern.empty() || noteGroups.empty() || velocities.empty())
        return result;

    // Safety checks against vector sizes changing between sections
    if (state.patIdx >= pattern.size()) state.patIdx = 0;
    if (state.noteIdx >= noteGroups.size()) state.noteIdx = 0;
    if (state.velIdx >= velocities.size()) state.velIdx = 0;

    int ticks = 0;

    while (ticks < sequenceLength) {
        int durConfig = pattern[state.patIdx];
        int durTotal = std::abs(durConfig);
        
        if (durTotal == 0) {
            durTotal = q; // fallback to prevent infinite loop
            durConfig = durTotal; // treat as note-on
        }

        int durRemaining = durTotal - state.ticksInStep;

        if (state.ticksInStep == 0 && durConfig > 0) {
            // --- Calculate timing offset ---
            double offset = 0.0;

            // 1. Swing: shift odd-indexed pattern steps
            if (swingPercent != 50 && state.patIdx % 2 == 1) {
                // swingPercent 50 = straight, 67 = triplet swing
                // Offset the odd note by a fraction of the previous step duration
                offset += durTotal * (swingPercent - 50) / 50.0;
            }

            // 2. Per-note timing offset (t line)
            if (!timingOffsets.empty()) {
                offset += timingOffsets[state.patIdx % timingOffsets.size()];
            }

            // 3. Humanize jitter
            if (humanizeTicks > 0) {
                offset += (rand() % (2 * humanizeTicks + 1)) - humanizeTicks;
            }

            // Clamp: don't shift before the start of the sequence or past the end
            int adjustedTicks = std::max(0, std::min(sequenceLength - 1, ticks + (int)offset));

            // Note-on: emit all notes in the current group
            for (int pitch : noteGroups[state.noteIdx]) {
                BirdNote n;
                n.pitch = pitch;
                n.beatPos = ticksToBeats(adjustedTicks);
                n.duration = ticksToBeats(static_cast<int>(durConfig * 0.9)); // 90% gate
                n.velocity = velocities[state.velIdx];
                
                // Add any step automations that correspond to this pattern step
                for (const auto& [macro, values] : stepAutomations) {
                    if (!values.empty()) {
                        size_t macroIdx = state.patIdx % values.size(); // Or keep its own state index in future
                        try {
                            // Extract numeric portion (ignoring symbols for step values for now)
                            std::string valStr = values[macroIdx];
                            std::string numStr = "";
                            for (char c : valStr) {
                                if (isdigit(c) || c == '.' || c == '-') numStr += c;
                            }
                            if (!numStr.empty()) {
                                n.stepParams[macro] = std::stof(numStr) / 100.0f; // Scale 0-100 to 0.0-1.0
                            }
                        } catch (...) {}
                    }
                }
                
                result.push_back(n);
            }
            state.noteIdx = (state.noteIdx + 1) % noteGroups.size();
            state.velIdx = (state.velIdx + 1) % velocities.size();
        }

        // Advance time
        if (ticks + durRemaining > sequenceLength) {
            // Step spans beyond the current sequence
            state.ticksInStep += (sequenceLength - ticks);
            ticks = sequenceLength;
        } else {
            // Step completes within or exactly at the end of the sequence
            ticks += durRemaining;
            state.ticksInStep = 0;
            state.patIdx = (state.patIdx + 1) % pattern.size();
        }
    }

    return result;
}

// --- Channel resolution ---
// Returns a pair: {Resolved BirdChannel, new PatternState for the next section}

std::pair<BirdChannel, BirdLoader::PatternState> resolveChannel(const UnresolvedChannel& uch, int bars, BirdLoader::PatternState state) {
    BirdChannel ch;
    ch.channel   = uch.channel;
    ch.name      = uch.name;
    ch.trackType = uch.trackType.empty() ? "midi" : uch.trackType;
    ch.plugin    = uch.plugin;
    ch.fx        = uch.fx;
    ch.strip     = uch.strip;
    ch.automation = uch.automation;

    int seqLen = bars * TICKS_PER_BAR;
    
    // Compute original pattern length (one pass through the pattern array)
    int maxPatternTicks = 0;
    for (auto& layer : uch.layers) {
        int patTicks = 0;
        for (int dur : layer.pattern)
            patTicks += std::abs(dur);
        if (patTicks > maxPatternTicks)
            maxPatternTicks = patTicks;
    }
    double sectionBeats = ticksToBeats(seqLen);
    double patBeats = ticksToBeats(maxPatternTicks);
    // If pattern fills the whole section (or is longer), no looping needed
    ch.patternBeats = (patBeats > 0 && patBeats < sectionBeats) ? patBeats : sectionBeats;

    // For simplicity, we assume single-layer for continuous patterns right now
    for (auto& layer : uch.layers) {
        auto resolved = BirdLoader::resolveNotes(layer.pattern, layer.noteGroups, layer.velocities, layer.stepAutomations, layer.timingOffsets, layer.swingPercent, layer.humanizeTicks, seqLen, state);
        ch.notes.insert(ch.notes.end(), resolved.begin(), resolved.end());
    }
    return {ch, state};
}
