#pragma once

#include <string>
#include <vector>
#include <map>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

// --- Resolved note data from a .bird file ---
struct BirdNote {
    int pitch;          // MIDI note number
    double beatPos;     // position in beats (0-based)
    double duration;    // duration in beats
    int velocity;       // 0–127
};

// --- A parsed channel from a .bird file ---
struct BirdChannel {
    int channel;                 // MIDI channel (0-based)
    std::string name;            // channel name (e.g. "bass", "drums")
    std::string plugin;          // plugin keyword (e.g. "synths", "kick", "drums", "bass")
    std::string fx;              // fx keyword (e.g. "delay", "reverb")
    std::string strip;           // channel strip keyword (e.g. "console1")
    std::vector<BirdNote> notes; // resolved notes
};

// --- A named section containing its own channels ---
struct BirdSection {
    std::string name;
    std::vector<BirdChannel> channels;
};

// --- An arrangement entry referencing a section by name ---
struct BirdArrangementEntry {
    std::string sectionName;
    int bars;
};

// --- Parse result ---
struct BirdParseResult {
    int bars = 1;                                    // cycle length in bars (total if arrangement exists)
    std::vector<BirdChannel> channels;               // top-level channels (no sections)
    std::vector<BirdSection> sections;               // named sections
    std::vector<BirdArrangementEntry> arrangement;   // arrangement order
    std::string error;                               // non-empty if parse failed
};

class BirdLoader {
public:
    // Parse a .bird file and return resolved note data
    static BirdParseResult parse(const std::string& filePath);

    // Populate a te::Edit from parsed bird data
    // Clears existing tracks and creates new ones from the parse result
    static void populateEdit(te::Edit& edit, const BirdParseResult& result, te::Engine& engine);

    // Serialize all track note data from an Edit as a JSON string
    static juce::String getTrackNotesJSON(te::Edit& edit, const BirdParseResult* parseResult = nullptr);

    struct PatternState {
        size_t patIdx = 0;
        size_t noteIdx = 0;
        size_t velIdx = 0;
        int ticksInStep = 0; // Ticks already elapsed in the current pattern step
    };

    // Resolve pattern + note groups + velocities into concrete BirdNote events
    // Modifies state to allow pattern phase to continue across sections
    static std::vector<BirdNote> resolveNotes(
        const std::vector<int>& pattern,
        const std::vector<std::vector<int>>& noteGroups,
        const std::vector<int>& velocities,
        int sequenceLength,
        PatternState& state);

private:
    // Internal parsing helpers
    static std::vector<std::string> splitLine(const std::string& line);
    static std::vector<int> parsePattern(const std::vector<std::string>& tokens, int& lastDur);
    static std::vector<int> parseVelocities(const std::vector<std::string>& tokens, int& lastVel);
    static std::vector<std::vector<int>> parseNotes(const std::vector<std::string>& tokens, int& lastNote);
};
