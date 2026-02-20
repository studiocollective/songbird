#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <tracktion_engine/tracktion_engine.h>
#include <map>
#include "libraries/magenta/LyriaPlugin.h"
#include "BirdLoader.h"
#include "PlaybackInfo.h"

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

    // Bird file loading
    void scanForPlugins();
    void loadBirdFile(const juce::File& birdFile);
    juce::String getTrackNotesJSON();
    BirdParseResult lastParseResult;  // stored for JSON serialization with plugin info
    juce::File currentBirdFile;       // path to the currently loaded .bird file

    // Lyria generated track management
    std::map<int, magenta::LyriaPlugin*> lyriaPlugins;
    void addGeneratedTrack();
    void removeGeneratedTrack(int trackId);
    void handleLyriaState(const juce::var& state);

    // Plugin window management
    void openPluginWindow(int trackId, const juce::String& slotType, const juce::String& pluginId);
    void changePlugin(int trackId, const juce::String& slotType, const juce::String& pluginName);
    void logToJS(const juce::String& message);

    // Playback info (levels, transport position, stereo analysis)
    PlaybackInfo playbackInfo;

    // WebView zoom & inspector
    double zoomLevel = 1.0;
    bool inspectorEnabled = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongbirdEditor)
};
