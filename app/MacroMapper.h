#pragma once

#include <JuceHeader.h>
#include <map>

class MacroMapper
{
public:
    /**
     * Looks up the actual VST3 parameter ID for a given plugin and Songbird semantic macro.
     * @param pluginName The name of the loaded plugin (e.g., "Mini V3", "Console 1")
     * @param macroName The Songbird semantic macro (e.g., "brightness", "drive")
     * @return The exact parameter ID string as exposed by the VST3, or empty string if not found.
     */
    static juce::String getParameterID(const juce::String& pluginName, const juce::String& macroName);

private:
    MacroMapper() = delete; // Static only

    // Internal maps
    static const std::map<juce::String, std::map<juce::String, juce::String>> channelStripMaps;
    static const std::map<juce::String, std::map<juce::String, juce::String>> instrumentMaps;
    static const std::map<juce::String, std::map<juce::String, juce::String>> effectMaps;
};
