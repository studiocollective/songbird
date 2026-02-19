#include "SongbirdEditor.h"

//==============================================================================
// State management — stores state as JSON keyed by store name
//==============================================================================

void SongbirdEditor::handleStateUpdate(const juce::String& storeName, const juce::String& jsonValue)
{
    // Store the state for later retrieval
    stateCache[storeName] = jsonValue;

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
        applyMixerState(state);
    }
    else if (storeName == "songbird-lyria")
    {
        handleLyriaState(state);
    }

    DBG("State updated: " + storeName);
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

    // BPM
    if (state.hasProperty("bpm"))
    {
        double bpm = state.getProperty("bpm", 120.0);
        if (bpm > 20.0 && bpm < 300.0)
        {
            edit->tempoSequence.getTempos()[0]->setBpm(bpm);
            DBG("BPM: " + juce::String(bpm));
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

    // Apply track volumes, pans, mutes, solos from state
    auto tracksVar = state.getProperty("tracks", {});
    if (!tracksVar.isArray()) return;

    auto* tracksArray = tracksVar.getArray();
    if (!tracksArray) return;

    auto audioTracks = te::getAudioTracks(*edit);

    for (int i = 0; i < tracksArray->size() && i < audioTracks.size(); i++)
    {
        auto trackState = (*tracksArray)[i];
        auto* track = audioTracks[i];
        if (!track || !trackState.isObject()) continue;

        // Volume (0-127 → 0.0-1.0)
        if (trackState.hasProperty("volume"))
        {
            double vol = (double)trackState.getProperty("volume", 80) / 127.0;
            if (auto volPlugin = track->getVolumePlugin())
                volPlugin->setVolumeDb(juce::Decibels::gainToDecibels(static_cast<float>(vol)));
        }

        // Pan (-64 to 63 → -1.0 to 1.0)
        if (trackState.hasProperty("pan"))
        {
            double pan = (double)trackState.getProperty("pan", 0) / 64.0;
            if (auto volPlugin = track->getVolumePlugin())
                volPlugin->setPan(static_cast<float>(pan));
        }

        // Mute
        if (trackState.hasProperty("muted"))
        {
            track->setMute((bool)trackState.getProperty("muted", false));
        }

        // Solo
        if (trackState.hasProperty("solo"))
        {
            track->setSolo((bool)trackState.getProperty("solo", false));
        }
    }
}
