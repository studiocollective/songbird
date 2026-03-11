#include "PluginRegistry.h"

// --- Well-known plugin constants ---
const PluginInfo CONSOLE_1 = { "softube.console-1", "Console 1" };
const PluginInfo REVERB_VALHALLA = { "valhalladsp.valhallaroom", "ValhallaRoom" };
const PluginInfo DELAY_TUBE = { "softube.tube-delay", "Tube Delay" };
const PluginInfo DIST_CULTURE = { "arturia.dist-tube-culture", "Dist TUBE-CULTURE" };
const PluginInfo WEISS_DS1 = { "softube.ds1-mk3", "DS1-MK3" };

PluginInfo pluginFromKeyword(const std::string& keyword) {
    // Arturia classic emulations
    if (keyword == "synths")   return { "arturia.pigments",      "Pigments" };
    if (keyword == "surge")    return { "surge-synth-team.surge-xt", "Surge XT" };
    if (keyword == "mini")     return { "arturia.mini-v",        "Mini V" };
    if (keyword == "cs80")     return { "arturia.cs-80-v",       "CS-80 V" };
    if (keyword == "prophet")  return { "arturia.prophet-v",     "Prophet-5 V" };
    if (keyword == "jup8")     return { "arturia.jup-8-v",       "Jup-8 V" };
    if (keyword == "dx7")      return { "arturia.dx7-v",         "DX7 V" };
    if (keyword == "buchla")   return { "arturia.buchla-easel-v","Buchla Easel V" };
    // Drums & bass
    if (keyword == "kick")     return { "sonicacademy.kick-3",   "Kick 3" };
    if (keyword == "drums")    return { "softube.heartbeat",     "Heartbeat" };
    if (keyword == "bass")     return { "arturia.mini-v",        "Mini V" };
    if (keyword == "monoment") return { "softube.monoment-bass", "Monoment Bass" };
    if (keyword == "sublab")   return { "futureaudioworkshop.sublabxl", "SubLabXL" };
    // Effects
    if (keyword == "delay")    return { "softube.tube-delay", "Tube Delay" };
    if (keyword == "valhalla") return { "valhalladsp.valhallaroom", "ValhallaRoom" };
    if (keyword == "widener")  return { "polyversemusic.widener", "Widener" };
    if (keyword == "soothe")   return { "oeksound.soothe2", "soothe2" };
    if (keyword == "tube")     return { "arturia.dist-tube-culture", "Dist TUBE-CULTURE" };
    
    // Channel Strips
    if (keyword == "console1") return { "softube.console-1", "Console 1" };
    
    return {}; // empty = no external plugin
}

// Search the scanned plugin list for a plugin matching the given display name
std::unique_ptr<juce::PluginDescription> findPluginByName(
    te::Engine& engine, const juce::String& name)
{
    if (name.isEmpty()) return {};

    auto& list = engine.getPluginManager().knownPluginList;
    auto lowerName = name.toLowerCase();

    // Exact match first
    for (const auto& desc : list.getTypes())
        if (desc.name.toLowerCase() == lowerName)
            return std::make_unique<juce::PluginDescription>(desc);

    // Substring match (e.g. "Kick 2" matches "Sonic Academy Kick 2")
    for (const auto& desc : list.getTypes())
        if (desc.name.toLowerCase().contains(lowerName))
            return std::make_unique<juce::PluginDescription>(desc);

    return {};
}
