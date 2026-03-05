#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include <vector>

namespace te = tracktion;

/**
 * PlaybackInfo — polls audio levels, transport position, and stereo analysis
 * at ~30Hz and pushes the data to the WebView as JSON events.
 *
 * Uses per-track LevelMeasurer::Client for track levels, and
 * EditPlaybackContext::masterLevels for true master output levels + raw FFT.
 *
 * Events emitted:
 *   "audioLevels"        — [[dBL, dBR], ...] per track + master
 *   "transportPosition"  — { position, bar, looping, loopLength, loopBars }
 *   "stereoAnalysis"     — { width, correlation, balance, spectrum: [...] }
 */
class PlaybackInfo : public juce::Timer
{
public:
    PlaybackInfo();
    ~PlaybackInfo() override;

    void setEdit(te::Edit* edit);
    void setWebView(juce::WebBrowserComponent* wv);

    // Called from masterLevels.bufferCallback with raw master audio
    void processMasterBuffer(const juce::AudioBuffer<float>& buffer, int start, int numSamples);

private:
    void attachClients();
    void detachClients();
    void timerCallback() override;

    te::Edit* currentEdit = nullptr;
    juce::WebBrowserComponent* webView = nullptr;

    // Per-track level metering clients
    std::vector<std::unique_ptr<te::LevelMeasurer::Client>> trackClients;

    // Master output level client (from EditPlaybackContext::masterLevels)
    std::unique_ptr<te::LevelMeasurer::Client> masterClient;
    bool masterClientAttached = false;

    // Stereo analysis (smoothed values emitted to UI)
    float stereoWidth = 0.0f;
    float phaseCorrelation = 1.0f;
    float stereoBalance = 0.0f;

    // RMS accumulation for stereo analysis (fed from raw master buffer)
    double rmsSumLSquared = 0.0;
    double rmsSumRSquared = 0.0;
    double rmsSumLR = 0.0;
    int    rmsFrameCount = 0;
    bool   analyzerAlive = false;

    // FFT processing
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    float fifo[fftSize] = { 0.0f };
    float fftData[fftSize * 2] = { 0.0f };
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;

    // Spectrum — 16 bands derived from real FFT
    std::vector<float> spectrumMagnitudes;
};
