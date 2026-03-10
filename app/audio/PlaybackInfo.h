#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include <vector>
#include <atomic>

namespace te = tracktion;

class MasterAnalyzerPlugin;

/**
 * PlaybackInfo — polls audio levels, transport position, and stereo analysis
 * at ~30Hz and pushes the data to the WebView as JSON events.
 *
 * Architecture:
 *   Audio thread  → MasterAnalyzerPlugin::applyToBuffer() → lock-free ring buffer write
 *   Background    → AnalysisThread pulls from ring buffer, computes FFT + stereo
 *   Message thread → 30Hz timerCallback reads pre-computed atomic results, emits JSON
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

    // Called from MasterAnalyzerPlugin::applyToBuffer (audio thread)
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

    // MasterAnalyzerPlugin instance (inserted on master track)
    MasterAnalyzerPlugin* analyzerPlugin = nullptr;

    // ── Lock-free audio thread → background thread communication ──

    // Simple lock-free SPSC ring buffer for raw audio samples (mono-mixed)
    static constexpr int ringSize = 8192;
    float ringBuffer[ringSize] = { 0.0f };
    std::atomic<int> ringWritePos { 0 };
    std::atomic<int> ringReadPos { 0 };

    // RMS accumulators — written from audio thread, read+reset from background thread
    std::atomic<double> rmsInputSumLSq { 0.0 };
    std::atomic<double> rmsInputSumRSq { 0.0 };
    std::atomic<double> rmsInputSumLR  { 0.0 };
    std::atomic<int>    rmsInputCount  { 0 };

    // ── Background analysis thread ──

    class AnalysisThread : public juce::Thread
    {
    public:
        AnalysisThread(PlaybackInfo& owner);
        void run() override;
    private:
        PlaybackInfo& owner;
    };
    std::unique_ptr<AnalysisThread> analysisThread;

    // FFT buffers — used only by background thread
    // The actual juce::dsp::FFT and WindowingFunction objects are created
    // inside AnalysisThread::run() to avoid juce_dsp dependency in this header.
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    float fftInputBuffer[fftSize] = { 0.0f };
    float fftData[fftSize * 2] = { 0.0f };

    // ── Pre-computed results (written by background thread, read by timer) ──

    std::atomic<float> computedWidth       { 0.0f };
    std::atomic<float> computedCorrelation { 1.0f };
    std::atomic<float> computedBalance     { 0.0f };

    static constexpr int numUIBands = 16;
    float spectrumBands[numUIBands] = { 0.0f };
    std::atomic<bool> spectrumLock { false };

    bool analyzerAlive = false;
};
