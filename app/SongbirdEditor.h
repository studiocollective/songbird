#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <tracktion_engine/tracktion_engine.h>
#include <map>
#include "libraries/magenta/LyriaPlugin.h"
#include "BirdLoader.h"
#include "PlaybackInfo.h"
#include "ProjectState.h"
#include "libraries/tracktion/examples/common/PluginWindow.h"
#include "MidiRecorder.h"
#include "AudioRecorder.h"

namespace te = tracktion;

class SongbirdEditor : public juce::Component, private juce::Timer
{
public:
    SongbirdEditor();
    ~SongbirdEditor() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // Engine & Edit
    te::Engine engine { "Songbird Player", std::make_unique<ExtendedUIBehaviour>(), nullptr };
    std::unique_ptr<te::Edit> edit;

    // WebView
    std::unique_ptr<juce::WebBrowserComponent> webView;
    juce::WebBrowserComponent::Options createWebViewOptions();

    // State cache (storeName → JSON)
    std::map<juce::String, juce::String> stateCache;
    void handleStateUpdate(const juce::String& storeName, const juce::String& jsonValue);
    void applyTransportState(const juce::var& state);
    void applyMixerState(const juce::var& state);
    void saveStateCache();
    void loadStateCache();
    void saveEditState();
    void loadEditState();
    void timerCallback() override;

    // Bird file loading
    void scanForPlugins();
    void loadBirdFile(const juce::File& birdFile);
    void scheduleReload(const juce::String& content);
    void applyParsedResult(const BirdParseResult& result);
    void exportSheetMusic();
    void exportStems(bool includeReturnFx);
    void exportMaster();
    juce::String getTrackNotesJSON();
    BirdParseResult lastParseResult;
    juce::File currentBirdFile;
    std::unique_ptr<juce::Thread> stemExportThread;

    // Reload debounce & background parse
    juce::String pendingBirdContent;
    std::atomic<bool> reloadPending { false };
    std::unique_ptr<juce::Thread> reloadThread;

    bool isLoadingStarted = false;
    bool isLoadFinished = false;
    void startBackgroundLoading();

    // Lyria generated track management
    struct LyriaTrackContext {
        magenta::LyriaPlugin* plugin = nullptr;
        int quantizeBars = 0;  // 0 = no quantize, 1 = 1 bar, 2 = 2 bars, etc.
    };
    std::map<int, LyriaTrackContext> lyriaPlugins;
    void addGeneratedTrack();
    void removeGeneratedTrack(int trackId);
    void handleLyriaState(const juce::var& state);
    void setLyriaTrackConfig(int trackId, const juce::var& config);
    void setLyriaTrackPrompts(int trackId, const juce::var& prompts);
    void setLyriaQuantize(int trackId, int bars);

    // MIDI recorder
    std::unique_ptr<MidiRecorder> midiRecorder;
    int midiRecordTrackId = -1;

    // Audio recorder
    std::unique_ptr<AudioRecorder> audioRecorder;

    // Plugin window management
    void openPluginWindow(int trackId, const juce::String& slotType, const juce::String& pluginId);
    void changePlugin(int trackId, const juce::String& slotType, const juce::String& pluginName);
    void setSidechainSource(int destTrackId, int sourceTrackId);
    void logToJS(const juce::String& message);

    // Project state (git-based undo/redo)
    ProjectState projectState;
    void undoProject();
    void redoProject();
    void revertLLM();

    // Playback info (levels, transport position, stereo analysis)
    PlaybackInfo playbackInfo;

    // WebView zoom & inspector
    double zoomLevel = 1.0;
    bool inspectorEnabled = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongbirdEditor)
};
