#include "MasterAnalyzerPlugin.h"

const char* MasterAnalyzerPlugin::xmlTypeName = "masterAnalyzer";

juce::ValueTree MasterAnalyzerPlugin::create()
{
    return juce::ValueTree(te::IDs::PLUGIN, {
        { te::IDs::type, xmlTypeName }
    });
}
