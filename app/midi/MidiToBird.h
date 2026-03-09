#pragma once

#include <string>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

// --- MIDI recording → Bird notation converter ---

struct MidiToBirdResult {
    std::string birdText;       // Bird notation lines (p, v, n, t, sw)
    int detectedBars;           // Number of bars in the output
    int gridTicksUsed;          // Grid resolution in ticks (e.g. 24 = 16th note)
};

/**
 * Convert a MIDI recording into Bird notation.
 *
 * Takes a MIDI message sequence and quantizes it to a grid, producing
 * Bird `p`, `v`, `n`, and `t` lines. The `t` line preserves the
 * micro-timing feel (difference between actual note position and grid).
 *
 * @param midi        MIDI messages to convert (note-on/off pairs)
 * @param bpm         Project BPM (used for tick → beat conversion)
 * @param gridTicks   Grid resolution in ticks. Default: 24 (16th note).
 *                    Other common values: 48 (8th), 96 (quarter).
 * @param maxBars     Maximum bars to output. 0 = auto-detect from content.
 * @return            Bird notation text and metadata
 */
MidiToBirdResult convertMidiToBird(
    const juce::MidiMessageSequence& midi,
    int bpm,
    int gridTicks = 24,
    int maxBars = 0
);
