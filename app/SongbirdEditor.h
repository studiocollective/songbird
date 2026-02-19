#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <tracktion_engine/tracktion_engine.h>
#include <map>
#include "libraries/magenta/LyriaPlugin.h"

namespace te = tracktion;

class SongbirdEditor : public juce::Component
{
public:
    SongbirdEditor();
    ~SongbirdEditor() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // Engine & Edit
    te::Engine engine { "Songbird Player" };
    std::unique_ptr<te::Edit> edit;

    // WebView
    std::unique_ptr<juce::WebBrowserComponent> webView;
    juce::WebBrowserComponent::Options createWebViewOptions();

    // State cache (storeName → JSON)
    std::map<juce::String, juce::String> stateCache;
    void handleStateUpdate(const juce::String& storeName, const juce::String& jsonValue);
    void applyTransportState(const juce::var& state);
    void applyMixerState(const juce::var& state);

    // Audio helpers
    void createTestEdit();
    void addTestMidiClip(te::AudioTrack& track, int trackIndex);

    // Lyria generated track management
    std::map<int, magenta::LyriaPlugin*> lyriaPlugins;
    void addGeneratedTrack();
    void removeGeneratedTrack(int trackId);
    void handleLyriaState(const juce::var& state);

    // Plugin window management
    void openPluginWindow(int trackId, const juce::String& slotType, const juce::String& pluginId);

    // WebView inspector (debug only)
    bool inspectorEnabled = false;
    bool zoomEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongbirdEditor)
};
