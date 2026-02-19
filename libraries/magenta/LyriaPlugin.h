#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include "Gemini.h"
#include "CircularBuffer.h"
#include "LyriaConfig.h"

namespace te = tracktion;

namespace magenta {

//==============================================================================
/**
 *  LyriaPlugin — a Tracktion Engine Plugin that streams AI-generated
 *  audio from the Lyria RealTime API via WebSocket.
 *
 *  Each instance owns its own Gemini connection and CircularBuffer.
 *  Audio chunks arrive as base64-encoded 16-bit PCM stereo @ 48 kHz,
 *  get decoded into the ring buffer, and are read out in applyToBuffer()
 *  with sample-rate conversion to match the DAW rate.
 */
class LyriaPlugin : public te::Plugin
{
public:
    LyriaPlugin(te::PluginCreationInfo);
    ~LyriaPlugin() override;

    //--- Tracktion Plugin interface ---
    static const char* getPluginName()  { return "Lyria Generator"; }
    static const char* xmlTypeName;

    juce::String getName() const override           { return getPluginName(); }
    juce::String getPluginType() override            { return xmlTypeName; }
    juce::String getSelectableDescription() override { return getName(); }
    bool needsConstantBufferSize() override          { return true; }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer(const te::PluginRenderContext&) override;

    //--- Lyria controls ---
    void setApiKey(const juce::String& key);
    void setPrompts(const std::vector<Prompt>& prompts);
    void setConfig(LyriaConfig& config);
    void play();
    void pause();
    void resetContext();

    bool isConnected() const    { return gemini.isConnected(); }
    bool isBuffering() const    { return buffering.load(); }

    //--- Callbacks (set by SongbirdEditor to push state to UI) ---
    std::function<void(bool connected, bool buffering)> onStatusChange;

    juce::CachedValue<juce::String> apiKeyValue;

private:
    Gemini gemini;
    CircularBuffer audioBuffer;
    std::atomic<bool> buffering{false};
    std::atomic<bool> playing{false};
    double currentSampleRate = 44100.0;

    void processAudioChunks(std::vector<AudioChunk> chunks);
    void loadApiKeyFromInfiniteCrate();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LyriaPlugin)
};

}  // namespace magenta
