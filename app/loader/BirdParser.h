#pragma once

#include "BirdLoader.h"

#include <string>
#include <vector>
#include <map>

// --- Tick-to-beat conversion ---
// TICKS_PER_WHOLE_NOTE = 384, one whole note = 4 beats, so 1 beat = 96 ticks
static constexpr double TICKS_PER_BEAT = 96.0;

double ticksToBeats(int ticks);

// --- String helpers ---
std::string ltrim(const std::string& s);

// --- Unresolved (pre-resolution) layer/channel structs ---
// Used during parsing before bar counts are known

struct UnresolvedLayer {
    std::vector<int> pattern;
    std::vector<std::vector<int>> noteGroups;
    std::vector<int> velocities;
    std::map<std::string, std::vector<std::string>> stepAutomations;
    std::vector<int> timingOffsets;  // per-note tick offsets (t line)
    int swingPercent = 50;           // 50 = straight, 67 = triplet swing
    int humanizeTicks = 0;           // random ±ticks jitter
};

struct UnresolvedClip {
    std::string filePath;
    double beatPos = 0.0;
    double duration = 0.0;
    double offsetBeats = 0.0;
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;
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
    std::vector<UnresolvedClip> clips;
    std::map<std::string, BirdAutomationCurve> automation;
};

// --- Channel resolution ---
// Returns a pair: {Resolved BirdChannel, new PatternState for the next section}
std::pair<BirdChannel, BirdLoader::PatternState> resolveChannel(
    const UnresolvedChannel& uch, int bars, BirdLoader::PatternState state = {});
