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
 * Events emitted:
 *   "audioLevels"        — [[dBL, dBR], ...] per track + master
 *   "transportPosition"  — { position, bar, looping, loopLength, loopBars }
 *   "stereoAnalysis"     — { width: 0-1, correlation: -1..+1 }
 */
class PlaybackInfo : public juce::Timer
{
public:
    PlaybackInfo();
    ~PlaybackInfo() override;

    void setEdit(te::Edit* edit);
    void setWebView(juce::WebBrowserComponent* wv);
    void reattachAnalyzer(); // Call after BirdLoader::populateEdit to re-insert the analyzer plugin

private:
    void attachClients();
    void detachClients();
    void timerCallback() override;

    te::Edit* currentEdit = nullptr;
    juce::WebBrowserComponent* webView = nullptr;

    // Per-track level metering clients
    std::vector<std::unique_ptr<te::LevelMeasurer::Client>> trackClients;

    // Custom Master Analyzer
    te::Plugin::Ptr analyzerPlugin;

    // Stereo analysis
    float stereoWidth = 0.0f;        // 0..1 (mono..wide)
    float phaseCorrelation = 1.0f;   // -1..+1 (out of phase..mono)
    float stereoBalance = 0.0f;      // -1..+1 (full left..full right)
    int stereoFrameCount = 0;
    
    // FFT processing
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    float fifo[fftSize] = { 0.0f };
    float fftData[fftSize * 2] = { 0.0f };
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;
    std::vector<float> spectrumMagnitudes;

public:
    void processMasterBuffer(const juce::AudioBuffer<float>& buffer, int numSamples);
};
