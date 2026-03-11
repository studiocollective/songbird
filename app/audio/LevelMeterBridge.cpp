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
        // DISABLED: rtFrame from PlaybackInfo now handles all meter + transport data at 60Hz.
        // startTimerHz(30);
        // DBG("LevelMeterBridge: Timer started (30Hz)");
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
        // DISABLED: rtFrame from PlaybackInfo now handles all meter + transport data at 60Hz.
        // startTimerHz(30);
        DBG("LevelMeterBridge: Attached " + juce::String((int)trackClients.size()) + " clients (timer disabled — rtFrame handles meters)");
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

    // Build a compact array: [[leftDb, rightDb], ...]
    // Plus a master entry at the end
    juce::Array<juce::var> levelsArray;
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

        juce::Array<juce::var> pair;
        pair.add(juce::var(dbL));
        pair.add(juce::var(dbR));
        levelsArray.add(juce::var(pair));
    }

    // Append master as the last entry
    juce::Array<juce::var> masterPair;
    masterPair.add(juce::var(masterL));
    masterPair.add(juce::var(masterR));
    levelsArray.add(juce::var(masterPair));

    webView->emitEventIfBrowserIsVisible("audioLevels", juce::var(levelsArray));

    // Also push transport position at the same 30Hz rate
    auto& transport = currentEdit->getTransport();
    double posSeconds = transport.getPosition().inSeconds();

    // Compute current bar from the tempo sequence
    auto barsBeats = currentEdit->tempoSequence.toBarsAndBeats(transport.getPosition());
    int bar = barsBeats.bars + 1; // 1-based

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

    juce::DynamicObject::Ptr posObj = new juce::DynamicObject();
    posObj->setProperty("position", posSeconds);
    posObj->setProperty("bar", bar);
    posObj->setProperty("looping", looping);
    posObj->setProperty("loopLength", loopLenSeconds);
    posObj->setProperty("loopBars", loopBars);
    posObj->setProperty("loopStartBar", loopStartBar);

    webView->emitEventIfBrowserIsVisible("transportPosition", juce::var(posObj.get()));
}
