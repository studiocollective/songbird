#include "LevelMeterBridge.h"
#include <memory>

LevelMeterBridge::LevelMeterBridge()
{
}

LevelMeterBridge::~LevelMeterBridge()
{
    stopTimer();
    detachClients();
}

void LevelMeterBridge::setEdit(te::Edit* edit)
{
    detachClients();
    currentEdit = edit;
    if (currentEdit)
        attachClients();
}

void LevelMeterBridge::setWebView(juce::WebBrowserComponent* wv)
{
    webView = wv;
    if (webView && currentEdit && !isTimerRunning())
    {
        startTimerHz(30);
        DBG("LevelMeterBridge: Timer started (30Hz)");
    }
}

void LevelMeterBridge::attachClients()
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

    // Master output
    if (auto master = currentEdit->getMasterVolumePlugin())
    {
        // The master volume plugin's output goes through the edit's
        // master level measurer, but we can use the edit's output device
        // level instead. For now, use the aggregate of track meters.
    }

    if (webView)
    {
        startTimerHz(30);
        DBG("LevelMeterBridge: Attached " + juce::String((int)trackClients.size()) + " clients, timer started");
    }
    else
    {
        DBG("LevelMeterBridge: Attached " + juce::String((int)trackClients.size()) + " clients (no webview yet)");
    }
}

void LevelMeterBridge::detachClients()
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

void LevelMeterBridge::timerCallback()
{
    if (!webView || !currentEdit) return;

    auto tracks = te::getAudioTracks(*currentEdit);

    // Build a compact JSON array: [[leftDb, rightDb], ...]
    // Plus a master entry at the end
    juce::String json = "[";
    float masterL = -100.0f, masterR = -100.0f;

    for (int i = 0; i < (int)trackClients.size() && i < tracks.size(); i++)
    {
        auto levelL = trackClients[i]->getAndClearAudioLevel(0);
        auto levelR = trackClients[i]->getAndClearAudioLevel(1);

        float dbL = levelL.dB;
        float dbR = levelR.dB;

        // Track the max for master
        if (dbL > masterL) masterL = dbL;
        if (dbR > masterR) masterR = dbR;

        if (i > 0) json += ",";
        json += "[" + juce::String(dbL, 1) + "," + juce::String(dbR, 1) + "]";
    }

    // Append master as the last entry
    json += ",[" + juce::String(masterL, 1) + "," + juce::String(masterR, 1) + "]]";

    webView->emitEventIfBrowserIsVisible("audioLevels", juce::var(json));
}
