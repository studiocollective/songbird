#include "SongbirdEditor.h"

//==============================================================================
// State management — stores state as JSON keyed by store name
//==============================================================================

juce::String SongbirdEditor::describeMixerChange(const juce::String& oldJson, const juce::String& newJson)
{
    if (oldJson.isEmpty()) return "Mixer initialized";

    auto oldParsed = juce::JSON::parse(oldJson);
    auto newParsed = juce::JSON::parse(newJson);
    auto oldState = oldParsed.getProperty("state", {});
    auto newState = newParsed.getProperty("state", {});
    if (!oldState.isObject() || !newState.isObject()) return "Mixer state change";

    juce::StringArray changes;

    // Check top-level properties
    for (auto& propName : juce::StringArray{"mixerOpen", "returnsOpen"})
    {
        auto oldVal = oldState.getProperty(juce::Identifier(propName), {});
        auto newVal = newState.getProperty(juce::Identifier(propName), {});
        if (oldVal != newVal)
            changes.add(propName + " -> " + newVal.toString());
    }

    auto* oldTracks = oldState.getProperty("tracks", {}).getArray();
    auto* newTracks = newState.getProperty("tracks", {}).getArray();

    if (oldTracks && newTracks)
    {
        if (oldTracks->size() != newTracks->size())
            changes.add("tracks " + juce::String(oldTracks->size()) + " -> " + juce::String(newTracks->size()));

        int count = juce::jmin(oldTracks->size(), newTracks->size());
        for (int i = 0; i < count && changes.size() < 4; ++i)
        {
            auto oldT = (*oldTracks)[i];
            auto newT = (*newTracks)[i];
            juce::String name = newT.getProperty("name", "Track " + juce::String(i));

            int oldVol = (int)oldT.getProperty("volume", 80);
            int newVol = (int)newT.getProperty("volume", 80);
            if (oldVol != newVol)
                changes.add("'" + name + "' vol " + juce::String(oldVol) + "->" + juce::String(newVol));

            int oldPan = (int)oldT.getProperty("pan", 0);
            int newPan = (int)newT.getProperty("pan", 0);
            if (oldPan != newPan)
                changes.add("'" + name + "' pan " + juce::String(newPan));

            bool oldMute = (bool)oldT.getProperty("muted", false);
            bool newMute = (bool)newT.getProperty("muted", false);
            if (oldMute != newMute)
                changes.add("'" + name + "' " + juce::String(newMute ? "muted" : "unmuted"));

            bool oldSolo = (bool)oldT.getProperty("solo", false);
            bool newSolo = (bool)newT.getProperty("solo", false);
            if (oldSolo != newSolo)
                changes.add("'" + name + "' " + juce::String(newSolo ? "solo on" : "solo off"));

            // Sends
            auto* oldSends = oldT.getProperty("sends", {}).getArray();
            auto* newSends = newT.getProperty("sends", {}).getArray();
            if (oldSends && newSends)
            {
                for (int s = 0; s < juce::jmin(oldSends->size(), newSends->size()); ++s)
                {
                    if ((*oldSends)[s] != (*newSends)[s])
                        changes.add("'" + name + "' send" + juce::String(s));
                }
            }
        }
    }

    if (changes.isEmpty())
    {
        DBG("StateSync: describeMixerChange found no tracked diffs — JSON strings differ by content");
        return "Mixer update";
    }
    return changes.joinIntoString(", ");
}

void SongbirdEditor::handleStateUpdate(const juce::String& storeName, const juce::String& jsonValue)
{
    // Skip if value is identical to what's already cached (echo suppression)
    auto it = stateCache.find(storeName);
    if (it != stateCache.end() && it->second == jsonValue)
        return;

    // Save previous state for diff before updating cache
    juce::String prevMixerJson;
    if (storeName == "songbird-mixer" && it != stateCache.end())
        prevMixerJson = it->second;

    // Store the state for later retrieval
    stateCache[storeName] = jsonValue;

    // Only mixer state participates in undo/redo (git-tracked via daw.state.json).
    // Transport/chat/lyria are saved to daw.session.json (gitignored) — no commits.
    if (storeName == "songbird-mixer" && isLoadFinished && !undoRedoInProgress.load() && !midiEditPending.load())
    {
        // Generate descriptive commit message by diffing old vs new state
        juce::String commitMsg = describeMixerChange(prevMixerJson, jsonValue);
        saveStateCache();
        saveEditState();
        commitAndNotify(commitMsg, ProjectState::Mixer, false);

        // Normalize the cached JSON so echo string comparison works.
        stateCache[storeName] = juce::JSON::toString(juce::JSON::parse(jsonValue));
    }
    else if (storeName != "songbird-mixer")
    {
        // Debounce session saves (transport position fires at ~60fps)
        startTimer(500);
    }

    // Parse and react to state changes
    auto json = juce::JSON::parse(jsonValue);
    if (!json.isObject()) return;

    auto state = json.getProperty("state", {});
    if (!state.isObject()) return;

    if (storeName == "songbird-transport")
    {
        applyTransportState(state);
    }
    else if (storeName == "songbird-mixer")
    {
        // During undo/redo, the restored state has already been applied to Tracktion.
        // React's persist will echo back stale values -- don't re-apply them.
        if (!undoRedoInProgress.load())
            applyMixerState(state);
    }
    else if (storeName == "songbird-lyria")
    {
        handleLyriaState(state);
    }
}

void SongbirdEditor::applyTransportState(const juce::var& state)
{
    if (!edit) return;

    auto& transport = edit->getTransport();

    // Playing state
    if (state.hasProperty("playing"))
    {
        bool shouldPlay = state.getProperty("playing", false);
        if (shouldPlay && !transport.isPlaying())
        {
            transport.play(false);
            DBG("Transport: playing");
        }
        else if (!shouldPlay && transport.isPlaying())
        {
            transport.stop(false, false);
            DBG("Transport: stopped");
        }
    }

    // Position
    if (state.hasProperty("position"))
    {
        double posValue = state.getProperty("position", -1.0);
        if (posValue >= 0.0)
        {
            double currentPos = transport.getPosition().inSeconds();
            if (std::abs(currentPos - posValue) > 0.05)
            {
                transport.setPosition(te::TimePosition::fromSeconds(posValue));
                DBG("Transport: position set to " + juce::String(posValue));
            }
        }
    }

    // BPM
    if (state.hasProperty("bpm"))
    {
        double bpm = state.getProperty("bpm", 120.0);
        if (bpm > 20.0 && bpm < 300.0)
        {
            edit->tempoSequence.getTempos()[0]->setBpm(bpm);

        }
    }

    // Looping
    if (state.hasProperty("looping"))
    {
        transport.looping = (bool)state.getProperty("looping", true);
    }
}

void SongbirdEditor::applyMixerState(const juce::var& state)
{
    if (!edit) return;

    suppressMixerEcho = true;

    auto tracksVar = state.getProperty("tracks", {});
    if (!tracksVar.isArray()) { suppressMixerEcho = false; return; }

    auto* tracksArray = tracksVar.getArray();
    if (!tracksArray) { suppressMixerEcho = false; return; }

    auto audioTracks = te::getAudioTracks(*edit);

    for (int i = 0; i < tracksArray->size(); i++)
    {
        auto trackState = (*tracksArray)[i];
        if (!trackState.isObject()) continue;

        te::Track* track = nullptr;
        bool isMaster = trackState.hasProperty("isMaster") ? (bool)trackState.getProperty("isMaster", false) : false;

        if (isMaster) {
            track = edit->getMasterTrack();
        } else if (i < audioTracks.size()) {
            track = audioTracks[i];
        }

        if (!track) continue;

        auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
        if (!audioTrack) continue;

        // Volume (0-127 -> 0.0-1.0) — only apply if changed
        if (trackState.hasProperty("volume"))
        {
            int volMidi = (int)trackState.getProperty("volume", 80);
            double vol = (double)volMidi / 127.0;
            float volDb = juce::Decibels::gainToDecibels(static_cast<float>(vol));
            if (auto volPlugin = audioTrack->getVolumePlugin())
            {
                float prevDb = volPlugin->getVolumeDb();
                if (std::abs(volDb - prevDb) > 0.05f)
                {
                    volPlugin->setVolumeDb(volDb);
                    DBG("  Track[" + juce::String(i) + "] '" + audioTrack->getName()
                        + "' vol: " + juce::String(prevDb, 1) + " -> " + juce::String(volDb, 1) + "dB");
                }
            }
        }

        // Pan — only apply if changed
        if (trackState.hasProperty("pan"))
        {
            float pan = static_cast<float>((double)trackState.getProperty("pan", 0) / 64.0);
            if (auto volPlugin = audioTrack->getVolumePlugin())
            {
                if (std::abs(pan - volPlugin->getPan()) > 0.01f)
                    volPlugin->setPan(pan);
            }
        }

        // Mute — only apply if changed
        if (trackState.hasProperty("muted"))
        {
            bool muted = (bool)trackState.getProperty("muted", false);
            if (muted != audioTrack->isMuted(false))
                audioTrack->setMute(muted);
        }

        // Solo — only apply if changed
        if (trackState.hasProperty("solo"))
        {
            bool solo = (bool)trackState.getProperty("solo", false);
            if (solo != audioTrack->isSolo(false))
                audioTrack->setSolo(solo);
        }

        // Sends
        if (trackState.hasProperty("sends"))
        {
            auto sendsVar = trackState.getProperty("sends", {});
            if (sendsVar.isArray())
            {
                auto* sendsArray = sendsVar.getArray();
                for (int b = 0; b < sendsArray->size() && b < 4; b++)
                {
                    if (auto* sendPlugin = audioTrack->getAuxSendPlugin(b))
                    {
                        double sendVol = (double)(*sendsArray)[b];
                        sendPlugin->setGainDb(juce::Decibels::gainToDecibels(static_cast<float>(sendVol)));
                    }
                }
            }
        }
    }

    suppressMixerEcho = false;
}

void SongbirdEditor::createTrackWatchers()
{
    trackWatchers.clear();
    if (!edit) return;
    
    auto audioTracks = te::getAudioTracks(*edit);
    for (int i = 0; i < audioTracks.size(); i++)
    {
        trackWatchers.push_back(std::make_unique<TrackStateWatcher>(
            audioTracks[i], i, webView.get(), suppressMixerEcho));
    }
    
    DBG("StateSync: Created " + juce::String((int)trackWatchers.size()) + " track watchers");
}

void SongbirdEditor::pushMixerStateToReact()
{
    for (auto& watcher : trackWatchers)
        watcher->forcePush();
}

