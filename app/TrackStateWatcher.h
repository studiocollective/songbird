#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

/**
 * Per-track reactive state watcher.
 * 
 * Each TrackStateWatcher listens to a single track's ValueTree (and its 
 * VolumeAndPanPlugin's subtree) for property changes. On change:
 * - Sets per-property dirty flags
 * - Debounces by both time (100ms) and value delta (skip tiny oscillations)
 * - Pushes only changed properties to React via WebView
 * 
 * A `suppressEcho` flag prevents feedback loops when React originates the change.
 */
class TrackStateWatcher : private juce::ValueTree::Listener, private juce::Timer
{
public:
    TrackStateWatcher(te::AudioTrack* track, int trackIndex, 
                      juce::WebBrowserComponent* webView,
                      std::atomic<bool>& echoGuard)
        : audioTrack(track), index(trackIndex), web(webView), suppressEcho(echoGuard)
    {
        if (!track) return;
        
        // Listen to track's own state (mute, solo)
        trackState = track->state;
        trackState.addListener(this);
        
        // Listen to VolumeAndPan plugin's state (volume, pan)
        if (auto vp = track->getVolumePlugin())
        {
            volPanState = vp->state;
            volPanState.addListener(this);
        }
        
        // Initialize last-pushed values
        readCurrentState();
    }
    
    ~TrackStateWatcher() override
    {
        stopTimer();
        trackState.removeListener(this);
        volPanState.removeListener(this);
    }
    
    // Read current Tracktion state (no push, just cache)
    void readCurrentState()
    {
        if (!audioTrack) return;
        
        if (auto vp = audioTrack->getVolumePlugin())
        {
            lastVolume = juce::Decibels::decibelsToGain(vp->getVolumeDb());
            lastPan = vp->getPan();
        }
        lastMuted = audioTrack->isMuted(false);
        lastSolo = audioTrack->isSolo(false);
    }
    
    // Push current state to React only if values differ from last push
    void forcePush()
    {
        if (!audioTrack) return;
        
        float curVolume = 0.0f, curPan = 0.0f;
        bool curMuted = false, curSolo = false;
        
        if (auto vp = audioTrack->getVolumePlugin())
        {
            curVolume = juce::Decibels::decibelsToGain(vp->getVolumeDb());
            curPan = vp->getPan();
        }
        curMuted = audioTrack->isMuted(false);
        curSolo = audioTrack->isSolo(false);
        
        bool changed = std::abs(curVolume - lastVolume) > kVolumeDelta
                     || std::abs(curPan - lastPan) > kPanDelta
                     || curMuted != lastMuted
                     || curSolo != lastSolo;
        
        if (!changed) return;  // nothing to push
        
        lastVolume = curVolume;
        lastPan = curPan;
        lastMuted = curMuted;
        lastSolo = curSolo;
        pushToReact();
    }

private:
    te::AudioTrack* audioTrack;
    int index;
    juce::WebBrowserComponent* web;
    std::atomic<bool>& suppressEcho;  // shared guard set by applyMixerState
    
    juce::ValueTree trackState;
    juce::ValueTree volPanState;
    
    // Last values pushed to React (for delta debouncing)
    float lastVolume = 0.0f;
    float lastPan = 0.0f;
    bool lastMuted = false;
    bool lastSolo = false;
    
    // Minimum deltas to trigger a push (avoid sending noise)
    static constexpr float kVolumeDelta = 0.005f;   // ~0.5% change
    static constexpr float kPanDelta = 0.02f;        // ~2% change
    
    bool dirty = false;
    
    // ValueTree::Listener — fires when any property changes on watched trees
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override
    {
        if (suppressEcho.load()) return;  // React originated this change, don't echo
        
        dirty = true;
        startTimer(100);  // debounce: push in 100ms
    }
    
    // Unused but required by interface
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}
    
    void timerCallback() override
    {
        stopTimer();
        if (!dirty || !audioTrack) return;
        dirty = false;
        
        // Read current state from Tracktion
        float curVolume = 0.0f, curPan = 0.0f;
        bool curMuted = false, curSolo = false;
        
        if (auto vp = audioTrack->getVolumePlugin())
        {
            curVolume = juce::Decibels::decibelsToGain(vp->getVolumeDb());
            curPan = vp->getPan();
        }
        curMuted = audioTrack->isMuted(false);
        curSolo = audioTrack->isSolo(false);
        
        // Value-delta debounce: skip if changes are too small
        bool volumeChanged = std::abs(curVolume - lastVolume) > kVolumeDelta;
        bool panChanged = std::abs(curPan - lastPan) > kPanDelta;
        bool muteChanged = curMuted != lastMuted;
        bool soloChanged = curSolo != lastSolo;
        
        if (!volumeChanged && !panChanged && !muteChanged && !soloChanged)
            return;  // nothing meaningful changed
        
        DBG("TrackWatcher[" + juce::String(index) + "] '" + audioTrack->getName() + "': "
            + (volumeChanged ? "vol=" + juce::String(curVolume, 3) + "(was " + juce::String(lastVolume, 3) + ") " : "")
            + (panChanged ? "pan=" + juce::String(curPan, 2) + " " : "")
            + (muteChanged ? "mute=" + juce::String(curMuted ? "Y" : "N") + " " : "")
            + (soloChanged ? "solo=" + juce::String(curSolo ? "Y" : "N") + " " : "")
            + "-> pushing to React");
        
        lastVolume = curVolume;
        lastPan = curPan;
        lastMuted = curMuted;
        lastSolo = curSolo;
        
        pushToReact();
    }
    
    void pushToReact()
    {
        if (!web) return;
        
        // Build a minimal update for just this track
        auto* obj = new juce::DynamicObject();
        obj->setProperty("trackIndex", index);
        obj->setProperty("volume", juce::roundToInt(lastVolume * 127.0f));
        obj->setProperty("pan", juce::roundToInt(lastPan * 64.0f));
        obj->setProperty("muted", lastMuted);
        obj->setProperty("solo", lastSolo);
        
        auto jsonStr = juce::JSON::toString(juce::var(obj), true);
        web->emitEventIfBrowserIsVisible("trackMixerUpdate", juce::var(jsonStr));
    }
};
