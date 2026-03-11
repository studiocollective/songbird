#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>
#include "PlaybackInfo.h"

namespace te = tracktion;

/**
 * Lightweight real‑time dropout detector + CPU meter.
 *
 * Registers as an AudioIODeviceCallback on the JUCE DeviceManager so it runs
 * inside the CoreAudio thread.  It measures:
 *  - Gap between successive callbacks (should be ≈ bufferSize/sampleRate).
 *
 * A 4Hz Timer on the message thread:
 *  - Emits "dropoutDetected" when timing gaps exceed threshold
 *  - Emits "cpuStats" with overall CPU usage for the top-bar meter
 */
class DropoutDetector : public juce::AudioIODeviceCallback,
                         private juce::Timer
{
public:
    DropoutDetector() = default;
    ~DropoutDetector() override { stopTimer(); }

    /** Call once after the device manager is initialised. */
    void start(juce::AudioDeviceManager& dm, juce::WebBrowserComponent* webView,
               te::Engine* eng = nullptr)
    {
        deviceManager = &dm;
        browser = webView;
        tracktionEngine = eng;
        expectedPeriodMs = 0.0;
        lastCallbackTime.store(0.0);
        totalXruns.store(0);
        worstGapMs.store(0.0);
        lastXrunCount = dm.getXRunCount();

        dm.addAudioCallback(this);
        startTimerHz(4); // poll 4× per second
    }

    void setEdit(te::Edit* e) { currentEdit = e; }
    void setPlaybackInfo(PlaybackInfo* pi) { playbackInfoPtr = pi; }

    void stop()
    {
        stopTimer();
        if (deviceManager)
            deviceManager->removeAudioCallback(this);
    }

    //==========================================================================
    // AudioIODeviceCallback — runs on the real‑time audio thread
    //==========================================================================
    void audioDeviceIOCallbackWithContext(const float* const* /*in*/,
                                          int /*numIn*/,
                                          float* const* /*out*/,
                                          int /*numOut*/,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        double now = juce::Time::getMillisecondCounterHiRes();
        double prev = lastCallbackTime.exchange(now);

        if (prev > 0.0)
        {
            double gapMs = now - prev;
            if (gapMs > expectedPeriodMs * 1.5 && expectedPeriodMs > 0.0)
            {
                totalXruns.fetch_add(1);
                double current = worstGapMs.load();
                while (gapMs > current && !worstGapMs.compare_exchange_weak(current, gapMs)) {}
            }
        }

        (void)numSamples;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        if (device)
        {
            double sr = device->getCurrentSampleRate();
            int bs = device->getCurrentBufferSizeSamples();
            expectedPeriodMs = (bs > 0 && sr > 0) ? (1000.0 * bs / sr) : 0.0;
            bufferSize = bs;
            sampleRate = sr;
            DBG("DropoutDetector: started — buffer=" + juce::String(bs)
                + " sr=" + juce::String(sr, 0)
                + " period=" + juce::String(expectedPeriodMs, 2) + "ms");
        }
    }

    void audioDeviceStopped() override
    {
        DBG("DropoutDetector: device stopped");
    }

private:
    void timerCallback() override
    {
        if (!deviceManager || !browser) return;

        // --- Dropout detection ---
        int currentXruns = deviceManager->getXRunCount();
        int newXruns = currentXruns - lastXrunCount;
        lastXrunCount = currentXruns;

        int detected = totalXruns.exchange(0);
        double gap = worstGapMs.exchange(0.0);

        if (detected > 0 || newXruns > 0)
        {
            juce::String msg = "DROPOUT: " + juce::String(detected) + " timing gap(s)"
                + ", worst=" + juce::String(gap, 1) + "ms"
                + " (expected=" + juce::String(expectedPeriodMs, 1) + "ms)"
                + ", xruns=" + juce::String(newXruns);
            DBG(msg);

            auto* obj = new juce::DynamicObject();
            obj->setProperty("timingGaps", detected);
            obj->setProperty("worstGapMs", gap);
            obj->setProperty("expectedMs", expectedPeriodMs);
            obj->setProperty("xruns", newXruns);
            obj->setProperty("message", msg);
            browser->emitEventIfBrowserIsVisible("dropoutDetected", juce::var(obj));
        }

        // --- CPU stats: feed into PlaybackInfo for batched rtFrame emission ---
        double cpuPct = deviceManager->getCpuUsage() * 100.0;

        if (playbackInfoPtr)
        {
            playbackInfoPtr->cpuPercent.store(cpuPct, std::memory_order_relaxed);
            playbackInfoPtr->cpuBufferSize.store(bufferSize, std::memory_order_relaxed);
            playbackInfoPtr->cpuSampleRate.store(sampleRate, std::memory_order_relaxed);
        }
    }

    juce::AudioDeviceManager* deviceManager = nullptr;
    juce::WebBrowserComponent* browser = nullptr;
    te::Engine* tracktionEngine = nullptr;
    te::Edit* currentEdit = nullptr;
    PlaybackInfo* playbackInfoPtr = nullptr;
    double expectedPeriodMs = 0.0;
    int bufferSize = 0;
    double sampleRate = 0.0;
    int lastXrunCount = 0;

    // Atomic state updated from the real‑time audio thread
    std::atomic<double> lastCallbackTime { 0.0 };
    std::atomic<int> totalXruns { 0 };
    std::atomic<double> worstGapMs { 0.0 };
};

