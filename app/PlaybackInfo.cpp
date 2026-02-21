#include "PlaybackInfo.h"
#include <cmath>
#include <juce_dsp/juce_dsp.h>

class MasterAnalyzerPlugin : public te::Plugin
{
public:
    MasterAnalyzerPlugin(te::PluginCreationInfo info, PlaybackInfo& owner_)
        : te::Plugin(info), owner(owner_)
    {
    }

    ~MasterAnalyzerPlugin() override {}

    juce::String getName() const override { return "Master Analyzer"; }
    juce::String getPluginType() override { return "MasterAnalyzer"; }
    juce::String getSelectableDescription() override { return "Master Analyzer Plugin"; }

    void initialise(const te::PluginInitialisationInfo&) override {}
    void deinitialise() override {}

    void applyToBuffer(const te::PluginRenderContext& rc) override
    {
        if (rc.destBuffer != nullptr)
            owner.processMasterBuffer(*rc.destBuffer, rc.bufferNumSamples);
    }

    PlaybackInfo& owner;
};

PlaybackInfo::PlaybackInfo()
{
}

PlaybackInfo::~PlaybackInfo()
{
    stopTimer();
    detachClients();
}

void PlaybackInfo::setEdit(te::Edit* edit)
{
    detachClients();
    currentEdit = edit;
    if (currentEdit)
        attachClients();
}

void PlaybackInfo::setWebView(juce::WebBrowserComponent* wv)
{
    webView = wv;
    if (webView && currentEdit && !isTimerRunning())
    {
        startTimerHz(30);
        DBG("PlaybackInfo: Timer started (30Hz)");
    }
}

//==============================================================================
// Client attachment
//==============================================================================

void PlaybackInfo::attachClients()
{
    if (!currentEdit) return;

    auto tracks = te::getAudioTracks(*currentEdit);
    trackClients.clear();
    trackClients.reserve(tracks.size());

    for (int i = 0; i < tracks.size(); i++)
    {
        trackClients.push_back(std::make_unique<te::LevelMeasurer::Client>());
        if (auto* meter = tracks[i]->getLevelMeterPlugin())
            meter->measurer.addClient(*trackClients.back());
    }

    if (webView)
    {
        startTimerHz(30);
        DBG("PlaybackInfo: Attached " + juce::String((int)trackClients.size()) + " clients, timer started");
    }
    else
    {
        DBG("PlaybackInfo: Attached " + juce::String((int)trackClients.size()) + " clients (no webview yet)");
    }
}

void PlaybackInfo::detachClients()
{
    stopTimer();

    if (currentEdit)
    {
        auto tracks = te::getAudioTracks(*currentEdit);
        for (int i = 0; i < (int)trackClients.size() && i < tracks.size(); i++)
        {
            if (auto* meter = tracks[i]->getLevelMeterPlugin())
                meter->measurer.removeClient(*trackClients[i]);
        }
    }

    if (analyzerPlugin && currentEdit) {
        analyzerPlugin->deleteFromParent();
        analyzerPlugin = nullptr;
    }

    trackClients.clear();
}

void PlaybackInfo::reattachAnalyzer()
{
    if (!currentEdit) return;
    
    // Remove old analyzer if still around
    if (analyzerPlugin) {
        analyzerPlugin->deleteFromParent();
        analyzerPlugin = nullptr;
    }
    
    // Re-insert at the end of the master's plugin list
    if (auto* master = currentEdit->getMasterTrack()) {
        te::PluginCreationInfo info { *currentEdit, juce::ValueTree("MasterAnalyzer"), true };
        analyzerPlugin = new MasterAnalyzerPlugin(info, *this);
        master->pluginList.insertPlugin(analyzerPlugin, -1, nullptr);
        DBG("PlaybackInfo: Reattached analyzer plugin to master track");
    }
}

void PlaybackInfo::processMasterBuffer(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (numSamples <= 0 || buffer.getNumChannels() == 0) return;

    const float* left = buffer.getReadPointer(0);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : left;

    // Feed mono-mixed samples into the FFT ring buffer
    for (int i = 0; i < numSamples; ++i) {
        if (fifoIndex < fftSize) {
            fifo[fifoIndex++] = (left[i] + right[i]) * 0.5f;
        }

        if (fifoIndex >= fftSize) {
            std::copy(std::begin(fifo), std::end(fifo), std::begin(fftData));
            juce::FloatVectorOperations::clear(fftData + fftSize, fftSize);
            nextFFTBlockReady = true;
            fifoIndex = 0;
        }
    }
}

//==============================================================================
// Timer callback — 30Hz UI pump
//==============================================================================

void PlaybackInfo::timerCallback()
{
    if (!webView || !currentEdit) return;

    auto tracks = te::getAudioTracks(*currentEdit);

    // ── Audio levels ──────────────────────────────────────────────
    juce::String json = "[";
    float masterL = -100.0f, masterR = -100.0f;
    int numTracks = juce::jmin((int)trackClients.size(), (int)tracks.size());

    for (int i = 0; i < numTracks; i++)
    {
        auto levelL = trackClients[i]->getAndClearAudioLevel(0);
        auto levelR = trackClients[i]->getAndClearAudioLevel(1);

        float dbL = levelL.dB;
        float dbR = levelR.dB;

        if (dbL > masterL) masterL = dbL;
        if (dbR > masterR) masterR = dbR;

        if (i > 0) json += ",";
        json += "[" + juce::String(dbL, 1) + "," + juce::String(dbR, 1) + "]";
    }

    json += ",[" + juce::String(masterL, 1) + "," + juce::String(masterR, 1) + "]]";
    webView->emitEventIfBrowserIsVisible("audioLevels", juce::var(json));

    // ── Transport position ────────────────────────────────────────
    auto& transport = currentEdit->getTransport();
    double posSeconds = transport.getPosition().inSeconds();

    auto barsBeats = currentEdit->tempoSequence.toBarsAndBeats(transport.getPosition());
    int bar = barsBeats.bars + 1;

    bool looping = transport.looping.get();
    double loopLenSeconds = 0.0;
    int loopBars = 0;
    if (looping)
    {
        auto loopRange = transport.getLoopRange();
        loopLenSeconds = loopRange.getLength().inSeconds();
        auto loopEndBB = currentEdit->tempoSequence.toBarsAndBeats(loopRange.getEnd());
        loopBars = loopEndBB.bars;
    }

    juce::String posJson = "{\"position\":" + juce::String(posSeconds, 3)
        + ",\"bar\":" + juce::String(bar)
        + ",\"looping\":" + (looping ? "true" : "false")
        + ",\"loopLength\":" + juce::String(loopLenSeconds, 2)
        + ",\"loopBars\":" + juce::String(loopBars) + "}";
    webView->emitEventIfBrowserIsVisible("transportPosition", juce::var(posJson));

    // ── Stereo analysis: width + phase + balance from L/R peak levels ──
    float linL = (masterL > -90.0f) ? std::pow(10.0f, masterL / 20.0f) : 0.0f;
    float linR = (masterR > -90.0f) ? std::pow(10.0f, masterR / 20.0f) : 0.0f;

    float lrSum  = linL + linR;
    float lrDiff = std::abs(linL - linR);
    float currentWidth       = (lrSum > 0.0001f) ? (lrDiff / lrSum) : 0.0f;
    float currentCorrelation = (lrSum > 0.0001f) ? (1.0f - lrDiff / lrSum) : 1.0f;
    float currentBalance     = (lrSum > 0.0001f) ? ((linR - linL) / lrSum) : 0.0f;

    const float alpha = 0.85f;
    stereoWidth       = stereoWidth       * alpha + currentWidth       * (1.0f - alpha);
    phaseCorrelation  = phaseCorrelation  * alpha + currentCorrelation * (1.0f - alpha);
    stereoBalance     = stereoBalance     * alpha + currentBalance     * (1.0f - alpha);

    // ── Real FFT Spectrum — 16 log-spaced bands from master output ──
    const int numUIBands = 16;
    spectrumMagnitudes.resize(numUIBands, 0.0f);

    if (nextFFTBlockReady)
    {
        nextFFTBlockReady = false;

        // Apply Hann window
        juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable(fftData, fftSize);

        // Perform FFT
        juce::dsp::FFT forwardFFT(fftOrder);
        forwardFFT.performFrequencyOnlyForwardTransform(fftData);

        // Map FFT bins to 16 logarithmically-spaced bands
        const int numBins = fftSize / 2;
        for (int band = 0; band < numUIBands; band++)
        {
            float frac0 = (float)band / (float)numUIBands;
            float frac1 = (float)(band + 1) / (float)numUIBands;
            int binStart = juce::jmax(1, (int)std::pow((float)numBins, frac0));
            int binEnd   = juce::jmax(binStart + 1, (int)std::pow((float)numBins, frac1));
            binEnd = juce::jmin(binEnd, numBins);

            float sum = 0.0f;
            int count = 0;
            for (int b = binStart; b < binEnd; b++)
            {
                sum += fftData[b];
                count++;
            }

            float avg        = (count > 0) ? (sum / count) : 0.0f;
            // The FFT values are relatively large, so we scale them back for the UI
            // A slow FFT like the user requested means we apply heavy smoothing
            float linear = avg * 0.05f; 
            float db = juce::Decibels::gainToDecibels(linear + 1e-6f);
            float normalized = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);

            // "Slow FFT" heavy smoothing
            spectrumMagnitudes[band] = spectrumMagnitudes[band] * 0.85f + normalized * 0.15f;
        }
    }

    // Build and emit stereoAnalysis payload
    juce::String spectrumJson = "[";
    for (int i = 0; i < numUIBands; ++i) {
        if (i > 0) spectrumJson += ",";
        spectrumJson += juce::String(spectrumMagnitudes[i], 3);
    }
    spectrumJson += "]";

    juce::String stereoJson = "{\"width\":"      + juce::String(stereoWidth, 3)
                            + ",\"correlation\":" + juce::String(phaseCorrelation, 3)
                            + ",\"balance\":"     + juce::String(stereoBalance, 3)
                            + ",\"spectrum\":"    + spectrumJson
                            + "}";
    webView->emitEventIfBrowserIsVisible("stereoAnalysis", juce::var(stereoJson));
}
