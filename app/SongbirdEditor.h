#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <tracktion_engine/tracktion_engine.h>
#include <map>
#include <set>
#include "libraries/magenta/LyriaPlugin.h"
#include "BirdLoader.h"
#include "PlaybackInfo.h"
#include "ProjectState.h"
#include "libraries/tracktion/examples/common/PluginWindow.h"
#include "MidiRecorder.h"
#include "AudioRecorder.h"
#include "TrackStateWatcher.h"

namespace te = tracktion;

class SongbirdEditor : public juce::Component, 
                       private juce::Timer,
                       private juce::AudioProcessorListener
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

    // Domain-specific bridge registration (called by createWebViewOptions)
    void registerStateBridge(juce::WebBrowserComponent::Options& options);
    void registerPluginMixerBridge(juce::WebBrowserComponent::Options& options);
    void registerBirdFileBridge(juce::WebBrowserComponent::Options& options);
    void registerTransportBridge(juce::WebBrowserComponent::Options& options);
    void registerSettingsBridge(juce::WebBrowserComponent::Options& options);
    void registerRecordingBridge(juce::WebBrowserComponent::Options& options);
    void registerTrackBridge(juce::WebBrowserComponent::Options& options);
    void registerLyriaBridge(juce::WebBrowserComponent::Options& options);
    void registerMidiEditBridge(juce::WebBrowserComponent::Options& options);
    void registerUndoRedoBridge(juce::WebBrowserComponent::Options& options);

    // State cache (storeName → JSON)
    std::map<juce::String, juce::String> stateCache;
    void handleStateUpdate(const juce::String& storeName, const juce::String& jsonValue);
    void applyTransportState(const juce::var& state);
    void applyMixerState(const juce::var& state);
    void saveStateCache();
    void saveSessionState();
    juce::String describeMixerChange(const juce::String& oldJson, const juce::String& newJson);
    void commitAndNotify(const juce::String& message, ProjectState::Source source, bool includeEditXml = true);
    void loadStateCache();
    void saveEditState();
    void loadEditState();
    void timerCallback() override;

    // Reactive plugin state tracking
    // Listeners fire from audio thread → callAsync marks plugins dirty on message thread
    // Timer debounces → flushes only dirty plugins → writes JSON to disk → commits to git
    std::set<te::ExternalPlugin*> dirtyPlugins;
    std::map<juce::String, std::set<juce::String>> dirtyPluginParams;  // plugin name → param names
    bool pluginParamsDirty = false;           // true when timer should commit plugin changes
    void registerPluginListeners();
    void unregisterPluginListeners();
    te::ExternalPlugin* findExternalPlugin(juce::AudioProcessor* processor);
    void audioProcessorParameterChanged(juce::AudioProcessor* processor, int parameterIndex, float newValue) override;
    void audioProcessorChanged(juce::AudioProcessor* processor, const ChangeDetails& details) override;

    // Per-track reactive mixer state sync (C++ → React)
    std::vector<std::unique_ptr<TrackStateWatcher>> trackWatchers;
    std::atomic<bool> suppressMixerEcho { false };
    std::atomic<bool> undoRedoInProgress { false };  // suppresses applyMixerState from React persist echo
    void createTrackWatchers();
    void pushMixerStateToReact();  // force push all tracks (after load/undo)

    // Bird file loading
    void scanForPlugins();
    void loadBirdFile(const juce::File& birdFile);
    void scheduleReload(const juce::String& content);
    void applyParsedResult(const BirdParseResult& result);
    void exportSheetMusic();
    void exportStems(bool includeReturnFx);
    void exportMaster();
    juce::String getTrackStateJSON();
    BirdParseResult lastParseResult;
    juce::File currentBirdFile;
    std::unique_ptr<juce::Thread> stemExportThread;

    // Reload debounce & background parse
    juce::String pendingBirdContent;
    std::atomic<bool> reloadPending { false };
    std::unique_ptr<juce::Thread> reloadThread;

    bool isLoadingStarted = false;
    bool isLoadFinished = false;
    bool projectLoadComplete = false;  // set after "Project loaded" commit
    bool reactHydrated = false;        // set when React calls reactReady
    bool pendingProjectLoadCommit = false;  // deferred until plugins settle
    void checkLoadFinished();           // sets isLoadFinished when both flags are true
    void startBackgroundLoading();

    // Lyria generated track management
    struct LyriaTrackContext {
        magenta::LyriaPlugin* plugin = nullptr;
        int quantizeBars = 0;
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

    // MIDI editing helpers (piano roll → bird + Tracktion)
    struct PendingMidiEdit {
        int trackId = -1;
        juce::String sectionName;
        double secOffset = 0.0;
        int secBars = 4;
    };
    PendingMidiEdit pendingMidiEdit;
    std::atomic<bool> midiEditPending { false };  // suppresses mixer echo commits

    struct ClipNote { int pitch; int velocity; double relBeat; };
    std::vector<ClipNote> collectNotesFromClip(int trackId, double secOffset, int secBars);
    void writeBirdFromClip(int trackId, const juce::String& sectionName,
                           double secOffset, int secBars,
                           const std::vector<ClipNote>& clipNotes);
    void emitTrackState(bool emitLoadingDone = false);
    void scheduleMidiCommit();

    // Playback info (levels, transport position, stereo analysis)
    PlaybackInfo playbackInfo;

    // WebView zoom & inspector
    double zoomLevel = 1.0;
    bool inspectorEnabled = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongbirdEditor)
};
