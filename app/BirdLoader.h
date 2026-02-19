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
    std::vector<BirdNote> notes; // resolved notes
};

// --- Parse result ---
struct BirdParseResult {
    int bars = 1;                        // cycle length in bars
    std::vector<BirdChannel> channels;   // parsed channels
    std::string error;                   // non-empty if parse failed
};

class BirdLoader {
public:
    // Parse a .bird file and return resolved note data
    static BirdParseResult parse(const std::string& filePath);

    // Populate a te::Edit from parsed bird data
    // Clears existing tracks and creates new ones from the parse result
    static void populateEdit(te::Edit& edit, const BirdParseResult& result);

    // Serialize all track note data from an Edit as a JSON string
    // Format: [ { "id": 0, "name": "bass", "notes": [ { "pitch": 36, "beat": 0, "duration": 1, "velocity": 80 }, ... ] }, ... ]
    static juce::String getTrackNotesJSON(te::Edit& edit);

private:
    // Internal parsing helpers
    static std::vector<std::string> splitLine(const std::string& line);
    static std::vector<int> parsePattern(const std::vector<std::string>& tokens, int& lastDur);
    static std::vector<int> parseVelocities(const std::vector<std::string>& tokens, int& lastVel);
    static std::vector<std::vector<int>> parseNotes(const std::vector<std::string>& tokens, int& lastNote);
    static std::vector<BirdNote> resolveNotes(
        const std::vector<int>& pattern,
        const std::vector<std::vector<int>>& noteGroups,
        const std::vector<int>& velocities,
        int sequenceLength);
};
