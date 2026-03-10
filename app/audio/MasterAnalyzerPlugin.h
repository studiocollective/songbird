#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <functional>

namespace te = tracktion;

/**
 * MasterAnalyzerPlugin — A lightweight Tracktion internal plugin that sits
 * on the master track and taps the audio buffer for spectrum/stereo analysis.
 *
 * This avoids modifying Tracktion Engine source code. The plugin passes
 * audio through unchanged (zero processing overhead) and just calls a
 * callback with the buffer pointer.
 */
class MasterAnalyzerPlugin : public te::Plugin
{
public:
    MasterAnalyzerPlugin(te::PluginCreationInfo info) : te::Plugin(info) {}
    ~MasterAnalyzerPlugin() override = default;

    static const char* xmlTypeName;
    static juce::ValueTree create();

    juce::String getName() const override           { return "MasterAnalyzer"; }
    juce::String getPluginType() override            { return xmlTypeName; }
    bool canBeAddedToClip() override                 { return false; }
    bool canBeAddedToRack() override                 { return false; }
    bool needsConstantBufferSize() override          { return false; }
    bool producesAudioWhenNoAudioInput() override    { return false; }

    void initialise(const te::PluginInitialisationInfo&) override {}
    void deinitialise() override {}

    void applyToBuffer(const te::PluginRenderContext& fc) override
    {
        if (fc.destBuffer == nullptr) return;
        if (onBuffer)
            onBuffer(*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples);
    }

    // Callback — set by PlaybackInfo to receive raw master audio
    std::function<void(const juce::AudioBuffer<float>&, int, int)> onBuffer;
};
