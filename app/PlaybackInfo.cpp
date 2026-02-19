#include "PlaybackInfo.h"
#include <cmath>
#include <memory>

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

    trackClients.clear();
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

    // ── Stereo analysis (from master L/R levels) ──────────────────
    // Convert dB back to linear amplitude for stereo analysis
    float linL = std::pow(10.0f, masterL / 20.0f);
    float linR = std::pow(10.0f, masterR / 20.0f);

    // Stereo width: how different L and R are (0 = mono, 1 = fully wide)
    float sum = linL + linR;
    float diff = std::abs(linL - linR);
    float width = (sum > 0.0001f) ? (diff / sum) : 0.0f;

    // Phase correlation: +1 = perfectly correlated (mono), 0 = unrelated, -1 = out of phase
    // Using simplified estimation from level difference
    float correlation = (sum > 0.0001f) ? (1.0f - diff / sum) : 1.0f;

    // Smooth with exponential moving average
    const float smoothing = 0.85f;
    stereoWidth = stereoWidth * smoothing + width * (1.0f - smoothing);
    phaseCorrelation = phaseCorrelation * smoothing + correlation * (1.0f - smoothing);

    juce::String stereoJson = "{\"width\":" + juce::String(stereoWidth, 3)
        + ",\"correlation\":" + juce::String(phaseCorrelation, 3) + "}";
    webView->emitEventIfBrowserIsVisible("stereoAnalysis", juce::var(stereoJson));
}
