#include "MacroMapper.h"

// Define the static channel strip mappings
const std::map<juce::String, std::map<juce::String, juce::String>> MacroMapper::channelStripMaps = {
    { "Console 1", {
        { "input_gain", "InputGain" },
        { "low_cut", "HighPass" },
        { "high_cut", "LowPass" },
        { "transient_shape", "ShapeAttack" },
        { "transient_sustain", "ShapeSustain" },
        { "eq_low_gain", "EQLowGain" },
        { "eq_mid_gain", "EQMidGain" },
        { "eq_high_gain", "EQHighGain" },
        { "comp_thresh", "CompThreshold" },
        { "comp_ratio", "CompRatio" },
        { "comp_mix", "CompDryWet" },
        { "drive", "Drive" },
        { "character", "Character" }
    }}
    // Add other channel strips here (American Class A, etc.)
};

// Define the static instrument mappings
const std::map<juce::String, std::map<juce::String, juce::String>> MacroMapper::instrumentMaps = {
    { "Mini V 3", {
        { "brightness", "Cutoff" },
        { "resonance", "Emphasis" },
        { "attack", "AttackTime" },
        { "decay", "DecayTime" },
        { "sustain", "SustainLevel" }
    }},
    { "SubLabXL", {
        { "brightness", "FilterCutoff" },
        { "drive", "DistortionAmount" },
        { "sub_level", "SubVolume" },
        { "attack", "AmpAttack" },
        { "pitch_decay", "PitchEnvDecay" }
    }},
    { "Surge XT", {
        { "brightness", "scene_a_filter_1_cutoff" }, // Assuming a typical layout
        { "resonance", "scene_a_filter_1_resonance" },
        { "attack", "scene_a_amp_env_attack" }
    }}
    // Add other synths here
};

// Define the static effect mappings
const std::map<juce::String, std::map<juce::String, juce::String>> MacroMapper::effectMaps = {
    { "ValhallaRoom", {
        { "space", "Mix" },
        { "decay", "Decay" },
        { "predelay", "PreDelay" },
        { "size", "Size" }
    }},
    { "Tube Delay", {
        { "echo", "Mix" },
        { "delay_time", "Time" },
        { "feedback", "Feedback" },
        { "drive", "Drive" }
    }}
    // Add other effects here
};

juce::String MacroMapper::getParameterID(const juce::String& pluginName, const juce::String& macroName)
{
    // Search channel strips first
    auto it1 = channelStripMaps.find(pluginName);
    if (it1 != channelStripMaps.end())
    {
        auto it2 = it1->second.find(macroName);
        if (it2 != it1->second.end()) return it2->second;
    }

    // Search instruments
    auto it3 = instrumentMaps.find(pluginName);
    if (it3 != instrumentMaps.end())
    {
        auto it4 = it3->second.find(macroName);
        if (it4 != it3->second.end()) return it4->second;
    }

    // Search effects
    auto it5 = effectMaps.find(pluginName);
    if (it5 != effectMaps.end())
    {
        auto it6 = it5->second.find(macroName);
        if (it6 != it5->second.end()) return it6->second;
    }

    // Fallback: the macro itself might be the literal parameter ID
    return macroName;
}
