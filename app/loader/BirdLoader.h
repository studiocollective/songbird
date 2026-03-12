#pragma once

#include <string>
#include <vector>
#include <map>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

// --- Automation curve data ---
struct BirdAutomationPoint {
    double time;        // position in beats (relative to section start for continuous, or relative to note for steps)
    float value;        // 0.0 to 1.0 (normalized)
    
    // Curve shape to the *next* point
    enum Shape { Step, Linear, Exponential, Logarithmic, Smooth };
    Shape shape = Linear;
};

struct BirdAutomationCurve {
    std::string macroName;
    std::vector<BirdAutomationPoint> points;
};

// --- Resolved note data from a .bird file ---
struct BirdNote {
    int pitch;          // MIDI note number
    double beatPos;     // position in beats (0-based)
    double duration;    // duration in beats
    int velocity;       // 0–127
    
    // Step-based automation tied to this specific note
    std::map<std::string, float> stepParams; 
};

// --- Resolved audio clip data from a .bird file ---
struct BirdClip {
    std::string filePath;
    double beatPos;      // position in beats (0-based)
    double duration;     // duration in beats
    double offsetBeats = 0.0; // offset into the source file
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;
};

// --- A parsed channel from a .bird file ---
struct BirdChannel {
    int channel;                 // MIDI channel (0-based)
    std::string name;            // channel name (e.g. "bass", "drums")
    std::string trackType;       // "midi" | "audio" (default: "midi")
    std::string plugin;          // plugin keyword (e.g. "synths", "kick", "drums", "bass")
    std::string fx;              // fx keyword (e.g. "delay", "reverb")
    std::string strip;           // channel strip keyword (e.g. "console1")
    std::vector<BirdNote> notes; // resolved notes
    std::vector<BirdClip> clips; // resolved audio clips
    double patternBeats = 0;     // original pattern length in beats (before expansion)
    
    // Continuous automation curves attached to this channel (section-level)
    std::map<std::string, BirdAutomationCurve> automation;
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
    int bpm = 0;                                     // project BPM (0 = not specified, use default 120)
    std::string scaleRoot;                           // scale root note (e.g. "C", "F#") — empty if not set
    std::string scaleMode;                           // scale mode (e.g. "ionian", "dorian") — empty if not set
    int keySharpsFlats = 0;                          // key signature: >0 for sharps, <0 for flats
    bool keyIsMinor = false;                         // true if key signature is minor
    std::string keyName = "";                        // Original key string (e.g. "C min")
    bool hasKeySignature = false;                    // true if a key signature was explicitly parsed
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
    static void populateEdit(te::Edit& edit, const BirdParseResult& result, te::Engine& engine, std::function<void(const juce::String&, float)> progressCallback = nullptr);

    // Serialize all track note data from an Edit as a JSON string
    static juce::String getTrackStateJSON(te::Edit& edit, const BirdParseResult* parseResult = nullptr);

    struct PatternState {
        size_t patIdx = 0;
        size_t noteIdx = 0;
        size_t velIdx = 0;
        int ticksInStep = 0; // Ticks already elapsed in the current pattern step
    };

    // Resolve pattern + note groups + velocities + timing into concrete BirdNote events
    // Modifies state to allow pattern phase to continue across sections
    static std::vector<BirdNote> resolveNotes(
        const std::vector<int>& pattern,
        const std::vector<std::vector<int>>& noteGroups,
        const std::vector<int>& velocities,
        const std::map<std::string, std::vector<std::string>>& stepAutomations,
        const std::vector<int>& timingOffsets,
        int swingPercent,
        int humanizeTicks,
        int sequenceLength,
        PatternState& state);

private:
    // Internal parsing helpers
    static std::vector<std::string> splitLine(const std::string& line);
    static std::vector<int> parsePattern(const std::vector<std::string>& tokens, int& lastDur);
    static std::vector<int> parseVelocities(const std::vector<std::string>& tokens, int& lastVel);
    static std::vector<std::vector<int>> parseNotes(const std::vector<std::string>& tokens, int& lastNote);
};
