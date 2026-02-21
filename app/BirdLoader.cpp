#include "BirdLoader.h"
#include "MacroMapper.h"
#include "libraries/magenta/LyriaPlugin.h"
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
    if (keyword == "surge")    return { "surge-synth-team.surge-xt", "Surge XT" };
    if (keyword == "mini")     return { "arturia.mini-v",        "Mini V" };
    if (keyword == "cs80")     return { "arturia.cs-80-v",       "CS-80 V" };
    if (keyword == "prophet")  return { "arturia.prophet-v",     "Prophet-5 V" };
    if (keyword == "jup8")     return { "arturia.jup-8-v",       "Jup-8 V" };
    if (keyword == "dx7")      return { "arturia.dx7-v",         "DX7 V" };
    if (keyword == "buchla")   return { "arturia.buchla-easel-v","Buchla Easel V" };
    // Drums & bass
    if (keyword == "kick")     return { "sonicacademy.kick-3",   "Kick 3" };
    if (keyword == "drums")    return { "softube.heartbeat",     "Heartbeat" };
    if (keyword == "bass")     return { "arturia.mini-v",        "Mini V" };
    if (keyword == "monoment") return { "softube.monoment-bass", "Monoment Bass" };
    if (keyword == "sublab")   return { "futureaudioworkshop.sublabxl", "SubLabXL" };
    // Effects
    if (keyword == "delay")    return { "softube.tube-delay", "Tube Delay" };
    if (keyword == "valhalla") return { "valhalladsp.valhallaroom", "ValhallaRoom" };
    if (keyword == "widener")  return { "polyversemusic.widener", "Widener" };
    if (keyword == "soothe")   return { "oeksound.soothe2", "soothe2" };
    if (keyword == "tube")     return { "arturia.dist-tube-culture", "Dist TUBE-CULTURE" };
    
    // Channel Strips
    if (keyword == "console1") return { "softube.console-1", "Console 1" };
    
    return {}; // empty = no external plugin
}

static const PluginInfo CONSOLE_1 = { "softube.console-1", "Console 1" };
static const PluginInfo REVERB_VALHALLA = { "valhalladsp.valhallaroom", "ValhallaRoom" };
static const PluginInfo DELAY_TUBE = { "softube.tube-delay", "Tube Delay" };
static const PluginInfo DIST_CULTURE = { "arturia.dist-tube-culture", "Dist TUBE-CULTURE" };
static const PluginInfo WEISS_DS1 = { "softube.ds1-mk3", "DS1-MK3" };

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

// --- Internal: parse a channel block (lines within a ch...until next ch/sec/arr/end) ---
// Returns a BirdChannel with UNRESOLVED notes — just the raw pattern/velocity/note data.
// We can't resolve yet because bar count comes from the arrangement.

struct UnresolvedLayer {
    std::vector<int> pattern;
    std::vector<std::vector<int>> noteGroups;
    std::vector<int> velocities;
    std::map<std::string, std::vector<std::string>> stepAutomations;
    std::vector<int> timingOffsets;  // per-note tick offsets (t line)
    int swingPercent = 50;           // 50 = straight, 67 = triplet swing
    int humanizeTicks = 0;           // random ±ticks jitter
};

struct UnresolvedChannel {
    int channel = 0;
    std::string name;
    std::string trackType = "midi"; // default
    std::string plugin;
    std::string fx;
    std::string strip;
    bool cont = false;
    std::vector<UnresolvedLayer> layers;
    std::map<std::string, BirdAutomationCurve> automation;
};

// Returns a pair: {Resolved BirdChannel, new PatternState for the next section}
static std::pair<BirdChannel, BirdLoader::PatternState> resolveChannel(const UnresolvedChannel& uch, int bars, BirdLoader::PatternState state = {}) {
    BirdChannel ch;
    ch.channel   = uch.channel;
    ch.name      = uch.name;
    ch.trackType = uch.trackType.empty() ? "midi" : uch.trackType;
    ch.plugin    = uch.plugin;
    ch.fx        = uch.fx;
    ch.strip     = uch.strip;
    ch.automation = uch.automation;

    int seqLen = bars * TICKS_PER_BAR;
    
    // For simplicity, we assume single-layer for continuous patterns right now
    for (auto& layer : uch.layers) {
        auto resolved = BirdLoader::resolveNotes(layer.pattern, layer.noteGroups, layer.velocities, layer.stepAutomations, layer.timingOffsets, layer.swingPercent, layer.humanizeTicks, seqLen, state);
        ch.notes.insert(ch.notes.end(), resolved.begin(), resolved.end());
    }
    return {ch, state};
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
    std::map<std::string, std::vector<std::string>> currentStepAutomations;
    std::vector<int> currentTimingOffsets;
    int currentSwing = 50;
    int currentHumanize = 0;

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
            layer.stepAutomations = currentStepAutomations;
            layer.timingOffsets = currentTimingOffsets;
            layer.swingPercent = currentSwing;
            layer.humanizeTicks = currentHumanize;
            currentUCh.layers.push_back(std::move(layer));
        }
        currentNoteGroups.clear();
        currentVelocities.clear();
        currentStepAutomations.clear();
        currentTimingOffsets.clear();
        if (clearPattern)
            currentPattern.clear();
    };

    // Flush current channel into appropriate container
    auto flushChannel = [&]() {
        flushLayer(true);
        if (inChannel) {
            bool hasLayers = !currentUCh.layers.empty();
            bool hasConfig = !currentUCh.plugin.empty() || !currentUCh.fx.empty() || !currentUCh.strip.empty() || !currentUCh.automation.empty();
            if (hasLayers || hasConfig) {
                if (currentSectionName.empty())
                    topLevelChannels.push_back(currentUCh);
                else
                    currentSectionChannels.push_back(currentUCh);
            }
        }
        inChannel = false;
        currentUCh = UnresolvedChannel();
        currentPattern.clear();
        currentNoteGroups.clear();
        currentVelocities.clear();
        currentStepAutomations.clear();
        currentTimingOffsets.clear();
        currentSwing = 50;
        currentHumanize = 0;
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

        // --- Key Signature ---
        if (tokens[0] == "key" && tokens.size() > 1) {
            std::string keyString = tokens[1];
            for (size_t i = 2; i < tokens.size(); ++i) {
                keyString += " " + tokens[i];
            }
            
            bool isMinor = false;
            int sharps = sharps_from_key_name(keyString, isMinor);
            if (sharps != -99) {
                result.keySharpsFlats = sharps;
                result.keyIsMinor = isMinor;
                result.keyName = keyString;
                result.hasKeySignature = true;
            } else {
                DBG("BirdLoader: Failed to parse key signature '" + keyString + "'");
            }
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

        // --- Track type ---
        // Syntax: `  type midi` | `  type audio` | `  type gen-audio`
        // Default is gen-midi (AI-composed bird notation) for backward compatibility.
        if (tokens[0] == "type" && tokens.size() > 1) {
            auto& t = tokens[1];
            if (t == "midi" || t == "audio")
                currentUCh.trackType = t;
            else
                DBG("BirdLoader: Unknown track type '" + juce::String(t) + "', ignoring");
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

        // --- Swing ---
        if (tokens[0] == "sw") {
            if (tokens.size() >= 2) {
                try { currentSwing = std::stoi(tokens[1]); } catch (...) {}
            }
            // Parse optional ~N humanize
            for (size_t i = 2; i < tokens.size(); i++) {
                if (!tokens[i].empty() && tokens[i][0] == '~') {
                    try { currentHumanize = std::stoi(tokens[i].substr(1)); } catch (...) {}
                }
            }
            continue;
        }

        // --- Per-note timing offsets ---
        if (tokens[0] == "t") {
            currentTimingOffsets.clear();
            for (size_t i = 1; i < tokens.size(); i++) {
                try { currentTimingOffsets.push_back(std::stoi(tokens[i])); } catch (...) { currentTimingOffsets.push_back(0); }
            }
            continue;
        }

        // --- FX ---
        if (tokens[0] == "fx" && tokens.size() > 1) {
            currentUCh.fx = tokens[1];
            continue;
        }

        // --- Strip ---
        if (tokens[0] == "strip" && tokens.size() > 1) {
            currentUCh.strip = tokens[1];
            continue;
        }

        // --- Cont ---
        if (tokens[0] == "cont") {
            currentUCh.cont = true;
            continue;
        }

        // --- Handle Semantic Macros & Automation ---
        // If a keyword isn't matched by the core structural syntax above,
        // it is assumed to be an automation macro targeting a plugin parameter.
        if (inChannel) {
            // Is it continuous (e.g. `brightness ramp 0.2 1.0`) or stepped (e.g. `cutoff 80/ 40)`)?
            if (tokens.size() > 1) {
                // If the second token is a generator type like "ramp" or "lfo"
                if (tokens[1] == "ramp" || tokens[1] == "lfo") {
                    BirdAutomationCurve curve;
                    curve.macroName = tokens[0];
                    // Example: brightness ramp 0.2 1.0 4b
                    // We'll parse this explicitly here. For now, just linear ramp support.
                    if (tokens[1] == "ramp" && tokens.size() >= 4) {
                        try {
                            float startVal = std::stof(tokens[2]);
                            float endVal = std::stof(tokens[3]);
                            
                            BirdAutomationPoint p1;
                            p1.time = 0.0;
                            p1.value = startVal;
                            p1.shape = BirdAutomationPoint::Linear;
                            curve.points.push_back(p1);
                            
                            BirdAutomationPoint p2;
                            p2.time = 4.0; // defaulting to 1 bar right now if length isn't parsed
                            p2.value = endVal;
                            p2.shape = BirdAutomationPoint::Step;
                            curve.points.push_back(p2);
                            
                            currentUCh.automation[curve.macroName] = curve;
                        } catch (...) {}
                    }
                } else {
                    // Otherwise, it's a step-based automation parameter (e.g., `cutoff 80/ 40) 60`)
                    std::vector<std::string> stepValues;
                    for (size_t i = 1; i < tokens.size(); i++) {
                        stepValues.push_back(tokens[i]);
                    }
                    currentStepAutomations[tokens[0]] = stepValues;
                }
            }
        }

        // Skip: m, cc, d, _d, etc. (not yet implemented)
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

        // Build a map of global channel configurations from topLevelChannels
        std::map<int, UnresolvedChannel> globalConfigs;
        for (auto& ch : topLevelChannels) {
            globalConfigs[ch.channel] = ch;
        }

        // Collect unique channel names across all sections and globals to create tracks
        std::map<std::string, int> channelOrder; // name → track index
        
        // Add global channels first so they dictate track order
        for (auto& ch : topLevelChannels) {
            if (channelOrder.find(ch.name) == channelOrder.end()) {
                int idx = static_cast<int>(channelOrder.size());
                channelOrder[ch.name] = idx;
            }
        }
        
        // Then add any section-specific channels
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

        // Phase tracking for 'cont' patterns across sections
        std::map<int, BirdLoader::PatternState> trackStates;

        // Walk arrangement entries and resolve notes at correct beat offsets
        double beatOffset = 0.0;
        for (auto& entry : result.arrangement) {
            auto it = sectionMap.find(entry.sectionName);
            if (it == sectionMap.end()) continue;

            auto& sd = *it->second;
            for (auto& uch : sd.channels) {
                // Merge with global config if available
                UnresolvedChannel mergedCh = uch;
                if (globalConfigs.find(uch.channel) != globalConfigs.end()) {
                    auto& globalCh = globalConfigs[uch.channel];
                    if (mergedCh.plugin.empty()) mergedCh.plugin = globalCh.plugin;
                    if (mergedCh.fx.empty()) mergedCh.fx = globalCh.fx;
                    if (mergedCh.strip.empty()) mergedCh.strip = globalCh.strip;
                    // If name is default "Track X", use global name
                    if (mergedCh.name == "Track " + std::to_string(uch.channel + 1) && !globalCh.name.empty()) {
                        mergedCh.name = globalCh.name;
                    }
                }

                // Handle continuity state
                BirdLoader::PatternState state;
                if (mergedCh.cont) {
                    state = trackStates[mergedCh.channel];
                }

                auto [resolved, newState] = resolveChannel(mergedCh, entry.bars, state);
                
                // Save state for next time
                trackStates[mergedCh.channel] = newState;

                // Find the target channel by name
                auto orderIt = channelOrder.find(uch.name);
                if (orderIt == channelOrder.end()) continue;
                auto& targetCh = result.channels[orderIt->second];

                // Copy plugin/fx/strip if not yet set
                if (targetCh.plugin.empty())
                    targetCh.plugin = resolved.plugin;
                if (targetCh.fx.empty())
                    targetCh.fx = resolved.fx;
                if (targetCh.strip.empty())
                    targetCh.strip = resolved.strip;

                // Offset all resolved notes by current beat position
                for (auto note : resolved.notes) {
                    note.beatPos += beatOffset;
                    targetCh.notes.push_back(note);
                }
            }

            beatOffset += entry.bars * 4.0; // 4 beats per bar
        }

        // Build sections list for JSON/UI
        for (auto& entry : result.arrangement) {
            BirdSection sec;
            sec.name = entry.sectionName;
            // Channels are merged into result.channels; sec.channels left empty
            result.sections.push_back(sec);
        }

    } else if (!topLevelChannels.empty()) {
        // Legacy file (no sections): resolve with result.bars
        for (auto& uch : topLevelChannels) {
            auto [resolvedCh, patternState] = resolveChannel(uch, result.bars);
            result.channels.push_back(resolvedCh);
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
    // Stash existing track settings by name
    struct TrackState {
        float volume = 1.0f;
        float pan = 0.0f;
        bool mute = false;
        bool solo = false;
    };
    std::map<juce::String, TrackState> previousStates;
    
    auto currentTracks = te::getAudioTracks(edit);
    for (auto* track : currentTracks) {
        if (track) {
            TrackState ts;
            ts.mute = track->isMuted(false);
            ts.solo = track->isSolo(false);
            if (auto volPlugin = track->getVolumePlugin()) {
                ts.volume = volPlugin->getVolumeDb();
                ts.pan = volPlugin->getPan();
            }
            previousStates[track->getName()] = ts;
        }
    }

    // Delete excess tracks
    int expectedTracks = static_cast<int>(result.channels.size()) + 4;
    for (int i = currentTracks.size() - 1; i >= expectedTracks; --i) {
        edit.deleteTrack(currentTracks[i]);
    }

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

        // Restore track settings if we had them
        if (previousStates.find(track->getName()) != previousStates.end()) {
            auto& ts = previousStates[track->getName()];
            track->setMute(ts.mute);
            track->setSolo(ts.solo);
            if (auto volPlugin = track->getVolumePlugin()) {
                volPlugin->setVolumeDb(ts.volume); // It's already in dB
                volPlugin->setPan(ts.pan);
            }
        }

        // Determine required plugins
        juce::StringArray requiredPlugins;
        bool instrumentLoaded = false;
        
        auto pluginInfo = pluginFromKeyword(ch.plugin);
        if (pluginInfo.pluginId.isNotEmpty()) requiredPlugins.add(pluginInfo.pluginName);
        else requiredPlugins.add("4OSC");

        if (!ch.fx.empty()) {
            auto fxInfo = pluginFromKeyword(ch.fx);
            if (fxInfo.pluginId.isNotEmpty()) requiredPlugins.add(fxInfo.pluginName);
        }
        if (!ch.strip.empty()) {
            auto stripInfo = pluginFromKeyword(ch.strip);
            if (stripInfo.pluginId.isNotEmpty()) requiredPlugins.add(stripInfo.pluginName);
        }

        // Get current external plugins
        juce::StringArray currentPlugins;
        auto plugins = track->pluginList.getPlugins();
         for (auto* p : plugins) {
            if (dynamic_cast<te::ExternalPlugin*>(p)) {
                currentPlugins.add(p->getName());
            } else if (dynamic_cast<te::FourOscPlugin*>(p)) {
                currentPlugins.add("4OSC"); // Match the required side
            }
        }

        bool pluginsMatch = (requiredPlugins == currentPlugins);

        if (!pluginsMatch) {
            DBG("BirdLoader: Track '" + juce::String(ch.name) + "' plugin change [" + currentPlugins.joinIntoString(",") + "] -> [" + requiredPlugins.joinIntoString(",") + "], rebuilding");
            // Remove old plugins
            for (auto* p : plugins) {
                if (dynamic_cast<te::ExternalPlugin*>(p) || dynamic_cast<te::FourOscPlugin*>(p)) {
                    p->removeFromParent();
                }
            }

            // Add instrument plugin based on bird file `plugin` keyword
            if (pluginInfo.pluginId.isNotEmpty()) {
                if (auto foundDesc = findPluginByName(engine, pluginInfo.pluginName)) {
                    auto extPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc);
                    if (extPlugin) {
                        track->pluginList.insertPlugin(*extPlugin, 0, nullptr);
                        instrumentLoaded = true;
                        DBG("BirdLoader: Loaded '" + foundDesc->name + "' for track '" + juce::String(ch.name) + "'");
                    }
                } else {
                    DBG("BirdLoader: Plugin '" + pluginInfo.pluginName + "' not found in scanned list — falling back to FourOsc");
                }
            }

            if (!instrumentLoaded) {
                if (auto synth = edit.getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {})) {
                    track->pluginList.insertPlugin(*synth, 0, nullptr);
                }
            }

            // Add FX plugin if specified
            if (!ch.fx.empty()) {
                auto fxInfo = pluginFromKeyword(ch.fx);
                if (fxInfo.pluginId.isNotEmpty()) {
                    if (auto foundDesc = findPluginByName(engine, fxInfo.pluginName)) {
                        if (auto fxPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc)) {
                            track->pluginList.insertPlugin(*fxPlugin, -1, nullptr); 
                            DBG("BirdLoader: Added FX '" + foundDesc->name + "' to track '" + juce::String(ch.name) + "'");
                        }
                    }
                }
            }

            // Add Channel Strip if specified
            if (!ch.strip.empty()) {
                auto stripInfo = pluginFromKeyword(ch.strip);
                if (stripInfo.pluginId.isNotEmpty()) {
                    if (auto foundDesc = findPluginByName(engine, stripInfo.pluginName)) {
                        if (auto stripPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc)) {
                            track->pluginList.insertPlugin(*stripPlugin, -1, nullptr); 
                            DBG("BirdLoader: Added Strip '" + foundDesc->name + "' to track '" + juce::String(ch.name) + "'");
                        }
                    }
                }
            }
        } else {
            DBG("BirdLoader: Plugins match, skipping tear-down for track '" + juce::String(ch.name) + "'");
        }

        // --- Handle MIDI Clip ---
        te::TimeRange clipRange(te::TimePosition(), fourBarsTime);
        auto clips = track->getClips();
        te::MidiClip* midiClip = nullptr;

        for (auto* clip : clips) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                midiClip = mc;
                break;
            }
        }

        if (!midiClip) {
            auto* clipBase = track->insertNewClip(
                te::TrackItem::Type::midi,
                juce::String(ch.name),
                clipRange, nullptr);
            midiClip = dynamic_cast<te::MidiClip*>(clipBase);
        } else {
            // Resize if needed (e.g. bars count changed)
            midiClip->setStart(te::TimePosition(), false, false);
            midiClip->setLength(clipRange.getLength(), false);
            midiClip->setOffset(te::TimeDuration());
            midiClip->setName(juce::String(ch.name));
        }

        if (!midiClip) continue;

        // --- Diff MIDI notes: only clear+refill if notes changed ---
        // This is the hot-path: a notes-only LLM edit costs almost nothing.
        auto& seq = midiClip->getSequence();
        const auto& existingNotes = seq.getNotes();
        bool notesChanged = (existingNotes.size() != ch.notes.size());
        if (!notesChanged) {
            for (size_t n = 0; n < ch.notes.size() && !notesChanged; ++n) {
                const auto& existing = *existingNotes[static_cast<int>(n)];
                const auto& incoming = ch.notes[n];
                if (existing.getNoteNumber() != incoming.pitch ||
                    std::abs(existing.getStartBeat().inBeats() - incoming.beatPos) > 1e-6 ||
                    std::abs(existing.getLengthBeats().inBeats() - incoming.duration) > 1e-6 ||
                    existing.getVelocity() != incoming.velocity)
                {
                    notesChanged = true;
                }
            }
        }

        if (notesChanged) {
            seq.clear(nullptr);
            for (auto& note : ch.notes) {
                seq.addNote(
                    note.pitch,
                    te::BeatPosition::fromBeats(note.beatPos),
                    te::BeatDuration::fromBeats(note.duration),
                    note.velocity,
                    0, nullptr);
            }
            DBG("BirdLoader: Track '" + juce::String(ch.name) + "' MIDI updated (" + juce::String((int)ch.notes.size()) + " notes)");
        } else {
            DBG("BirdLoader: Track '" + juce::String(ch.name) + "' MIDI unchanged, skipping refill");
        }

        // --- Add Sends (Regular Tracks) ---
        for (int bus = 0; bus < 4; bus++) {
            bool found = false;
            for (auto* p : track->pluginList.getPlugins()) {
                if (auto* send = dynamic_cast<te::AuxSendPlugin*>(p)) {
                    if (send->busNumber == bus) { found = true; break; }
                }
            }
            if (!found) {
                if (auto plugin = edit.getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {})) {
                    track->pluginList.insertPlugin(*plugin, -1, nullptr);
                    auto* sendPlugin = dynamic_cast<te::AuxSendPlugin*>(plugin.get());
                    sendPlugin->busNumber = bus;
                    sendPlugin->setGainDb(-100.0f); // Default to muted
                }
            }
        }

        // --- Apply Automation Curves ---
        // Helper to find parameter
        auto applyAutomation = [&](const juce::String& macroName, const juce::String& pluginName, const std::vector<BirdAutomationPoint>& points, double timeOffsetBeats = 0.0) {
            juce::String paramId = MacroMapper::getParameterID(pluginName, macroName);
            if (paramId.isEmpty()) return;

            // Find the plugin
            te::Plugin::Ptr targetPlugin;
            for (auto* p : track->pluginList.getPlugins()) {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p)) {
                    if (ext->getName() == pluginName || ext->desc.name.containsIgnoreCase(pluginName)) {
                        targetPlugin = p;
                        break;
                    }
                }
            }
            if (!targetPlugin) return;

            // Find the parameter
            te::AutomatableParameter::Ptr targetParam;
            for (auto* param : targetPlugin->getAutomatableParameters()) {
                if (param->paramID == paramId || param->getPluginAndParamName() == paramId || param->getParameterName().containsIgnoreCase(paramId)) {
                    targetParam = param;
                    break;
                }
            }
            
            if (targetParam) {
                auto& curve = targetParam->getCurve();
                
                // Write points
                for (auto& pt : points) {
                    double actualBeats = pt.time + timeOffsetBeats;
                    auto timePos = edit.tempoSequence.toTime(te::BeatPosition::fromBeats(actualBeats));
                    float val = pt.value;
                    
                    // Simple linear curve for now 
                    float curveShape = 0.0f; 
                    if (pt.shape == BirdAutomationPoint::Exponential || pt.shape == BirdAutomationPoint::Logarithmic) curveShape = 0.5f; 
                    
                    curve.addPoint(timePos, val, curveShape, nullptr);
                }
            }
        };

        // 1. Continuous section-level automation
        for (const auto& [macro, curve] : ch.automation) {
            // For now, assume it targets the main instrument plugin
            if (pluginInfo.pluginName.isNotEmpty()) {
                applyAutomation(macro, pluginInfo.pluginName, curve.points, 0.0);
            }
        }

        // 2. Step-based pattern-level automation attached to notes
        std::map<juce::String, std::vector<BirdAutomationPoint>> stepCurvesByMacro;
        
        for (const auto& note : ch.notes) {
            for (const auto& [macro, value] : note.stepParams) {
                BirdAutomationPoint pt;
                pt.time = note.beatPos;
                pt.value = value;
                pt.shape = BirdAutomationPoint::Step; // TODO parse shapes from strings
                stepCurvesByMacro[macro].push_back(pt);
            }
        }
        
        for (const auto& [macro, points] : stepCurvesByMacro) {
            if (pluginInfo.pluginName.isNotEmpty()) {
                applyAutomation(macro, pluginInfo.pluginName, points, 0.0);
            }
        }

        DBG("BirdLoader: Track " + juce::String(ch.name) +
            " — " + juce::String(static_cast<int>(ch.notes.size())) + " notes" +
            (ch.plugin.empty() ? "" : " [plugin: " + ch.plugin + "]"));
    }

    // --- Create/Configure 4 Return Tracks ---
    int numRegularTracks = static_cast<int>(result.channels.size());
    for (int r = 0; r < 4; r++) {
        int trackIdx = numRegularTracks + r;
        edit.ensureNumberOfAudioTracks(trackIdx + 1);
        auto* track = te::getAudioTracks(edit)[trackIdx];
        if (!track) continue;

        track->setName("Return " + juce::String(r + 1));

        // Restore track settings if we had them
        if (previousStates.find(track->getName()) != previousStates.end()) {
            auto& ts = previousStates[track->getName()];
            track->setMute(ts.mute);
            track->setSolo(ts.solo);
            if (auto volPlugin = track->getVolumePlugin()) {
                volPlugin->setVolumeDb(ts.volume);
                volPlugin->setPan(ts.pan);
            }
        }

        // Ensure AuxReturnPlugin
        bool foundReturn = false;
        for (auto* p : track->pluginList.getPlugins()) {
            if (auto* rp = dynamic_cast<te::AuxReturnPlugin*>(p)) {
                if (rp->busNumber == r) { foundReturn = true; break; }
            }
        }
        if (!foundReturn) {
            if (auto plugin = edit.getPluginCache().createNewPlugin(te::AuxReturnPlugin::xmlTypeName, {})) {
                track->pluginList.insertPlugin(*plugin, 0, nullptr);
                auto* returnPlugin = dynamic_cast<te::AuxReturnPlugin*>(plugin.get());
                returnPlugin->busNumber = r;
            }
        }

        // Add specific fixed FX if missing (User Request)
        // Return 1: Long Hall (ValhallaRoom)
        // Return 2: Short Plate (ValhallaRoom)
        // Return 3: Tube Delay (Tube Delay)
        // Return 4: Overdrive (Dist TUBE-CULTURE)
        bool hasExternalFX = false;
        for (auto* p : track->pluginList.getPlugins()) {
            if (dynamic_cast<te::ExternalPlugin*>(p) != nullptr) {
                hasExternalFX = true;
                break;
            }
        }
        
        if (!hasExternalFX) {
            PluginInfo fxToAdd;
            if (r == 0) fxToAdd = REVERB_VALHALLA;
            else if (r == 1) fxToAdd = REVERB_VALHALLA; // Different preset loaded later maybe
            else if (r == 2) fxToAdd = DELAY_TUBE;
            else if (r == 3) fxToAdd = DIST_CULTURE;
            
            if (fxToAdd.pluginId.isNotEmpty()) {
                if (auto foundDesc = findPluginByName(engine, fxToAdd.pluginName)) {
                    if (auto fxPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc)) {
                        track->pluginList.insertPlugin(*fxPlugin, -1, nullptr); 
                        DBG("BirdLoader: Added Return FX '" + foundDesc->name + "' to 'Return " + juce::String(r + 1) + "'");
                    }
                }
            }
        }
    }

    // --- Create/Configure Master Track ---
    if (auto* master = edit.getMasterTrack()) {
        const std::vector<PluginInfo> masterFX = { WEISS_DS1, CONSOLE_1 };
        
        for (const auto& fx : masterFX) {
            bool found = false;
            for (auto* p : master->pluginList.getPlugins()) {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p)) {
                    if (ext->getName().containsIgnoreCase(fx.pluginName)) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found && fx.pluginId.isNotEmpty()) {
                if (auto foundDesc = findPluginByName(engine, fx.pluginName)) {
                    if (auto fxPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc)) {
                        master->pluginList.insertPlugin(*fxPlugin, -1, nullptr);
                        DBG("BirdLoader: Added Master FX '" + foundDesc->name + "'");
                    }
                }
            }
        }
    }

    DBG("BirdLoader: Loaded " + juce::String(static_cast<int>(result.channels.size())) +
        " tracks, " + juce::String(result.bars) + " bar loop");
}

// --- Serialize track notes as JSON for the UI ---

juce::String BirdLoader::getTrackNotesJSON(te::Edit& edit, const BirdParseResult* parseResult) {
    auto tracks = te::getAudioTracks(edit);
    juce::String json = "{\"tracks\":[";

    int trackCount = 0;
    for (int t = 0; t <= tracks.size(); t++) {
        te::Track* track = nullptr;
        bool isMaster = (t == tracks.size());
        
        if (isMaster) {
            track = edit.getMasterTrack();
        } else {
            track = tracks[t];
        }

        if (!track) continue;

        // Skip the master track in the regular loop — it's handled separately at the end
        if (!isMaster && dynamic_cast<te::MasterTrack*>(track)) continue;

        if (trackCount > 0) json += ",";
        trackCount++;

        // For return/master tracks: read the actual ExternalPlugins from the plugin list directly
        bool isReturn = !isMaster && track->getName().startsWith("Return ");

        // Look up plugin info from parse result (regular tracks only)
        juce::String pluginField;
        juce::String fxField;
        juce::String channelStripField;
        
        if (!isMaster && !isReturn && parseResult && t < static_cast<int>(parseResult->channels.size())) {
            auto& parsedCh = parseResult->channels[t];
            
            auto info = pluginFromKeyword(parsedCh.plugin);
            if (info.pluginId.isNotEmpty()) {
                pluginField = ",\"plugin\":{\"pluginId\":" + juce::JSON::toString(info.pluginId) +
                              ",\"pluginName\":" + juce::JSON::toString(info.pluginName) + "}";
            }
            
            auto fxInfo = pluginFromKeyword(parsedCh.fx);
            if (fxInfo.pluginId.isNotEmpty()) {
                fxField = ",\"fx\":{\"pluginId\":" + juce::JSON::toString(fxInfo.pluginId) +
                          ",\"pluginName\":" + juce::JSON::toString(fxInfo.pluginName) + "}";
            }

            auto stripInfo = pluginFromKeyword(parsedCh.strip);
            if (stripInfo.pluginId.isNotEmpty()) {
                channelStripField = ",\"channelStrip\":{\"pluginId\":" + juce::JSON::toString(stripInfo.pluginId) +
                                    ",\"pluginName\":" + juce::JSON::toString(stripInfo.pluginName) + "}";
            }
        }

        if (isReturn || isMaster) {
            juce::Array<te::ExternalPlugin*> extPlugins;
            for (auto* p : track->pluginList.getPlugins()) {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p))
                    extPlugins.add(ext);
            }
            if (extPlugins.size() >= 1) {
                auto name = extPlugins[0]->getName();
                fxField = juce::String(",\"fx\":{\"pluginId\":\"\",\"pluginName\":") + juce::JSON::toString(name) + "}";
            }
            if (extPlugins.size() >= 2) {
                auto name = extPlugins[1]->getName();
                channelStripField = juce::String(",\"channelStrip\":{\"pluginId\":\"\",\"pluginName\":") + juce::JSON::toString(name) + "}";
            }
        }

        juce::String sendsJson = "[0.0, 0.0, 0.0, 0.0]";
        if (!isReturn && !isMaster) {
            juce::Array<float> sendVals = { 0.0f, 0.0f, 0.0f, 0.0f };
            for (auto* p : track->pluginList.getPlugins()) {
                if (auto* send = dynamic_cast<te::AuxSendPlugin*>(p)) {
                    if (send->busNumber >= 0 && send->busNumber < 4) {
                        sendVals.set(send->busNumber, juce::Decibels::decibelsToGain(send->getGainDb()));
                    }
                }
            }
            sendsJson = "[" + juce::String(sendVals[0], 2) + "," + 
                              juce::String(sendVals[1], 2) + "," + 
                              juce::String(sendVals[2], 2) + "," + 
                              juce::String(sendVals[3], 2) + "]";
        }

        // Determine the track type to emit
        // Priority: parse result trackType > Lyria plugin detection > default "midi"
        juce::String trackTypeStr = "midi"; // default for BirdLoader-owned tracks
        if (!isMaster && !isReturn && parseResult && t < static_cast<int>(parseResult->channels.size())) {
            auto& parsedCh = parseResult->channels[t];
            if (!parsedCh.trackType.empty())
                trackTypeStr = juce::String(parsedCh.trackType);
        } else if (isReturn) {
            trackTypeStr = "midi"; // returns are always midi
        }
        // If a LyriaPlugin is present on this track, override to gen-audio
        if (!isMaster && !isReturn && track) {
            for (auto* p : track->pluginList.getPlugins()) {
                if (dynamic_cast<magenta::LyriaPlugin*>(p)) {
                    trackTypeStr = "audio";
                    break;
                }
            }
        }

        json += "{\"id\":" + juce::String(t) +
                ",\"name\":" + juce::JSON::toString(isMaster ? "Master" : track->getName()) +
                ",\"trackType\":" + juce::JSON::toString(trackTypeStr) +
                ",\"isReturn\":" + (isReturn ? "true" : "false") +
                ",\"isMaster\":" + (isMaster ? "true" : "false") +
                ",\"sends\":" + sendsJson +
                pluginField + fxField + channelStripField +
                ",\"notes\":[";

        if (!isMaster) {
            if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(track)) {
                auto clips = audioTrack->getClips();
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
            }
        }
        json += "],\"automation\":[";

        if (!isMaster && parseResult && t < static_cast<int>(parseResult->channels.size())) {
            auto& parsedCh = parseResult->channels[t];
            
            // Collect all step automation into curves by macro
            std::map<juce::String, std::vector<BirdAutomationPoint>> stepCurvesByMacro;
            for (const auto& note : parsedCh.notes) {
                for (const auto& [macro, value] : note.stepParams) {
                    BirdAutomationPoint pt;
                    pt.time = note.beatPos;
                    pt.value = value;
                    pt.shape = BirdAutomationPoint::Step;
                    stepCurvesByMacro[macro].push_back(pt);
                }
            }
            
            int macroCount = 0;
            // 1. Continuous section curves
            for (const auto& [macro, curve] : parsedCh.automation) {
                if (macroCount > 0) json += ",";
                json += "{\"macro\":" + juce::JSON::toString(juce::String(macro)) + ",\"points\":[";
                for (size_t i = 0; i < curve.points.size(); ++i) {
                    if (i > 0) json += ",";
                    auto& pt = curve.points[i];
                    json += "{\"time\":" + juce::String(pt.time, 3) + 
                            ",\"value\":" + juce::String(pt.value, 3) + 
                            ",\"shape\":" + juce::String(pt.shape) + "}";
                }
                json += "]}";
                macroCount++;
            }
            // 2. Step curves
            for (const auto& [macro, points] : stepCurvesByMacro) {
                // Skip if this macro was already written as a continuous curve
                if (parsedCh.automation.find(macro.toStdString()) != parsedCh.automation.end()) continue;
                
                if (macroCount > 0) json += ",";
                json += "{\"macro\":" + juce::JSON::toString(macro) + ",\"points\":[";
                for (size_t i = 0; i < points.size(); ++i) {
                    if (i > 0) json += ",";
                    auto& pt = points[i];
                    json += "{\"time\":" + juce::String(pt.time, 3) + 
                            ",\"value\":" + juce::String(pt.value, 3) + 
                            ",\"shape\":" + juce::String(pt.shape) + "}";
                }
                json += "]}";
                macroCount++;
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

    json += "],\"totalBars\":" + juce::String(parseResult ? parseResult->bars : 1);

    if (parseResult && parseResult->hasKeySignature) {
        json += ",\"keySignature\":" + juce::JSON::toString(juce::String(parseResult->keyName));
    }

    json += "}";
    return json;
}
