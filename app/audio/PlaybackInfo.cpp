#include "PlaybackInfo.h"
#include <cmath>
#include <juce_dsp/juce_dsp.h>

PlaybackInfo::PlaybackInfo()
{
    masterClient = std::make_unique<te::LevelMeasurer::Client>();
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
// Raw master buffer callback — called from audio thread via masterLevels
//==============================================================================

void PlaybackInfo::processMasterBuffer(const juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    if (numSamples <= 0 || buffer.getNumChannels() == 0) return;

    if (!analyzerAlive) {
        analyzerAlive = true;
        DBG("PlaybackInfo: masterLevels bufferCallback is ALIVE "
            "(channels=" + juce::String(buffer.getNumChannels()) +
            " samples=" + juce::String(numSamples) + ")");
    }

    const float* left = buffer.getReadPointer(0, start);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1, start) : left;

    // Accumulate RMS for stereo analysis
    for (int i = 0; i < numSamples; ++i) {
        double l = (double)left[i];
        double r = (double)right[i];
        rmsSumLSquared += l * l;
        rmsSumRSquared += r * r;
        rmsSumLR       += l * r;
    }
    rmsFrameCount += numSamples;

    // Feed mono-mixed samples into FFT ring buffer
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

    // Attach master client + buffer callback to EditPlaybackContext::masterLevels
    masterClientAttached = false;
    if (auto* context = currentEdit->getTransport().getCurrentPlaybackContext())
    {
        context->masterLevels.addClient(*masterClient);
        context->masterLevels.bufferCallback = [this](const juce::AudioBuffer<float>& buf, int s, int n) {
            processMasterBuffer(buf, s, n);
        };
        masterClientAttached = true;
        DBG("PlaybackInfo: Attached master client + bufferCallback to masterLevels");
    }

    if (webView)
    {
        startTimerHz(30);
        DBG("PlaybackInfo: Attached " + juce::String((int)trackClients.size()) + " track clients, timer started");
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

        if (masterClientAttached)
        {
            if (auto* context = currentEdit->getTransport().getCurrentPlaybackContext())
            {
                context->masterLevels.removeClient(*masterClient);
                context->masterLevels.bufferCallback = nullptr;
            }
            masterClientAttached = false;
        }
    }

    trackClients.clear();
}

//==============================================================================
// Timer callback — 30Hz UI pump
//==============================================================================

void PlaybackInfo::timerCallback()
{
    if (!webView || !currentEdit) return;

    // Try to attach master client if not yet attached (context may not exist at startup)
    if (!masterClientAttached)
    {
        if (auto* context = currentEdit->getTransport().getCurrentPlaybackContext())
        {
            context->masterLevels.addClient(*masterClient);
            context->masterLevels.bufferCallback = [this](const juce::AudioBuffer<float>& buf, int s, int n) {
                processMasterBuffer(buf, s, n);
            };
            masterClientAttached = true;
            DBG("PlaybackInfo: Late-attached master client + bufferCallback");
        }
    }

    auto tracks = te::getAudioTracks(*currentEdit);

    // ── Per-track audio levels ────────────────────────────────────
    juce::String json = "[";
    int numTracks = juce::jmin((int)trackClients.size(), (int)tracks.size());

    for (int i = 0; i < numTracks; i++)
    {
        auto levelL = trackClients[i]->getAndClearAudioLevel(0);
        auto levelR = trackClients[i]->getAndClearAudioLevel(1);

        if (i > 0) json += ",";
        json += "[" + juce::String(levelL.dB, 1) + "," + juce::String(levelR.dB, 1) + "]";
    }

    // ── Master levels from EditPlaybackContext::masterLevels ──────
    float masterL = -100.0f, masterR = -100.0f;
    if (masterClientAttached)
    {
        auto ml = masterClient->getAndClearAudioLevel(0);
        auto mr = masterClient->getAndClearAudioLevel(1);
        masterL = ml.dB;
        masterR = mr.dB;
    }
    else
    {
        // Fallback: aggregate from track levels
        for (int i = 0; i < numTracks; i++)
        {
            auto ll = trackClients[i]->getAndClearAudioLevel(0);
            auto lr = trackClients[i]->getAndClearAudioLevel(1);
            if (ll.dB > masterL) masterL = ll.dB;
            if (lr.dB > masterR) masterR = lr.dB;
        }
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
    int loopStartBar = 0;
    if (looping)
    {
        auto loopRange = transport.getLoopRange();
        loopLenSeconds = loopRange.getLength().inSeconds();
        auto loopStartBB = currentEdit->tempoSequence.toBarsAndBeats(loopRange.getStart());
        loopStartBar = loopStartBB.bars;
        auto loopEndBB = currentEdit->tempoSequence.toBarsAndBeats(loopRange.getEnd());
        loopBars = loopEndBB.bars;
    }

    juce::String posJson = "{\"position\":" + juce::String(posSeconds, 3)
        + ",\"bar\":" + juce::String(bar)
        + ",\"looping\":" + (looping ? "true" : "false")
        + ",\"loopLength\":" + juce::String(loopLenSeconds, 2)
        + ",\"loopBars\":" + juce::String(loopBars)
        + ",\"loopStartBar\":" + juce::String(loopStartBar) + "}";
    webView->emitEventIfBrowserIsVisible("transportPosition", juce::var(posJson));

    // ── Stereo analysis: RMS-based from raw master buffer ────────
    float currentWidth = 0.0f;
    float currentCorrelation = 1.0f;
    float currentBalance = 0.0f;

    if (rmsFrameCount > 0)
    {
        double rmsL = std::sqrt(rmsSumLSquared / rmsFrameCount);
        double rmsR = std::sqrt(rmsSumRSquared / rmsFrameCount);
        double crossCorr = rmsSumLR / rmsFrameCount;

        double rmsProduct = rmsL * rmsR;
        if (rmsProduct > 1e-10)
            currentCorrelation = (float)juce::jlimit(-1.0, 1.0, crossCorr / rmsProduct);

        float rawWidth = juce::jlimit(0.0f, 1.0f, 1.0f - currentCorrelation);
        currentWidth = std::sqrt(rawWidth);

        double rmsSum = rmsL + rmsR;
        if (rmsSum > 1e-10)
            currentBalance = (float)((rmsR - rmsL) / rmsSum);

        rmsSumLSquared = 0.0;
        rmsSumRSquared = 0.0;
        rmsSumLR = 0.0;
        rmsFrameCount = 0;
    }
    else
    {
        // Fallback: peak dB
        float linL = (masterL > -90.0f) ? std::pow(10.0f, masterL / 20.0f) : 0.0f;
        float linR = (masterR > -90.0f) ? std::pow(10.0f, masterR / 20.0f) : 0.0f;
        float lrSum  = linL + linR;
        float lrDiff = std::abs(linL - linR);
        currentWidth       = (lrSum > 0.0001f) ? std::sqrt(lrDiff / lrSum) : 0.0f;
        currentCorrelation = (lrSum > 0.0001f) ? (1.0f - lrDiff / lrSum) : 1.0f;
        currentBalance     = (lrSum > 0.0001f) ? ((linR - linL) / lrSum) : 0.0f;
    }

    const float alphaSlowSmooth = 0.993f; // width + balance (~5s average)
    const float alphaFastSmooth = 0.80f;  // phase correlation (~150ms, responsive)
    stereoWidth       = stereoWidth       * alphaSlowSmooth + currentWidth       * (1.0f - alphaSlowSmooth);
    phaseCorrelation  = phaseCorrelation  * alphaFastSmooth + currentCorrelation * (1.0f - alphaFastSmooth);
    stereoBalance     = stereoBalance     * alphaSlowSmooth + currentBalance     * (1.0f - alphaSlowSmooth);

    // ── Real FFT Spectrum — 16 log-spaced bands from master output ──
    const int numUIBands = 16;
    spectrumMagnitudes.resize(numUIBands, 0.0f);

    if (nextFFTBlockReady)
    {
        nextFFTBlockReady = false;

        juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable(fftData, fftSize);

        juce::dsp::FFT forwardFFT(fftOrder);
        forwardFFT.performFrequencyOnlyForwardTransform(fftData);

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

            float avg = (count > 0) ? (sum / count) : 0.0f;
            float linear = avg * 0.05f;
            float db = juce::Decibels::gainToDecibels(linear + 1e-6f);
            float normalized = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);

            spectrumMagnitudes[band] = spectrumMagnitudes[band] * 0.85f + normalized * 0.15f;
        }
    }

    // Build and emit stereoAnalysis payload as a DynamicObject
    juce::Array<juce::var> spectrumArray;
    for (int i = 0; i < numUIBands; ++i) {
        spectrumArray.add(juce::var(spectrumMagnitudes[i]));
    }

    juce::DynamicObject::Ptr stereoObj = new juce::DynamicObject();
    stereoObj->setProperty("width", stereoWidth);
    stereoObj->setProperty("correlation", phaseCorrelation);
    stereoObj->setProperty("balance", stereoBalance);
    stereoObj->setProperty("spectrum", spectrumArray);

    webView->emitEventIfBrowserIsVisible("stereoAnalysis", juce::var(stereoObj.get()));
}
