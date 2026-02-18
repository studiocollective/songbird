#ifndef VOICES_MIX_PRESETS
#define VOICES_MIX_PRESETS

#include <string>
using std::string;

// =============================================================================
// Console 1 Core Mixing Suite — CC Assignments
// =============================================================================
// These map to the parameter sections of the Softube Console 1 Core Mixing 
// Suite plugin. Console 1 uses MIDI Learn, so you assign these CCs to
// the corresponding knobs once, then Bird notation drives them.
//
// CCs 20-31 and 85-102 are "undefined" in the MIDI spec, safe for custom use.
// Standard MIDI CCs are used for volume (7) and pan (10).
// =============================================================================

// ---- Input / Preamp ----
static const int MIX_INPUT_GAIN   = 20;   // Input gain
static const int MIX_PHASE        = 21;   // Phase invert (0 or 127)

// ---- Filters ----
static const int MIX_FILTER_LOW   = 22;   // Low cut frequency
static const int MIX_FILTER_HIGH  = 23;   // High cut frequency

// ---- Shape (Transient Shaper / Gate) ----
static const int MIX_SHAPE_GATE   = 24;   // Gate threshold
static const int MIX_SHAPE_SUSTAIN= 25;   // Sustain (transient shaper)
static const int MIX_SHAPE_PUNCH  = 26;   // Punch / attack (transient shaper)
static const int MIX_SHAPE_HARD_GATE = 27; // Hard gate on/off (0 or 127)

// ---- Equalizer ----
static const int MIX_EQ_LOW_GAIN  = 28;   // Low band gain
static const int MIX_EQ_LOW_FREQ  = 29;   // Low band frequency
static const int MIX_EQ_LO_MID_GAIN = 30; // Low-mid band gain
static const int MIX_EQ_LO_MID_FREQ = 31; // Low-mid band frequency
static const int MIX_EQ_LO_MID_Q = 85;    // Low-mid band Q/width
static const int MIX_EQ_HI_MID_GAIN = 86; // High-mid band gain
static const int MIX_EQ_HI_MID_FREQ = 87; // High-mid band frequency
static const int MIX_EQ_HI_MID_Q = 88;    // High-mid band Q/width
static const int MIX_EQ_HIGH_GAIN = 89;   // High band gain
static const int MIX_EQ_HIGH_FREQ = 90;   // High band frequency

// ---- Compressor ----
static const int MIX_COMP_THRESH  = 91;   // Compressor threshold
static const int MIX_COMP_RATIO   = 92;   // Compressor ratio
static const int MIX_COMP_ATTACK  = 93;   // Compressor attack
static const int MIX_COMP_RELEASE = 94;   // Compressor release
static const int MIX_COMP_MAKEUP  = 95;   // Compressor makeup gain
static const int MIX_COMP_MIX     = 96;   // Parallel / dry-wet mix
static const int MIX_COMP_SIDECHAIN = 97;  // Sidechain amount / ext key

// ---- Drive / Saturation ----
static const int MIX_DRIVE_AMOUNT = 98;   // Drive / saturation amount
static const int MIX_DRIVE_CHARACTER = 99; // Drive character / type

// ---- Output ----
static const int MIX_VOLUME       = 7;    // Standard MIDI volume
static const int MIX_PAN          = 10;   // Standard MIDI pan
static const int MIX_WIDTH        = 100;  // Stereo width
static const int MIX_SEND_1       = 101;  // Send 1 (reverb)
static const int MIX_SEND_2       = 102;  // Send 2 (delay)
static const int MIX_MUTE         = 103;  // Mute (0 or 127)
static const int MIX_SOLO         = 104;  // Solo (0 or 127)


// Name-to-CC lookup for the `mix` token in bird notation
static int mix_cc_from_name(string name) {

    // Input / Preamp
    if (name == "input" || name == "gain" || name == "input_gain")  return MIX_INPUT_GAIN;
    if (name == "phase")                                            return MIX_PHASE;

    // Filters
    if (name == "low_cut" || name == "lc" || name == "hpf")         return MIX_FILTER_LOW;
    if (name == "high_cut" || name == "hc" || name == "lpf")        return MIX_FILTER_HIGH;

    // Shape (Transient / Gate)
    if (name == "gate")                                             return MIX_SHAPE_GATE;
    if (name == "sustain" || name == "shape_sustain")                return MIX_SHAPE_SUSTAIN;
    if (name == "punch" || name == "shape_punch" || name == "transient") return MIX_SHAPE_PUNCH;
    if (name == "hard_gate")                                        return MIX_SHAPE_HARD_GATE;

    // EQ
    if (name == "eq_low" || name == "low")                          return MIX_EQ_LOW_GAIN;
    if (name == "eq_low_freq" || name == "low_freq")                return MIX_EQ_LOW_FREQ;
    if (name == "eq_lo_mid" || name == "lo_mid")                    return MIX_EQ_LO_MID_GAIN;
    if (name == "eq_lo_mid_freq" || name == "lo_mid_freq")          return MIX_EQ_LO_MID_FREQ;
    if (name == "eq_lo_mid_q" || name == "lo_mid_q")                return MIX_EQ_LO_MID_Q;
    if (name == "eq_hi_mid" || name == "hi_mid")                    return MIX_EQ_HI_MID_GAIN;
    if (name == "eq_hi_mid_freq" || name == "hi_mid_freq")          return MIX_EQ_HI_MID_FREQ;
    if (name == "eq_hi_mid_q" || name == "hi_mid_q")                return MIX_EQ_HI_MID_Q;
    if (name == "eq_high" || name == "high")                        return MIX_EQ_HIGH_GAIN;
    if (name == "eq_high_freq" || name == "high_freq")              return MIX_EQ_HIGH_FREQ;

    // Compressor
    if (name == "comp" || name == "threshold" || name == "comp_threshold") return MIX_COMP_THRESH;
    if (name == "ratio" || name == "comp_ratio")                    return MIX_COMP_RATIO;
    if (name == "comp_attack")                                      return MIX_COMP_ATTACK;
    if (name == "comp_release")                                     return MIX_COMP_RELEASE;
    if (name == "makeup" || name == "comp_makeup")                  return MIX_COMP_MAKEUP;
    if (name == "comp_mix" || name == "parallel")                   return MIX_COMP_MIX;
    if (name == "sidechain" || name == "sc")                        return MIX_COMP_SIDECHAIN;

    // Drive
    if (name == "drive" || name == "saturation" || name == "sat")   return MIX_DRIVE_AMOUNT;
    if (name == "drive_character" || name == "character")            return MIX_DRIVE_CHARACTER;

    // Output
    if (name == "volume" || name == "vol")                          return MIX_VOLUME;
    if (name == "pan")                                              return MIX_PAN;
    if (name == "width" || name == "stereo")                        return MIX_WIDTH;
    if (name == "send1" || name == "reverb")                        return MIX_SEND_1;
    if (name == "send2" || name == "delay")                         return MIX_SEND_2;
    if (name == "mute")                                             return MIX_MUTE;
    if (name == "solo")                                             return MIX_SOLO;

    return -1; // Unknown name
}

#endif // VOICES_MIX_PRESETS
