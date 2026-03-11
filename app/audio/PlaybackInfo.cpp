#include "PlaybackInfo.h"
#include "MasterAnalyzerPlugin.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

//==============================================================================
// Construction / Destruction
//==============================================================================

PlaybackInfo::PlaybackInfo()
{
    masterClient = std::make_unique<te::LevelMeasurer::Client>();
    // Pre-allocate so the cached string never mallocs during steady-state playback
    cachedJsonString.preallocateBytes(jsonBufSize);
}

PlaybackInfo::~PlaybackInfo()
{
    stopTimer();
    if (analysisThread)
    {
        analysisThread->signalThreadShouldExit();
        analysisThread->waitForThreadToExit(500);
    }
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
        DBG("PlaybackInfo: Timer started (30Hz — JS-side ballistic smoothing handles visual 60Hz)");
    }
}

//==============================================================================
// Audio thread callback — MUST be ultra-fast, lock-free
//==============================================================================

void PlaybackInfo::processMasterBuffer(const juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    if (numSamples <= 0 || buffer.getNumChannels() == 0) return;

    if (!analyzerAlive) {
        analyzerAlive = true;
        DBG("PlaybackInfo: MasterAnalyzerPlugin is ALIVE "
            "(channels=" + juce::String(buffer.getNumChannels()) +
            " samples=" + juce::String(numSamples) + ")");
    }

    const float* left = buffer.getReadPointer(0, start);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1, start) : left;

    // Accumulate RMS for stereo analysis (lock-free relaxed atomics)
    double sumLSq = 0.0, sumRSq = 0.0, sumLR = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        double l = (double)left[i];
        double r = (double)right[i];
        sumLSq += l * l;
        sumRSq += r * r;
        sumLR  += l * r;
    }
    rmsInputSumLSq.store(rmsInputSumLSq.load(std::memory_order_relaxed) + sumLSq, std::memory_order_relaxed);
    rmsInputSumRSq.store(rmsInputSumRSq.load(std::memory_order_relaxed) + sumRSq, std::memory_order_relaxed);
    rmsInputSumLR.store(rmsInputSumLR.load(std::memory_order_relaxed) + sumLR, std::memory_order_relaxed);
    rmsInputCount.store(rmsInputCount.load(std::memory_order_relaxed) + numSamples, std::memory_order_relaxed);

    // Write mono-mixed samples into lock-free ring buffer for FFT
    int wp = ringWritePos.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i) {
        ringBuffer[wp] = (left[i] + right[i]) * 0.5f;
        wp = (wp + 1) & (ringSize - 1);
    }
    ringWritePos.store(wp, std::memory_order_release);
}

//==============================================================================
// Background analysis thread — does FFT + stereo computation + JSON building
//==============================================================================

PlaybackInfo::AnalysisThread::AnalysisThread(PlaybackInfo& o)
    : juce::Thread("AudioAnalysis"), owner(o) {}

void PlaybackInfo::AnalysisThread::run()
{
    int localFifoIdx = 0;

    // Create FFT objects once at thread start (not per-callback!)
    juce::dsp::FFT forwardFFT(fftOrder);
    juce::dsp::WindowingFunction<float> windowFunc((size_t)fftSize, juce::dsp::WindowingFunction<float>::hann);

    while (!threadShouldExit())
    {
        wait(33);

        // ── Pull samples from ring buffer into FFT input ──
        int rp = owner.ringReadPos.load(std::memory_order_relaxed);
        int wp = owner.ringWritePos.load(std::memory_order_acquire);

        while (rp != wp)
        {
            owner.fftInputBuffer[localFifoIdx++] = owner.ringBuffer[rp];
            rp = (rp + 1) & (ringSize - 1);

            if (localFifoIdx >= fftSize)
            {
                std::copy(owner.fftInputBuffer, owner.fftInputBuffer + fftSize, owner.fftData);
                juce::FloatVectorOperations::clear(owner.fftData + fftSize, fftSize);

                windowFunc.multiplyWithWindowingTable(owner.fftData, fftSize);
                forwardFFT.performFrequencyOnlyForwardTransform(owner.fftData);

                const int numBins = fftSize / 2;
                float localBands[numUIBands];

                bool expected = false;
                while (!owner.spectrumLock.compare_exchange_weak(expected, true, std::memory_order_acquire))
                    expected = false;
                for (int i = 0; i < numUIBands; ++i)
                    localBands[i] = owner.spectrumBands[i];
                owner.spectrumLock.store(false, std::memory_order_release);

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
                        sum += owner.fftData[b];
                        count++;
                    }

                    float avg = (count > 0) ? (sum / count) : 0.0f;
                    float linear = avg * 0.05f;
                    float db = juce::Decibels::gainToDecibels(linear + 1e-6f);
                    float normalized = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
                    localBands[band] = localBands[band] * 0.85f + normalized * 0.15f;
                }

                expected = false;
                while (!owner.spectrumLock.compare_exchange_weak(expected, true, std::memory_order_acquire))
                    expected = false;
                for (int i = 0; i < numUIBands; ++i)
                    owner.spectrumBands[i] = localBands[i];
                owner.spectrumLock.store(false, std::memory_order_release);

                localFifoIdx = 0;
            }
        }
        owner.ringReadPos.store(rp, std::memory_order_relaxed);

        // ── Stereo analysis from RMS accumulators ──
        int count = owner.rmsInputCount.exchange(0, std::memory_order_relaxed);
        if (count > 0)
        {
            double sumLSq = owner.rmsInputSumLSq.exchange(0.0, std::memory_order_relaxed);
            double sumRSq = owner.rmsInputSumRSq.exchange(0.0, std::memory_order_relaxed);
            double sumLR  = owner.rmsInputSumLR.exchange(0.0, std::memory_order_relaxed);

            double rmsL = std::sqrt(sumLSq / count);
            double rmsR = std::sqrt(sumRSq / count);
            double crossCorr = sumLR / count;

            float correlation = 1.0f;
            double rmsProduct = rmsL * rmsR;
            if (rmsProduct > 1e-10)
                correlation = (float)juce::jlimit(-1.0, 1.0, crossCorr / rmsProduct);

            float rawWidth = juce::jlimit(0.0f, 1.0f, 1.0f - correlation);
            float width = std::sqrt(rawWidth);

            float balance = 0.0f;
            double rmsSum = rmsL + rmsR;
            if (rmsSum > 1e-10)
                balance = (float)((rmsR - rmsL) / rmsSum);

            const float alphaSlow = 0.993f;
            const float alphaFast = 0.80f;
            float prevWidth = owner.computedWidth.load(std::memory_order_relaxed);
            float prevCorr  = owner.computedCorrelation.load(std::memory_order_relaxed);
            float prevBal   = owner.computedBalance.load(std::memory_order_relaxed);

            owner.computedWidth.store(prevWidth * alphaSlow + width * (1.0f - alphaSlow), std::memory_order_relaxed);
            owner.computedCorrelation.store(prevCorr * alphaFast + correlation * (1.0f - alphaFast), std::memory_order_relaxed);
            owner.computedBalance.store(prevBal * alphaSlow + balance * (1.0f - alphaSlow), std::memory_order_relaxed);
        }

        // ═══════════════════════════════════════════════════════════════════
        // Build the full JSON payload using snprintf (ZERO heap allocations)
        // Read the latest level snapshot from the timer thread
        // ═══════════════════════════════════════════════════════════════════

        LevelSnapshot* snap = owner.levelSnapReady.load(std::memory_order_acquire);
        if (!snap) continue; // no data yet

        char* buf = owner.jsonWriteBuf;
        int pos = 0;
        int remaining = jsonBufSize - 1;

        // ── Levels ──
        pos += snprintf(buf + pos, remaining - pos, "{\"levels\":[");

        for (int i = 0; i < snap->numTracks; i++)
        {
            if (i > 0) pos += snprintf(buf + pos, remaining - pos, ",");
            pos += snprintf(buf + pos, remaining - pos, "[%.1f,%.1f]",
                            (double)snap->trackL[i], (double)snap->trackR[i]);
        }

        pos += snprintf(buf + pos, remaining - pos, ",[%.1f,%.1f]]",
                        (double)snap->masterL, (double)snap->masterR);

        // ── Transport ──
        pos += snprintf(buf + pos, remaining - pos,
            ",\"transport\":{\"position\":%.3f,\"bar\":%d,\"looping\":%s,\"loopLength\":%.2f,\"loopBars\":%d,\"loopStartBar\":%d}",
            snap->posSeconds, snap->bar,
            snap->looping ? "true" : "false",
            snap->loopLenSeconds, snap->loopBars, snap->loopStartBar);

        // ── Stereo analysis ──
        float width2 = owner.computedWidth.load(std::memory_order_relaxed);
        float corr2 = owner.computedCorrelation.load(std::memory_order_relaxed);
        float bal2 = owner.computedBalance.load(std::memory_order_relaxed);

        float localBands2[numUIBands];
        {
            bool exp2 = false;
            while (!owner.spectrumLock.compare_exchange_weak(exp2, true, std::memory_order_acquire))
                exp2 = false;
            std::copy(owner.spectrumBands, owner.spectrumBands + numUIBands, localBands2);
            owner.spectrumLock.store(false, std::memory_order_release);
        }

        pos += snprintf(buf + pos, remaining - pos,
            ",\"stereo\":{\"width\":%.4f,\"correlation\":%.4f,\"balance\":%.4f,\"spectrum\":[",
            (double)width2, (double)corr2, (double)bal2);

        for (int i = 0; i < numUIBands; ++i)
        {
            if (i > 0) pos += snprintf(buf + pos, remaining - pos, ",");
            pos += snprintf(buf + pos, remaining - pos, "%.4f", (double)localBands2[i]);
        }
        pos += snprintf(buf + pos, remaining - pos, "]}");

        // ── CPU stats ──
        pos += snprintf(buf + pos, remaining - pos,
            ",\"cpu\":{\"cpu\":%.1f,\"bufferSize\":%d,\"sampleRate\":%.0f}}",
            owner.cpuPercent.load(std::memory_order_relaxed),
            owner.cpuBufferSize.load(std::memory_order_relaxed),
            owner.cpuSampleRate.load(std::memory_order_relaxed));

        buf[pos] = '\0';

        // Publish: swap the ready pointer to this buffer
        owner.readyJson.store(buf, std::memory_order_release);

        // Switch to the other write buffer for next iteration
        owner.jsonWriteBuf = (buf == owner.jsonBufA) ? owner.jsonBufB : owner.jsonBufA;
    }
}

//==============================================================================
// Client attachment — uses MasterAnalyzerPlugin instead of bufferCallback
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

    // Attach master level client
    masterClientAttached = false;
    if (auto* context = currentEdit->getTransport().getCurrentPlaybackContext())
    {
        context->masterLevels.addClient(*masterClient);
        masterClientAttached = true;
        DBG("PlaybackInfo: Attached master level client");
    }

    // Try to attach MasterAnalyzerPlugin on the master track for raw audio tap
    // (used for spectrum + stereo analysis). Falls back gracefully if not possible.
    analyzerPlugin = nullptr;
    if (auto* masterTrack = currentEdit->getMasterTrack())
    {
        // Check if one already exists
        for (auto* plugin : masterTrack->pluginList.getPlugins())
        {
            if (auto* ap = dynamic_cast<MasterAnalyzerPlugin*>(plugin))
            {
                analyzerPlugin = ap;
                break;
            }
        }

        // If not, insert via ValueTree
        if (!analyzerPlugin)
        {
            auto vt = MasterAnalyzerPlugin::create();
            masterTrack->pluginList.state.appendChild(vt, &currentEdit->getUndoManager());

            // Find the newly created plugin
            for (auto* plugin : masterTrack->pluginList.getPlugins())
            {
                if (auto* ap = dynamic_cast<MasterAnalyzerPlugin*>(plugin))
                {
                    analyzerPlugin = ap;
                    break;
                }
            }
        }

        if (analyzerPlugin)
        {
            analyzerPlugin->onBuffer = [this](const juce::AudioBuffer<float>& buf, int s, int n) {
                processMasterBuffer(buf, s, n);
            };
            // Start silent to suppress init transients, fade in after plugins settle
            analyzerPlugin->goSilent();
            juce::Timer::callAfterDelay(200, [this]() {
                if (analyzerPlugin)
                    analyzerPlugin->requestFadeIn();
            });
            DBG("PlaybackInfo: MasterAnalyzerPlugin attached (silent → fade-in in 200ms)");
        }
        else
        {
            DBG("PlaybackInfo: MasterAnalyzerPlugin not available — spectrum/stereo analysis disabled");
        }
    }

    // Start background analysis thread
    if (!analysisThread)
    {
        analysisThread = std::make_unique<AnalysisThread>(*this);
        analysisThread->startThread(juce::Thread::Priority::low);
        DBG("PlaybackInfo: Background analysis thread started");
    }

    if (webView)
    {
        startTimerHz(30);
        DBG("PlaybackInfo: Attached " + juce::String((int)trackClients.size()) + " track clients, timer started (30Hz)");
    }
}

void PlaybackInfo::detachClients()
{
    stopTimer();

    if (analysisThread)
    {
        analysisThread->signalThreadShouldExit();
        analysisThread->waitForThreadToExit(500);
        analysisThread.reset();
    }

    if (analyzerPlugin)
    {
        analyzerPlugin->onBuffer = nullptr;
        analyzerPlugin = nullptr;
    }

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
                context->masterLevels.removeClient(*masterClient);
            masterClientAttached = false;
        }
    }

    trackClients.clear();
}

//==============================================================================
// Timer callback — 30Hz: snapshot levels → async emit pre-built JSON
// JS-side rAF + ballistic smoothing provides visual 60Hz
//==============================================================================

void PlaybackInfo::timerCallback()
{
    if (!webView || !currentEdit) return;

    // Try to attach master client if not yet attached
    if (!masterClientAttached)
    {
        if (auto* context = currentEdit->getTransport().getCurrentPlaybackContext())
        {
            context->masterLevels.addClient(*masterClient);
            masterClientAttached = true;

            if (!analysisThread)
            {
                analysisThread = std::make_unique<AnalysisThread>(*this);
                analysisThread->startThread(juce::Thread::Priority::low);
            }
            DBG("PlaybackInfo: Late-attached master client");
        }
    }

    // ── Snapshot levels + transport into the write buffer (fast atomic reads) ──
    {
        LevelSnapshot* snap = levelSnapWriting;
        // Use cached trackClients.size() — no ValueTree iteration needed!
        int numTracks = juce::jmin((int)trackClients.size(), maxTracks);
        snap->numTracks = numTracks;

        for (int i = 0; i < numTracks; i++)
        {
            auto levelL = trackClients[i]->getAndClearAudioLevel(0);
            auto levelR = trackClients[i]->getAndClearAudioLevel(1);
            snap->trackL[i] = levelL.dB;
            snap->trackR[i] = levelR.dB;
        }

        if (masterClientAttached)
        {
            auto ml = masterClient->getAndClearAudioLevel(0);
            auto mr = masterClient->getAndClearAudioLevel(1);
            snap->masterL = ml.dB;
            snap->masterR = mr.dB;
        }
        else
        {
            snap->masterL = -100.0f;
            snap->masterR = -100.0f;
            for (int i = 0; i < numTracks; i++)
            {
                if (snap->trackL[i] > snap->masterL) snap->masterL = snap->trackL[i];
                if (snap->trackR[i] > snap->masterR) snap->masterR = snap->trackR[i];
            }
        }

        // Transport position
        auto& transport = currentEdit->getTransport();
        snap->posSeconds = transport.getPosition().inSeconds();
        auto barsBeats = currentEdit->tempoSequence.toBarsAndBeats(transport.getPosition());
        snap->bar = barsBeats.bars + 1;

        snap->looping = transport.looping.get();
        if (snap->looping)
        {
            auto loopRange = transport.getLoopRange();
            snap->loopLenSeconds = loopRange.getLength().inSeconds();
            auto loopStartBB = currentEdit->tempoSequence.toBarsAndBeats(loopRange.getStart());
            snap->loopStartBar = loopStartBB.bars;
            auto loopEndBB = currentEdit->tempoSequence.toBarsAndBeats(loopRange.getEnd());
            snap->loopBars = loopEndBB.bars;
        }
        else
        {
            snap->loopLenSeconds = 0.0;
            snap->loopBars = 0;
            snap->loopStartBar = 0;
        }

        // Publish snapshot — swap to the other buffer
        levelSnapReady.store(snap, std::memory_order_release);
        levelSnapWriting = (snap == &levelSnapA) ? &levelSnapB : &levelSnapA;
    }

    // ── Emit the pre-built JSON synchronously (30Hz is lightweight) ──
    const char* json = readyJson.load(std::memory_order_acquire);
    if (json && json[0] != '\0')
    {
        cachedJsonString = juce::String(juce::CharPointer_UTF8(json));
        if (webView) webView->emitEventIfBrowserIsVisible("rtFrame", juce::var(cachedJsonString));
    }
}

