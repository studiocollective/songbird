#include "LyriaPlugin.h"

namespace magenta {

const char* LyriaPlugin::xmlTypeName = "lyria-generator";

//==============================================================================
LyriaPlugin::LyriaPlugin(te::PluginCreationInfo info)
    : te::Plugin(info),
      audioBuffer(DEFAULT_FRAME_CAPACITY, CircularBuffer::Format::Stereo)
{
    // Wire up Gemini callbacks
    gemini.onAudioChunks = [this](std::vector<AudioChunk> chunks) {
        processAudioChunks(std::move(chunks));
    };
    gemini.onConnected = [this]() {
        DBG("LyriaPlugin: connected to Lyria RealTime");
        if (onStatusChange)
            onStatusChange(true, buffering.load());
    };
    gemini.onClose = [this](json) {
        DBG("LyriaPlugin: disconnected from Lyria RealTime");
        if (onStatusChange)
            onStatusChange(false, buffering.load());
    };
    gemini.onContextClear = [this]() {
        audioBuffer.clear();
        buffering.store(false);
    };
    gemini.onFilteredPrompt = [](json prompt) {
        DBG("LyriaPlugin: prompt filtered — " << prompt.dump());
    };

    // Try loading API key from The Infinite Crate settings
    loadApiKeyFromInfiniteCrate();
}

LyriaPlugin::~LyriaPlugin()
{
}

//==============================================================================
void LyriaPlugin::initialise(const te::PluginInitialisationInfo& info)
{
    currentSampleRate = info.sampleRate;
    DBG("LyriaPlugin initialised at " << currentSampleRate << " Hz");
}

void LyriaPlugin::deinitialise()
{
    DBG("LyriaPlugin deinitialised");
}

void LyriaPlugin::applyToBuffer(const te::PluginRenderContext& rc)
{
    if (!playing.load() || !rc.destBuffer)
        return;

    auto& buffer = *rc.destBuffer;
    int numSamples = rc.bufferNumSamples;

    // Read from the circular buffer into the track's audio buffer
    int framesRead = audioBuffer.readStereo(
        buffer, numSamples, static_cast<float>(currentSampleRate));

    if (framesRead == 0)
    {
        // No audio available — clear output and flag buffering
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, rc.bufferStartSample, numSamples);

        if (playing.load() && !buffering.load())
        {
            buffering.store(true);
            if (onStatusChange)
                onStatusChange(gemini.isConnected(), true);
        }
    }
    else
    {
        if (buffering.load())
        {
            buffering.store(false);
            if (onStatusChange)
                onStatusChange(gemini.isConnected(), false);
        }
    }
}

//==============================================================================
void LyriaPlugin::setApiKey(const juce::String& key)
{
    if (key.isNotEmpty())
    {
        gemini.initialize(key.toStdString());
        DBG("LyriaPlugin: API key set, connecting...");
    }
}

void LyriaPlugin::setPrompts(const std::vector<Prompt>& promptList)
{
    gemini.sendPrompts(promptList);
}

void LyriaPlugin::setConfig(LyriaConfig& config)
{
    gemini.sendConfig(config);
}

void LyriaPlugin::play()
{
    playing.store(true);
    gemini.sendTransport(true);
    DBG("LyriaPlugin: play");
}

void LyriaPlugin::pause()
{
    playing.store(false);
    gemini.sendTransport(false);
    DBG("LyriaPlugin: pause");
}

void LyriaPlugin::resetContext()
{
    gemini.resetContext();
    audioBuffer.clear();
    buffering.store(false);
    DBG("LyriaPlugin: context reset");
}

//==============================================================================
void LyriaPlugin::processAudioChunks(std::vector<AudioChunk> chunks)
{
    const int numChannels = 2;
    const int bitDepth = 16;
    const float scaleFactor = 1.0f / 32768.0f;

    for (const auto& chunk : chunks)
    {
        if (!chunk.data.has_value())
            continue;

        juce::MemoryOutputStream decodedStream;
        juce::Base64::convertFromBase64(decodedStream, chunk.data.value());

        const juce::MemoryBlock& decodedData = decodedStream.getMemoryBlock();
        const char* rawBytes = static_cast<const char*>(decodedData.getData());
        size_t numBytes = decodedData.getSize();

        const int bytesPerSample = bitDepth / 8;
        const int bytesPerFrame = bytesPerSample * numChannels;
        int numSamples = static_cast<int>(numBytes / bytesPerFrame);

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            int frameByteOffset = sampleIndex * bytesPerFrame;
            int16_t rawSample1 = juce::ByteOrder::littleEndianShort(
                reinterpret_cast<const int16_t*>(rawBytes + frameByteOffset));
            int16_t rawSample2 = juce::ByteOrder::littleEndianShort(
                reinterpret_cast<const int16_t*>(rawBytes + frameByteOffset + bytesPerSample));

            audioBuffer.writeStereoSample(
                static_cast<float>(rawSample1) * scaleFactor,
                static_cast<float>(rawSample2) * scaleFactor);
        }
    }
}

void LyriaPlugin::loadApiKeyFromInfiniteCrate()
{
    // Read the API key from The Infinite Crate's ApplicationProperties
    juce::PropertiesFile::Options options;
    options.applicationName = MAGENTA_APP_NAME;
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = juce::String(MAGENTA_COMPANY_NAME) +
                         juce::File::getSeparatorString() +
                         juce::String(MAGENTA_APP_NAME);
    options.storageFormat = juce::PropertiesFile::storeAsXML;

    juce::ApplicationProperties appProps;
    appProps.setStorageParameters(options);

    juce::String savedState = appProps.getUserSettings()->getValue(APP_STATE_KEY, juce::String());
    if (savedState.isNotEmpty())
    {
        try
        {
            json state = json::parse(savedState.toStdString());
            if (state.contains("credentials") && state["credentials"].contains("api_key"))
            {
                std::string apiKey = state["credentials"]["api_key"];
                if (!apiKey.empty())
                {
                    DBG("LyriaPlugin: loaded API key from Infinite Crate settings");
                    gemini.initialize(apiKey);
                }
            }
        }
        catch (const std::exception& e)
        {
            DBG("LyriaPlugin: failed to parse Infinite Crate settings — " << e.what());
        }
    }
    else
    {
        DBG("LyriaPlugin: no Infinite Crate settings found, API key must be set manually");
    }
}

}  // namespace magenta
