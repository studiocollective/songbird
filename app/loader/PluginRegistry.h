#pragma once

#include <string>
#include <memory>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

// --- Plugin keyword → display names ---
struct PluginInfo {
    juce::String pluginId;
    juce::String pluginName;
};

// Map a bird-file keyword (e.g. "synths", "kick") to a PluginInfo
PluginInfo pluginFromKeyword(const std::string& keyword);

// Search the scanned plugin list for a plugin matching the given display name
std::unique_ptr<juce::PluginDescription> findPluginByName(
    te::Engine& engine, const juce::String& name);

// --- Well-known plugin constants ---
extern const PluginInfo CONSOLE_1;
extern const PluginInfo REVERB_VALHALLA;
extern const PluginInfo DELAY_TUBE;
extern const PluginInfo DIST_CULTURE;
extern const PluginInfo WEISS_DS1;
