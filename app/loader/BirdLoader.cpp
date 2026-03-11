#include "BirdLoader.h"
#include "BirdParser.h"
#include "BirdPopulator.h"
#include "BirdSerializer.h"
#include "PluginRegistry.h"
#include "libraries/theory/note_parser.h"
#include "libraries/sequencing/utils/time_constants.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

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
    bool inChannelsBlock = false;
    bool inSigBlock = false;

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
        // --- Sig block (signature: bpm, scale, key) ---
        if (tokens[0] == "sig") {
            inSigBlock = true;
            inArrangement = false;
            inChannelsBlock = false;
            continue;
        }

        // If we're inside a sig block, read indented entries
        if (inSigBlock) {
            if (rawLine.size() > 0 && (rawLine[0] == ' ' || rawLine[0] == '\t')) {
                if (tokens[0] == "bpm" && tokens.size() > 1) {
                    try { result.bpm = std::stoi(tokens[1]); } catch (...) {}
                } else if (tokens[0] == "scale" && tokens.size() > 2) {
                    result.scaleRoot = tokens[1];
                    std::string mode = tokens[2];
                    if (mode == "major") mode = "ionian";
                    else if (mode == "minor") mode = "aeolian";
                    result.scaleMode = mode;
                } else if (tokens[0] == "key" && tokens.size() > 1) {
                    std::string keyString = tokens[1];
                    for (size_t i = 2; i < tokens.size(); ++i)
                        keyString += " " + tokens[i];
                    bool isMinor = false;
                    int sharps = sharps_from_key_name(keyString, isMinor);
                    if (sharps != -99) {
                        result.keySharpsFlats = sharps;
                        result.keyIsMinor = isMinor;
                        result.keyName = keyString;
                        result.hasKeySignature = true;
                    }
                }
                continue;
            } else {
                inSigBlock = false;
                // Fall through to process this line normally
            }
        }

        if (tokens[0] == "arr") {
            flushSection();
            inArrangement = true;
            inChannelsBlock = false;
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
            inChannelsBlock = false;
            currentSectionName = (tokens.size() > 1) ? tokens[1] : "unnamed";
            continue;
        }

        // --- Channels block ---
        if (tokens[0] == "channels") {
            flushChannel();
            inChannelsBlock = true;
            continue;
        }

        // If we're inside a channels block, read indented channel entries
        if (inChannelsBlock) {
            if (rawLine.size() > 0 && (rawLine[0] == ' ' || rawLine[0] == '\t')) {
                // Check if this is a new channel definition (first token is a number)
                bool isNumber = !tokens[0].empty() && std::all_of(tokens[0].begin(), tokens[0].end(), ::isdigit);
                if (isNumber) {
                    flushChannel();
                    inChannel = true;
                    try { currentUCh.channel = std::stoi(tokens[0]) - 1; } catch (...) {}
                    currentUCh.name = (tokens.size() > 1) ? tokens[1] : ("Track " + std::to_string(currentUCh.channel + 1));
                }
                // Otherwise it's a property line (plugin, strip, type, etc.) — fall through
                // to the normal property handlers below
                if (isNumber) continue;
            } else {
                // Not indented — channels block is over
                inChannelsBlock = false;
                flushChannel();
                // Fall through to process this line normally
            }
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

        // --- BPM ---
        if (tokens[0] == "bpm" && tokens.size() > 1) {
            try { result.bpm = std::stoi(tokens[1]); } catch (...) {}
            continue;
        }

        // --- Scale ---
        // Syntax: scale <root> <mode>   e.g. "scale C ionian", "scale F# dorian"
        // Also accepts: "scale C major" (→ ionian), "scale A minor" (→ aeolian)
        if (tokens[0] == "scale" && tokens.size() > 2) {
            result.scaleRoot = tokens[1];
            std::string mode = tokens[2];
            // Normalize aliases
            if (mode == "major") mode = "ionian";
            else if (mode == "minor") mode = "aeolian";
            result.scaleMode = mode;
            continue;
        }

        // --- Channel (backward compat: bare `ch` at top level) ---
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
        // Apply trackType from global configs
        for (auto& [chIdx, globalCh] : globalConfigs) {
            auto orderIt = channelOrder.find(globalCh.name);
            if (orderIt != channelOrder.end()) {
                result.channels[orderIt->second].trackType = globalCh.trackType;
            }
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
                    if (mergedCh.trackType == "midi" && globalCh.trackType != "midi")
                        mergedCh.trackType = globalCh.trackType;
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

                // Copy plugin/fx/strip/trackType if not yet set
                if (targetCh.plugin.empty())
                    targetCh.plugin = resolved.plugin;
                if (targetCh.fx.empty())
                    targetCh.fx = resolved.fx;
                if (targetCh.strip.empty())
                    targetCh.strip = resolved.strip;
                if (targetCh.trackType.empty() || (targetCh.trackType == "midi" && resolved.trackType != "midi"))
                    targetCh.trackType = resolved.trackType;

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

// --- Delegate to extracted modules ---

void BirdLoader::populateEdit(te::Edit& edit, const BirdParseResult& result, te::Engine& engine,
                              std::function<void(const juce::String&, float)> progressCallback) {
    ::populateEdit(edit, result, engine, progressCallback);
}

juce::String BirdLoader::getTrackStateJSON(te::Edit& edit, const BirdParseResult* parseResult) {
    return ::getTrackStateJSON(edit, parseResult);
}
