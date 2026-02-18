#ifndef VOICES_CC_PRESETS
#define VOICES_CC_PRESETS

#include <string>
#include <map>
using std::string;
using std::map;

// Standard MIDI CC numbers
static const int CC_MOD_WHEEL   = 1;
static const int CC_BREATH      = 2;
static const int CC_VOLUME      = 7;
static const int CC_PAN         = 10;
static const int CC_EXPRESSION  = 11;
static const int CC_SUSTAIN     = 64;
static const int CC_RESONANCE   = 71;
static const int CC_RELEASE     = 72;
static const int CC_ATTACK      = 73;
static const int CC_CUTOFF      = 74;
static const int CC_DECAY       = 75;

// Arturia Analog Lab Macros (CC 12-19)
static const int CC_MACRO_1     = 12;   // Typically "brightness" or filter
static const int CC_MACRO_2     = 13;
static const int CC_MACRO_3     = 14;
static const int CC_MACRO_4     = 15;
static const int CC_MACRO_5     = 16;
static const int CC_MACRO_6     = 17;
static const int CC_MACRO_7     = 18;
static const int CC_MACRO_8     = 19;

// Name-to-CC lookup for the `var` token in bird notation
static int cc_from_name(string name) {
    // Standard MIDI
    if (name == "mod" || name == "modwheel")    return CC_MOD_WHEEL;
    if (name == "breath")                       return CC_BREATH;
    if (name == "volume" || name == "vol")       return CC_VOLUME;
    if (name == "pan")                          return CC_PAN;
    if (name == "expression" || name == "expr")  return CC_EXPRESSION;
    if (name == "sustain")                      return CC_SUSTAIN;
    if (name == "cutoff" || name == "filter")    return CC_CUTOFF;
    if (name == "resonance" || name == "reso")   return CC_RESONANCE;
    if (name == "attack" || name == "att")       return CC_ATTACK;
    if (name == "release" || name == "rel")      return CC_RELEASE;
    if (name == "decay")                        return CC_DECAY;

    // Arturia Analog Lab macros
    if (name == "macro1" || name == "brightness") return CC_MACRO_1;
    if (name == "macro2")                        return CC_MACRO_2;
    if (name == "macro3")                        return CC_MACRO_3;
    if (name == "macro4")                        return CC_MACRO_4;
    if (name == "macro5")                        return CC_MACRO_5;
    if (name == "macro6")                        return CC_MACRO_6;
    if (name == "macro7")                        return CC_MACRO_7;
    if (name == "macro8")                        return CC_MACRO_8;

    return -1; // Unknown name
}

#endif // VOICES_CC_PRESETS
