#include "MidiToBird.h"
#include "libraries/sequencing/utils/time_constants.h"
#include "libraries/theory/note_parser.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

// --- Internal helpers ---

namespace {

// Convert beat position (Tracktion uses beats, 1 beat = 1 quarter) to ticks
static int beatsToTicks(double beats) {
    // 1 quarter note = 96 ticks (TICKS_PER_WHOLE_NOTE / 4 = 384 / 4 = 96)
    return static_cast<int>(std::round(beats * 96.0));
}

// Find the closest grid slot for a tick position
static int quantizeToGrid(int tick, int gridTicks) {
    return static_cast<int>(std::round(static_cast<double>(tick) / gridTicks)) * gridTicks;
}

// Convert a MIDI pitch to a note name string (e.g. 60 → "C4")
static std::string pitchToName(int pitch) {
    static const char* noteNames[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    int octave = (pitch / 12) - 1;
    int note = pitch % 12;
    return std::string(noteNames[note]) + std::to_string(octave);
}

// Find the best Bird duration token for a given tick count
static std::string ticksToDurToken(int ticks) {
    if (ticks <= 0) return "x";
    
    // Match exact durations to tokens
    if (ticks >= dur::w)   return "w";
    if (ticks >= dur::qq)  return "qq";
    if (ticks >= dur::q)   return "q";
    if (ticks >= dur::xx)  return "xx";
    return "x";  // 16th note (24 ticks)
}

struct NoteEvent {
    int pitch;
    int velocity;
    int startTick;      // absolute tick position
    int durationTicks;  // note duration in ticks
    int gridSlot;       // which grid slot this quantizes to
    int timingOffset;   // startTick - quantizedTick (for t line)
};

} // anonymous namespace

// --- Main conversion function ---

MidiToBirdResult convertMidiToBird(
    const juce::MidiMessageSequence& midi,
    int bpm,
    int gridTicks,
    int maxBars)
{
    MidiToBirdResult result;
    result.gridTicksUsed = gridTicks;

    // 1. Extract note events from MIDI sequence
    std::vector<NoteEvent> events;
    
    for (int i = 0; i < midi.getNumEvents(); ++i) {
        auto* event = midi.getEventPointer(i);
        if (!event || !event->message.isNoteOn())
            continue;
        
        NoteEvent ne;
        ne.pitch = event->message.getNoteNumber();
        ne.velocity = event->message.getVelocity();
        ne.startTick = beatsToTicks(event->message.getTimeStamp());
        
        // Find matching note-off for duration
        auto* offEvent = event->noteOffObject;
        if (offEvent) {
            ne.durationTicks = beatsToTicks(offEvent->message.getTimeStamp()) - ne.startTick;
        } else {
            ne.durationTicks = gridTicks; // default to one grid slot
        }
        
        // Quantize to grid
        int quantized = quantizeToGrid(ne.startTick, gridTicks);
        ne.gridSlot = quantized / gridTicks;
        ne.timingOffset = ne.startTick - quantized;
        
        events.push_back(ne);
    }
    
    if (events.empty()) {
        result.birdText = "  p _\n  n 60\n  v 80\n";
        result.detectedBars = 1;
        return result;
    }
    
    // 2. Determine total range
    int maxGridSlot = 0;
    for (auto& e : events) {
        maxGridSlot = std::max(maxGridSlot, e.gridSlot);
    }
    
    int slotsPerBar = TICKS_PER_BAR / gridTicks;
    int detectedBars = (maxGridSlot / slotsPerBar) + 1;
    
    if (maxBars > 0 && detectedBars > maxBars)
        detectedBars = maxBars;
    
    int totalSlots = detectedBars * slotsPerBar;
    result.detectedBars = detectedBars;
    
    // 3. Build grid: each slot may have one or more notes (chords)
    struct SlotData {
        std::vector<int> pitches;
        int velocity = 0;
        int timingOffset = 0;
        bool hasNote = false;
    };
    
    std::vector<SlotData> grid(totalSlots);
    
    for (auto& e : events) {
        if (e.gridSlot < 0 || e.gridSlot >= totalSlots)
            continue;
        
        auto& slot = grid[e.gridSlot];
        slot.pitches.push_back(e.pitch);
        // Use the highest velocity if multiple notes on same slot
        slot.velocity = std::max(slot.velocity, e.velocity);
        // Average timing offset for chords
        if (!slot.hasNote) {
            slot.timingOffset = e.timingOffset;
        } else {
            slot.timingOffset = (slot.timingOffset + e.timingOffset) / 2;
        }
        slot.hasNote = true;
    }
    
    // 4. Determine the grid token (what kind of grid are we using?)
    std::string gridToken;
    if (gridTicks == dur::x) gridToken = "x";
    else if (gridTicks == dur::xx) gridToken = "xx";
    else if (gridTicks == dur::q) gridToken = "q";
    else if (gridTicks == dur::qq) gridToken = "qq";
    else gridToken = "x"; // fallback to 16ths
    
    // 5. Build Bird notation lines
    std::ostringstream pLine, vLine, tLine;
    std::vector<std::string> nLines; // may need multiple n lines for polyphony
    
    pLine << "  p";
    vLine << "  v";
    tLine << "  t";
    
    // Track if all timing offsets are 0 (skip t line if so)
    bool hasTimingOffsets = false;
    
    // For single-voice: one n line with all pitches
    std::ostringstream nLine;
    nLine << "  n";
    
    int lastPitch = -1;
    int lastVel = -1;
    
    for (int s = 0; s < totalSlots; ++s) {
        auto& slot = grid[s];
        
        if (slot.hasNote) {
            pLine << " " << gridToken;
            
            // Velocity: use - for repeat
            if (slot.velocity == lastVel) {
                vLine << " -";
            } else {
                vLine << " " << slot.velocity;
                lastVel = slot.velocity;
            }
            
            // Notes: use - for repeat, note name for new
            if (slot.pitches.size() == 1) {
                if (slot.pitches[0] == lastPitch) {
                    nLine << " -";
                } else {
                    nLine << " " << pitchToName(slot.pitches[0]);
                    lastPitch = slot.pitches[0];
                }
            } else {
                // Chord: space-separated pitches on one n line entry
                // Bird handles multiple n lines for chords, so emit first pitch here
                // and note that chords should be multiple n lines
                nLine << " " << pitchToName(slot.pitches[0]);
                lastPitch = slot.pitches[0];
                // TODO: handle polyphony with additional n lines
            }
            
            // Timing offset
            if (slot.timingOffset != 0) {
                hasTimingOffsets = true;
                tLine << " " << (slot.timingOffset > 0 ? "+" : "") << slot.timingOffset;
            } else {
                tLine << " 0";
            }
        } else {
            // Rest
            pLine << " _";
        }
    }
    
    // 6. Build final output
    std::ostringstream out;
    out << pLine.str() << "\n";
    out << vLine.str() << "\n";
    out << nLine.str() << "\n";
    
    if (hasTimingOffsets) {
        out << tLine.str() << "\n";
    }
    
    result.birdText = out.str();
    return result;
}
