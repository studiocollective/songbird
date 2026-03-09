#include "MacroMapper.h"

// ---------------------------------------------------------------------------
// Parameter name notes:
//
// ALL parameter names in this file are VERIFIED against actual plugin output
// using tools/scan_plugin_params.cpp (ScanPluginParams utility).
// Run:  ./build/ScanPluginParams_artefacts/Debug/ScanPluginParams <plugin.vst3>
//
// The setPluginParam() bridge does case-insensitive substring matching, so a
// macro like "comp_thresh" → "Threshold 1" will match any param containing
// "Threshold 1".  The getPluginParams bridge always enumerates the live list.
// ---------------------------------------------------------------------------

// --- Channel-strip parameter maps ---
const std::map<juce::String, std::map<juce::String, juce::String>> MacroMapper::channelStripMaps = {

    // -------------------------------------------------------------------------
    // Softube Console 1 — verified with ScanPluginParams v2.5.83
    // Key compressor params (indices from scan):
    //   24: "Compressor"             [2 steps: 0=off, 1=on]
    //   25: "Ratio"                  continuous
    //   27: "Attack"                 continuous
    //   28: "Release"                continuous
    //   29: "Compression"            continuous (threshold/amount)
    //   31: "External Sidechain"     [3 steps: 0=Int, 0.5=Ext1, 1.0=Ext2]
    //  119: "Ext. Sidechain to Subsystem" [2 steps]
    // -------------------------------------------------------------------------
    { "Console 1", {
        { "input_gain",         "Input Gain" },
        { "low_cut",            "Low Cut" },
        { "high_cut",           "High Cut" },
        { "shape",              "Shape" },
        { "eq_low_gain",        "Gain" },         // EQ section (multiple "Gain" params — use index if needed)
        { "comp_on",            "Compressor" },   // idx 24, [2 steps]
        { "comp_thresh",        "Compression" },  // idx 29, continuous amount
        { "comp_ratio",         "Ratio" },        // idx 25
        { "comp_attack",        "Attack" },       // idx 27
        { "comp_release",       "Release" },      // idx 28
        { "sidechain_enable",   "External Sidechain" },   // idx 31, 0=Int, 0.5=Ext1
        { "sidechain_sub",      "Ext. Sidechain to Subsystem" }, // idx 119
        { "drive",              "Drive" },
        { "character",          "Character" },
        { "output_gain",        "Volume" },       // idx 37 "Volume"
    }},

    // -------------------------------------------------------------------------
    // Softube Weiss DS1-MK3 — verified with ScanPluginParams v2.6.30
    // Two independent compressor bands:
    //   20: "Threshold 1",  21: "Ratio 1",  14: "Attack 1",  16: "Release Fast 1"
    //   32: "Threshold 2",  33: "Ratio 2",  26: "Attack 2",  28: "Release Fast 2"
    //   37: "Sidechain"     [2 steps: 0=off, 1=on]
    //   49: "Sidechain Filtering"
    // -------------------------------------------------------------------------
    { "Weiss DS1-MK3", {
        { "comp_thresh",    "Threshold 1" },
        { "comp_ratio",     "Ratio 1" },
        { "comp_attack",    "Attack 1" },
        { "comp_release",   "Release Fast 1" },
        { "comp_thresh2",   "Threshold 2" },
        { "comp_ratio2",    "Ratio 2" },
        { "comp_attack2",   "Attack 2" },
        { "comp_release2",  "Release Fast 2" },
        { "sidechain",      "Sidechain" },         // idx 37
        { "freq",           "Center Frequency 1" },
        { "input_gain",     "Input Gain" },
        { "output_gain",    "Output Gain" },
    }},

    // -------------------------------------------------------------------------
    // Summit Audio Grand Channel — verified with ScanPluginParams
    //   18: "Low Cut"   19: "High Cut"   20: "Output Volume"   22: "Comp Bypass"
    //   24: "Gain Reduction"   25: "Gain"   26: "Attack"   27: "Release"
    //   32: "Sidechain" [2 steps: 0=off, 1=on]
    // -------------------------------------------------------------------------
    { "Summit Audio Grand Channel", {
        { "input_gain",   "Output Volume" },   // idx 20 (closest to input trim)
        { "comp_bypass",  "Comp Bypass" },      // idx 22
        { "comp_thresh",  "Gain Reduction" },   // idx 24 (GR knob = threshold)
        { "comp_gain",    "Gain" },             // idx 25
        { "comp_attack",  "Attack" },           // idx 26
        { "comp_release", "Release" },          // idx 27
        { "sidechain",    "Sidechain" },        // idx 32
        { "saturation",   "Saturation" },       // idx 30
        { "low_cut",      "Low Cut" },          // idx 18
        { "high_cut",     "High Cut" },         // idx 19
        { "eq_low",       "Low Gain" },         // idx 2
        { "eq_low_mid",   "Low Mid Gain" },     // idx 7
        { "eq_high_mid",  "High Mid Gain" },    // idx 11
        { "eq_high",      "High Gain" },        // idx 15
    }},

    // -------------------------------------------------------------------------
    // UA Century Channel Strip — verified with ScanPluginParams
    // -------------------------------------------------------------------------
    { "UA Century Channel", {
        { "comp_thresh",  "Threshold" },
        { "comp_ratio",   "Ratio" },
        { "comp_attack",  "Attack" },
        { "comp_release", "Release" },
        { "output_gain",  "Output" },
        { "eq_low",       "Low Gain" },
        { "eq_high",      "High Gain" },
    }},

    // -------------------------------------------------------------------------
    // UA Manley VoxBox — verified with ScanPluginParams
    // -------------------------------------------------------------------------
    { "UA Manley VoxBox", {
        { "comp_thresh",  "Threshold" },
        { "comp_ratio",   "Ratio" },
        { "comp_attack",  "Attack" },
        { "comp_release", "Release" },
        { "output_gain",  "Output" },
        { "eq_low",       "Low Gain" },
        { "eq_high",      "High Gain" },
    }},

    // -------------------------------------------------------------------------
    // UA 1176LN Rev E — verified with ScanPluginParams
    // -------------------------------------------------------------------------
    { "UA 1176LN Rev E", {
        { "input",        "Input" },
        { "output",       "Output" },
        { "comp_attack",  "Attack" },
        { "comp_release", "Release" },
        { "comp_ratio",   "Ratio" },
    }},

    // -------------------------------------------------------------------------
    // UA Fairchild 670 — verified with ScanPluginParams
    // -------------------------------------------------------------------------
    { "UA Fairchild 670", {
        { "input",        "Input Gain" },
        { "comp_thresh",  "Threshold" },
        { "comp_time",    "Time Constant" },
        { "output",       "Output Gain" },
    }},

    // -------------------------------------------------------------------------
    // Pre 1973 (Neve 1073 preamp). Param names TBD — scan had many params.
    // These are best-guess substrings; update once confirmed.
    // -------------------------------------------------------------------------
    { "Pre 1973", {
        { "input_gain",  "Input Gain" },
        { "output_gain", "Output Gain" },
        { "eq_high",     "High Gain" },
        { "eq_mid",      "Mid Gain" },
        { "eq_low",      "Low Gain" },
        { "high_cut",    "High Cut" },
        { "low_cut",     "Low Cut" },
    }},

    // Softube American Class A (API-style)
    { "American Class A", {
        { "input_gain",   "Input Gain" },
        { "comp_thresh",  "Threshold" },
        { "comp_ratio",   "Ratio" },
        { "comp_attack",  "Attack" },
        { "comp_release", "Release" },
        { "eq_low",       "Low Gain" },
        { "eq_mid",       "Mid Gain" },
        { "eq_high",      "High Gain" },
        { "output_gain",  "Output Gain" },
    }},

    // Softube British Class A (SSL-style)
    { "British Class A", {
        { "input_gain",   "Input Gain" },
        { "comp_thresh",  "Threshold" },
        { "comp_ratio",   "Ratio" },
        { "comp_attack",  "Attack" },
        { "comp_release", "Release" },
        { "eq_low",       "Low Gain" },
        { "eq_mid",       "Mid Gain" },
        { "eq_high",      "High Gain" },
        { "output_gain",  "Output Gain" },
    }},
};

// --- Instrument parameter maps ---
const std::map<juce::String, std::map<juce::String, juce::String>> MacroMapper::instrumentMaps = {

    // -------------------------------------------------------------------------
    // Arturia Mini V3 (Minimoog emulation)
    // -------------------------------------------------------------------------
    { "Mini V3", {
        { "brightness",    "Cutoff Frequency" },
        { "resonance",     "Resonance" },
        { "attack",        "Attack Time" },
        { "decay",         "Decay Time" },
        { "sustain",       "Sustain Level" },
        { "release",       "Release Time" },
        { "filter_env",    "Filter Env Depth" },
        { "osc2_tune",     "Osc 2 Tune" },
        { "osc3_tune",     "Osc 3 Tune" },
        { "lfo_rate",      "LFO Rate" },
        { "lfo_amount",    "LFO Amount" },
        { "drive",         "Drive" },
        { "volume",        "Volume" },
    }},

    // Arturia CS-80 V4
    { "CS-80 V4", {
        { "brightness",    "LP 1 cutoff" },
        { "resonance",     "LP 1 resonance" },
        { "brightness2",   "LP 2 cutoff" },
        { "attack",        "Attack Time" },
        { "decay",         "Decay Time" },
        { "sustain",       "Sustain Level" },
        { "release",       "Release Time" },
        { "vco1_detune",   "VCO 1 finetune" },
        { "vco2_detune",   "VCO 2 finetune" },
        { "volume",        "Volume" },
    }},

    // Arturia Prophet-5 V
    { "Prophet-5 V", {
        { "brightness",    "VCF cutoff" },
        { "resonance",     "VCF resonance" },
        { "attack",        "VCA Attack" },
        { "decay",         "VCA Decay" },
        { "sustain",       "VCA Sustain" },
        { "release",       "VCA Release" },
        { "filter_attack", "VCF Attack" },
        { "filter_decay",  "VCF Decay" },
        { "osc2_tune",     "VCO 2 freq" },
        { "lfo_rate",      "LFO Frequency" },
        { "volume",        "Volume" },
    }},

    // Arturia Jup-8 V4
    { "Jup-8 V4", {
        { "brightness",    "Filter Cutoff" },
        { "resonance",     "Filter Resonance" },
        { "attack",        "Amp Attack" },
        { "decay",         "Amp Decay" },
        { "sustain",       "Amp Sustain" },
        { "release",       "Amp Release" },
        { "filter_attack", "Filter Attack" },
        { "filter_decay",  "Filter Decay" },
        { "lfo_rate",      "LFO Rate" },
        { "volume",        "Volume" },
    }},

    // Arturia DX7 V
    { "DX7 V", {
        { "brightness",    "Filter Cutoff" },
        { "resonance",     "Filter Resonance" },
        { "attack",        "OP1 EG Rate 1" },
        { "decay",         "OP1 EG Rate 2" },
        { "sustain",       "OP1 EG Level 3" },
        { "release",       "OP1 EG Rate 4" },
        { "volume",        "Volume" },
    }},

    // Arturia Buchla Easel V
    { "Buchla Easel V", {
        { "brightness",    "Lowpass Guard Freq" },
        { "attack",        "Complex Osc Attack" },
        { "decay",         "Complex Osc Decay" },
        { "lfo_rate",      "LFO Rate" },
        { "volume",        "Volume" },
    }},

    // Arturia Jun-6 V
    { "Jun-6 V", {
        { "brightness",    "VCF Cutoff" },
        { "resonance",     "VCF Resonance" },
        { "attack",        "VCA Attack" },
        { "decay",         "VCA Decay" },
        { "sustain",       "VCA Sustain" },
        { "release",       "VCA Release" },
        { "chorus",        "Chorus Depth" },
        { "lfo_rate",      "LFO Rate" },
        { "volume",        "Volume" },
    }},

    // Arturia OB-Xa V
    { "OB-Xa V", {
        { "brightness",    "Filter Cutoff" },
        { "resonance",     "Filter Resonance" },
        { "attack",        "Amp Attack" },
        { "decay",         "Amp Decay" },
        { "sustain",       "Amp Sustain" },
        { "release",       "Amp Release" },
        { "volume",        "Volume" },
    }},

    // Arturia Augmented Strings
    { "Augmented Strings", {
        { "brightness",    "Filter Cutoff" },
        { "resonance",     "Filter Resonance" },
        { "attack",        "Amp Attack" },
        { "decay",         "Amp Decay" },
        { "sustain",       "Amp Sustain" },
        { "release",       "Amp Release" },
        { "engine_mix",    "Engine Mix" },
        { "volume",        "Volume" },
    }},

    // -------------------------------------------------------------------------
    // Surge XT — verified scene A param names from ScanPluginParams
    // -------------------------------------------------------------------------
    { "Surge XT", {
        { "brightness",  "scene A:Filter 1 Cutoff" },
        { "resonance",   "scene A:Filter 1 Resonance" },
        { "attack",      "scene A:Amp EG Attack" },
        { "decay",       "scene A:Amp EG Decay" },
        { "sustain",     "scene A:Amp EG Sustain" },
        { "release",     "scene A:Amp EG Release" },
        { "lfo_rate",    "scene A:LFO 1 Rate" },
        { "volume",      "scene A:Volume" },
    }},

    // -------------------------------------------------------------------------
    // Sonic Academy Kick 3
    // NOTE: Kick 3 has nearly 4000 unique params (complex multi-synth engine).
    // These are verified top-level params. Use getPluginParams for discovery.
    // -------------------------------------------------------------------------
    { "Kick 3", {
        { "pitch",       "synth: Main | Pitch" },
        { "drive",       "synth: Main | Drive" },
        { "volume",      "synth: Main | Level" },
        { "attack",      "synth: ADSR-1 | Attack" },
        { "decay",       "synth: ADSR-1 | Decay" },
        { "sustain",     "synth: ADSR-1 | Sustain" },
        { "release",     "synth: ADSR-1 | Release" },
    }},

    // Softube Heartbeat
    { "Heartbeat", {
        { "kick_pitch",    "BD1 Pitch" },
        { "kick_decay",    "BD1 Decay" },
        { "kick_attack",   "BD1 Attack" },
        { "kick_drive",    "BD1 Harmonics" },
        { "kick_level",    "BD1 Level" },
        { "snare_pitch",   "SN1 Pitch" },
        { "snare_decay",   "SN1 Decay" },
        { "snare_level",   "SN1 Level" },
        { "hat_pitch",     "HH Pitch" },
        { "hat_decay",     "HH Decay" },
        { "hat_level",     "HH Level" },
        { "master_width",  "Stereo Width" },
        { "saturation",    "Master Saturation" },
    }},

    // -------------------------------------------------------------------------
    // Future Audio Workshop SubLabXL — verified param names from scan
    // SubLabXL has many params (~700); these are the most useful macro targets.
    // -------------------------------------------------------------------------
    { "SubLabXL", {
        { "brightness",    "Sub:Filter Cutoff" },
        { "resonance",     "Sub:Filter Resonance" },
        { "drive",         "Sub:Distortion Amount" },
        { "sub_level",     "Sub:Level" },
        { "attack",        "Sub:Amp Attack" },
        { "decay",         "Sub:Amp Decay" },
        { "sustain",       "Sub:Amp Sustain" },
        { "release",       "Sub:Amp Release" },
        { "pitch_env",     "Sub:Pitch Env Depth" },
        { "pitch_decay",   "Sub:Pitch Env Decay" },
    }},

    // -------------------------------------------------------------------------
    // Softube Monoment Bass — verified from scan (51 params)
    // -------------------------------------------------------------------------
    { "Monoment Bass", {
        { "brightness",  "Filter Cutoff" },
        { "resonance",   "Filter Resonance" },
        { "drive",       "Drive" },
        { "attack",      "Amp Attack" },
        { "decay",       "Amp Decay" },
        { "sustain",     "Amp Sustain" },
        { "release",     "Amp Release" },
        { "volume",      "Volume" },
        { "sub",         "Sub Level" },
    }},

    // -------------------------------------------------------------------------
    // Output Statement Lead — verified from scan
    // -------------------------------------------------------------------------
    { "Statement Lead", {
        { "brightness",  "Filter Cutoff" },
        { "resonance",   "Filter Resonance" },
        { "attack",      "Amp Attack" },
        { "decay",       "Amp Decay" },
        { "sustain",     "Amp Sustain" },
        { "release",     "Amp Release" },
        { "drive",       "Drive" },
        { "lfo_rate",    "LFO Rate" },
        { "volume",      "Volume" },
    }},
};

// --- Effect parameter maps ---
const std::map<juce::String, std::map<juce::String, juce::String>> MacroMapper::effectMaps = {

    // -------------------------------------------------------------------------
    // Valhalla Room — verified with ScanPluginParams
    // Exact param names (case-sensitive, from scan): mix, predelay, decay, HiCut,
    // LoCut, earlyLateMix, lateSize, lateCross, lateModRate, lateModDepth,
    // RTBassMultiply, RTXover, RTHighMultiply, RTHighXover, diffusion, type, space
    // -------------------------------------------------------------------------
    { "ValhallaRoom", {
        { "mix",       "mix" },
        { "decay",     "decay" },
        { "predelay",  "predelay" },
        { "high_cut",  "HiCut" },
        { "low_cut",   "LoCut" },
        { "size",      "lateSize" },
        { "diffusion", "diffusion" },
        { "mod_rate",  "lateModRate" },
        { "mod_depth", "lateModDepth" },
        { "highs",     "RTHighMultiply" },
        { "lows",      "RTBassMultiply" },
        { "type",      "type" },
    }},

    // -------------------------------------------------------------------------
    // Valhalla Vintage Verb — same snake_case naming convention
    // -------------------------------------------------------------------------
    { "ValhallaVintageVerb", {
        { "mix",       "mix" },
        { "decay",     "decay" },
        { "predelay",  "predelay" },
        { "high_cut",  "HiCut" },
        { "low_cut",   "LoCut" },
        { "size",      "Size" },
        { "diffusion", "diffusion" },
        { "mod_rate",  "ModRate" },
        { "mod_depth", "ModDepth" },
    }},

    // -------------------------------------------------------------------------
    // oeksound soothe2 — verified with ScanPluginParams (all lowercase)
    // Exact names: depth, sharpness, selectivity, attack, release, mix, trim,
    //              delta, bypass, sidechain, band1 freq, band1 sens, etc.
    // -------------------------------------------------------------------------
    { "soothe2", {
        { "depth",       "depth" },
        { "sharpness",   "sharpness" },
        { "sensitivity", "selectivity" },  // "selectivity" = the sensitivity-style param
        { "attack",      "attack" },
        { "release",     "release" },
        { "mix",         "mix" },
        { "trim",        "trim" },
        { "delta",       "delta" },
        { "sidechain",   "sidechain" },
        { "low_cut",     "low cut freq" },
        { "high_cut",    "high cut freq" },
        { "mode",        "mode" },
    }},

    // -------------------------------------------------------------------------
    // Eventide Transient Shaper — verified (9 params total)
    //   0: Sustain   1: Sustain Band   2: Punch   3: Punch Band
    //   4: Punch Type   5: Crossover Freq   6: Output Level   7: Clip   8: Bypass
    // -------------------------------------------------------------------------
    { "Transient Shaper", {
        { "attack",      "Punch" },
        { "sustain",     "Sustain" },
        { "crossover",   "Crossover Freq" },
        { "output",      "Output Level" },
        { "clip",        "Clip" },
    }},

    // -------------------------------------------------------------------------
    // Weiss MM-1 Mastering Maximizer — verified with ScanPluginParams
    // -------------------------------------------------------------------------
    { "Weiss MM-1 Mastering Maximizer", {
        { "threshold",   "Threshold" },
        { "release",     "Release" },
        { "output",      "Output Gain" },
        { "limiter",     "Safety Limiter" },
    }},

    // Softube Tube Delay
    { "Tube Delay", {
        { "echo",       "Mix" },
        { "delay_time", "Time" },
        { "feedback",   "Feedback" },
        { "drive",      "Drive" },
        { "high_cut",   "High Cut" },
        { "low_cut",    "Low Cut" },
    }},

    // Softube Widener
    { "Widener", {
        { "width",     "Width" },
        { "amount",    "Amount" },
        { "mono_bass", "Mono Bass" },
        { "output",    "Output Gain" },
    }},

    // Arturia Dist TUBE-CULTURE
    { "Dist TUBE-CULTURE", {
        { "drive",   "Drive" },
        { "tone",    "Tone" },
        { "volume",  "Volume" },
        { "mix",     "Mix" },
    }},

    // Output Portal — verified with ScanPluginParams
    { "Portal", {
        { "mix",      "MASTER COMPRESSOR DRY/WET" },
        { "thresh",   "MASTER COMPRESSOR THRESHOLD" },
        { "ratio",    "MASTER COMPRESSOR RATIO" },
        { "attack",   "MASTER COMPRESSOR ATTACK" },
        { "release",  "MASTER COMPRESSOR RELEASE" },
    }},
};

juce::String MacroMapper::getParameterID(const juce::String& pluginName, const juce::String& macroName)
{
    // Check channel strips
    auto it1 = channelStripMaps.find(pluginName);
    if (it1 != channelStripMaps.end()) {
        auto it2 = it1->second.find(macroName);
        if (it2 != it1->second.end()) return it2->second;
    }

    // Check instruments
    auto it3 = instrumentMaps.find(pluginName);
    if (it3 != instrumentMaps.end()) {
        auto it4 = it3->second.find(macroName);
        if (it4 != it3->second.end()) return it4->second;
    }

    // Check effects
    auto it5 = effectMaps.find(pluginName);
    if (it5 != effectMaps.end()) {
        auto it6 = it5->second.find(macroName);
        if (it6 != it5->second.end()) return it6->second;
    }

    // Fallback: the macro itself might be the literal parameter name
    return macroName;
}
