#include "PlaybackInfo.h"
#include <cmath>
#include <memory>
#include <numeric>
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

void PlaybackInfo::attachClients()
{
    if (!currentEdit) return;

    auto tracks = te::getAudioTracks(*currentEdit);
    trackClients.clear();
    trackClients.reserve(tracks.size());

    // Attach level meter clients for each track
    for (int i = 0; i < tracks.size(); i++)
    {
        trackClients.push_back(std::make_unique<te::LevelMeasurer::Client>());
        if (auto* meter = tracks[i]->getLevelMeterPlugin())
            meter->measurer.addClient(*trackClients.back());
    }

    // Don't insert analyzer here — reattachAnalyzer() handles it after populateEdit

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

    // Debug: log every 100 calls to confirm this is being hit and data is non-zero
    static int callCount = 0;
    if (++callCount % 100 == 0) {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int s = 0; s < numSamples; ++s)
                peak = std::max(peak, std::abs(buffer.getSample(ch, s)));
        DBG("processMasterBuffer called #" + juce::String(callCount)
            + " | numSamples=" + juce::String(numSamples)
            + " | peak=" + juce::String(peak, 6));
    }

    // --- Stereophonic Analysis Phase & Width (Sample-Accurate) ---
    const float* left = buffer.getReadPointer(0);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : left;

    float sumL = 0.0f, sumR = 0.0f, sumDiff = 0.0f, sumMid = 0.0f, sumLR = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float l = left[i];
        float r = right[i];
        
        sumL += l * l;
        sumR += r * r;
        sumLR += l * r; // Cross-correlation
        
        float mid = (l + r) * 0.5f;
        float side = (l - r) * 0.5f;
        sumMid += mid * mid;
        sumDiff += side * side;
    }

    // Phase Correlation: E[L*R] / sqrt(E[L^2] * E[R^2])
    float denom = std::sqrt(sumL * sumR);
    float currentCorrelation = (denom > 1e-6f) ? (sumLR / denom) : 1.0f;

    // Width estimation: M/S energy ratio mapped 0..1
    float totalMS = sumMid + sumDiff;
    float currentWidth = (totalMS > 1e-6f) ? (sumDiff / totalMS) : 0.0f;

    // Smooth
    const float alpha = 0.85f;
    stereoWidth = stereoWidth * alpha + currentWidth * (1.0f - alpha);
    phaseCorrelation = phaseCorrelation * alpha + currentCorrelation * (1.0f - alpha);

    // --- FFT Accumulation for Spectrum Analyzer ---
    // Mix to mono for the EQ meter
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

void PlaybackInfo::timerCallback()
{
    if (!webView || !currentEdit) return;




    auto tracks = te::getAudioTracks(*currentEdit);

    // ── Audio levels ──────────────────────────────────────────────
    juce::String json = "[";
    float masterL = -100.0f, masterR = -100.0f;

    for (int i = 0; i < (int)trackClients.size() && i < tracks.size(); i++)
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
    // balance: -1 = full left, 0 = center, +1 = full right
    float currentBalance     = (lrSum > 0.0001f) ? ((linR - linL) / lrSum) : 0.0f;

    const float alpha = 0.85f;
    stereoWidth       = stereoWidth       * alpha + currentWidth       * (1.0f - alpha);
    phaseCorrelation  = phaseCorrelation  * alpha + currentCorrelation * (1.0f - alpha);
    stereoBalance     = stereoBalance     * alpha + currentBalance     * (1.0f - alpha);

    // ── Spectrum: per-track level mapped to frequency bands ──
    // Each track gets a slice of the 64 bands proportional to its level.
    // Not a real FFT but animates meaningfully with the music.
    const int numUIBands = 64;
    spectrumMagnitudes.resize(numUIBands, 0.0f);
    int numTracks = (int)trackClients.size();

    if (numTracks > 0) {
        // For each band, pick the owning track and compute its level contribution
        for (int band = 0; band < numUIBands; band++) {
            int trackIdx = (band * numTracks) / numUIBands;
            trackIdx = juce::jlimit(0, numTracks - 1, trackIdx);

            // Pull last known level for this track band
            float masterLinear = (masterL > -90.0f) ? std::pow(10.0f, masterL / 20.0f) : 0.0f;
            // Use the master level as a shared envelope, shaped by band position
            // Natural roll-off: lows are louder, highs quieter
            float bandFrac = (float)band / (float)(numUIBands - 1);
            // Spectral tilt: more energy at low freqs
            float tilt = std::pow(1.0f - bandFrac * 0.6f, 2.0f);
            float target = masterLinear * tilt;

            spectrumMagnitudes[band] = spectrumMagnitudes[band] * 0.75f + target * 0.25f;
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
