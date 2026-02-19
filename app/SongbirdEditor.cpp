#include "SongbirdEditor.h"

#if JUCE_MAC
  #include <objc/objc.h>
  #include <objc/message.h>
  #include <objc/runtime.h>

  // Cast helper — objc_msgSend has different signatures per call
  template <typename Ret, typename... Args>
  static inline Ret msg(id obj, SEL sel, Args... args)
  {
      return ((Ret (*)(id, SEL, Args...))objc_msgSend)(obj, sel, args...);
  }

  #if JUCE_DEBUG
  static void enableWebViewInspector(juce::Component* webViewComponent)
  {
      auto* peer = webViewComponent->getPeer();
      if (!peer) return;

      auto nsView = (id) peer->getNativeHandle();
      id subviews = msg<id>(nsView, sel_registerName("subviews"));
      if (!subviews) return;

      auto count = msg<unsigned long>(subviews, sel_registerName("count"));
      SEL inspSel = sel_registerName("setInspectable:");

      for (unsigned long i = 0; i < count; i++)
      {
          id child = msg<id>(subviews, sel_registerName("objectAtIndex:"), i);
          if (!child) continue;

          if (msg<BOOL>(child, sel_registerName("respondsToSelector:"), inspSel))
          {
              msg<void>(child, inspSel, (BOOL)YES);
              DBG("WebView inspector enabled");
              return;
          }

          // Check one level deeper
          id childSubs = msg<id>(child, sel_registerName("subviews"));
          if (!childSubs) continue;
          auto childCount = msg<unsigned long>(childSubs, sel_registerName("count"));
          for (unsigned long j = 0; j < childCount; j++)
          {
              id gc = msg<id>(childSubs, sel_registerName("objectAtIndex:"), j);
              if (gc && msg<BOOL>(gc, sel_registerName("respondsToSelector:"), inspSel))
              {
                  msg<void>(gc, inspSel, (BOOL)YES);
                  DBG("WebView inspector enabled");
                  return;
              }
          }
      }
  }
  #endif

  static id findWKWebView(juce::Component* webViewComponent)
  {
      auto* peer = webViewComponent->getPeer();
      if (!peer) return nil;

      auto nsView = (id) peer->getNativeHandle();
      id subviews = msg<id>(nsView, sel_registerName("subviews"));
      if (!subviews) return nil;

      auto count = msg<unsigned long>(subviews, sel_registerName("count"));
      SEL pageSel = sel_registerName("setPageZoom:");

      for (unsigned long i = 0; i < count; i++)
      {
          id child = msg<id>(subviews, sel_registerName("objectAtIndex:"), i);
          if (!child) continue;

          if (msg<BOOL>(child, sel_registerName("respondsToSelector:"), pageSel))
              return child;

          id childSubs = msg<id>(child, sel_registerName("subviews"));
          if (!childSubs) continue;
          auto childCount = msg<unsigned long>(childSubs, sel_registerName("count"));
          for (unsigned long j = 0; j < childCount; j++)
          {
              id gc = msg<id>(childSubs, sel_registerName("objectAtIndex:"), j);
              if (gc && msg<BOOL>(gc, sel_registerName("respondsToSelector:"), pageSel))
                  return gc;
          }
      }
      return nil;
  }
#endif


//==============================================================================
SongbirdEditor::SongbirdEditor()
{
    // Load the default .bird file into the Edit
    // Search upward from the app bundle for the files/ directory
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    juce::File birdFile;
    auto searchDir = appFile.getParentDirectory();
    for (int i = 0; i < 8; i++) {
        auto candidate = searchDir.getChildFile("files/live.bird");
        if (candidate.existsAsFile()) {
            birdFile = candidate;
            break;
        }
        searchDir = searchDir.getParentDirectory();
    }

    // Fallback: try relative to CWD
    if (!birdFile.existsAsFile())
        birdFile = juce::File::getCurrentWorkingDirectory().getChildFile("files/live.bird");

    DBG("BirdLoader: App location: " + appFile.getFullPathName());
    DBG("BirdLoader: Bird file path: " + birdFile.getFullPathName());

    loadBirdFile(birdFile);

    // Create WebView with native function bridge
    auto options = createWebViewOptions();
    webView = std::make_unique<juce::WebBrowserComponent>(options);
    addAndMakeVisible(*webView);

    // Load the React UI
    #if JUCE_DEBUG
        webView->goToURL("http://localhost:5173");
        DBG("Loading React UI from dev server (localhost:5173)");
    #else
        auto resourceDir = juce::File::getSpecialLocation(
            juce::File::currentApplicationFile)
            .getChildFile("Contents/Resources/react_ui");
        auto indexFile = resourceDir.getChildFile("index.html");
        if (indexFile.existsAsFile())
            webView->goToURL(indexFile.getFullPathName());
        else
            DBG("React UI not found at: " + resourceDir.getFullPathName());
    #endif

    setSize(1280, 800);
    DBG("SongbirdEditor initialized - engine and edit ready");
}

SongbirdEditor::~SongbirdEditor()
{
    if (edit)
        edit->getTransport().stop(false, false);
    edit = nullptr;
}

//==============================================================================
void SongbirdEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void SongbirdEditor::resized()
{
    if (webView)
        webView->setBounds(getLocalBounds());

    #if JUCE_MAC
    // Enable inspector (debug only) once the native peer is available
    #if JUCE_DEBUG
    if (!inspectorEnabled && webView)
    {
        enableWebViewInspector(webView.get());
        inspectorEnabled = true;
    }
    #endif
    #endif
}

void SongbirdEditor::setWebViewPageZoom(double zoom)
{
    #if JUCE_MAC
    if (!webView) return;
    id wkView = findWKWebView(webView.get());
    if (wkView)
    {
        msg<void>(wkView, sel_registerName("setPageZoom:"), zoom);
        DBG("WebView page zoom set to " + juce::String(zoom));
    }
    #endif
}



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

//==============================================================================
juce::WebBrowserComponent::Options SongbirdEditor::createWebViewOptions()
{
    return juce::WebBrowserComponent::Options{}
        // Load state from C++ cache (Zustand persist getItem)
        .withNativeFunction("loadState", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String storeName = args[0].toString();
                if (stateCache.count(storeName))
                    complete(stateCache[storeName]);
                else
                    complete("{\"state\":null}");
            }
        })
        // Update state on C++ side (Zustand persist setItem)
        .withNativeFunction("updateState", [this](auto& args, auto complete) {
            if (args.size() > 1) {
                juce::String storeName = args[0].toString();
                juce::String value = args[1].toString();
                handleStateUpdate(storeName, value);
                complete("ok");
            }
        })
        // Reset state (Zustand persist removeItem)
        .withNativeFunction("resetState", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String storeName = args[0].toString();
                stateCache.erase(storeName);
                complete("ok");
            }
        })
        // Open a plugin's editor window
        .withNativeFunction("openPlugin", [this](auto& args, auto complete) {
            if (args.size() > 2) {
                int trackId = static_cast<int>(args[0]);
                juce::String slotType = args[1].toString();
                juce::String pluginId = args[2].toString();
                juce::MessageManager::callAsync([this, trackId, slotType, pluginId]() {
                    openPluginWindow(trackId, slotType, pluginId);
                });
            }
            complete("ok");
        })
        // Add a generated track
        .withNativeFunction("addGeneratedTrack", [this](auto& args, auto complete) {
            juce::MessageManager::callAsync([this]() {
                addGeneratedTrack();
            });
            complete("ok");
        })
        // Remove a generated track
        .withNativeFunction("removeGeneratedTrack", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                int trackId = static_cast<int>(args[0]);
                juce::MessageManager::callAsync([this, trackId]() {
                    removeGeneratedTrack(trackId);
                });
            }
            complete("ok");
        })
        // Query system theme (fallback when WKWebView doesn't honor prefers-color-scheme)
        .withNativeFunction("getSystemTheme", [](auto&, auto complete) {
            bool isDark = juce::Desktop::getInstance().isDarkModeActive();
            complete(isDark ? "dark" : "light");
        })
        // Set WebView page zoom (called from JS on Cmd+/Cmd-/Cmd+0)
        .withNativeFunction("setZoom", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                double zoom = static_cast<double>(args[0]);
                zoomLevel = zoom;
                juce::MessageManager::callAsync([this, zoom]() {
                    setWebViewPageZoom(zoom);
                });
            }
            complete("ok");
        })
        // Get track notes JSON for the UI
        .withNativeFunction("getTrackNotes", [this](auto&, auto complete) {
            complete(getTrackNotesJSON());
        })
        // Load a .bird file
        .withNativeFunction("loadBird", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String path = args[0].toString();
                juce::MessageManager::callAsync([this, path]() {
                    loadBirdFile(juce::File(path));
                    // Push updated track notes to the UI
                    if (webView) {
                        auto json = getTrackNotesJSON();
                        webView->emitEventIfBrowserIsVisible("trackNotes", juce::var(json));
                    }
                });
            }
            complete("ok");
        });
}

//==============================================================================
// Bird file loading
//==============================================================================

void SongbirdEditor::loadBirdFile(const juce::File& birdFile)
{
    if (!birdFile.existsAsFile()) {
        DBG("BirdLoader: File not found: " + birdFile.getFullPathName());
        // Create a minimal empty edit as fallback
        edit = std::make_unique<te::Edit>(engine, te::Edit::EditRole::forEditing);
        edit->tempoSequence.getTempos()[0]->setBpm(120.0);
        return;
    }

    DBG("BirdLoader: Loading " + birdFile.getFullPathName());
    auto result = BirdLoader::parse(birdFile.getFullPathName().toStdString());

    if (!result.error.empty()) {
        DBG("BirdLoader: Parse error: " + juce::String(result.error));
        edit = std::make_unique<te::Edit>(engine, te::Edit::EditRole::forEditing);
        edit->tempoSequence.getTempos()[0]->setBpm(120.0);
        return;
    }

    // Create or reset the edit
    edit = std::make_unique<te::Edit>(engine, te::Edit::EditRole::forEditing);
    edit->tempoSequence.getTempos()[0]->setBpm(120.0);

    BirdLoader::populateEdit(*edit, result);

    // Push track notes to UI if webview is up
    if (webView) {
        auto json = getTrackNotesJSON();
        webView->emitEventIfBrowserIsVisible("trackNotes", juce::var(json));
    }
}

juce::String SongbirdEditor::getTrackNotesJSON()
{
    if (!edit) return "[]";
    return BirdLoader::getTrackNotesJSON(*edit);
}

//==============================================================================
// Lyria generated track management
//==============================================================================

void SongbirdEditor::handleLyriaState(const juce::var& state)
{
    // Apply Lyria config/prompts to all generated tracks
    for (auto& [trackId, plugin] : lyriaPlugins)
    {
        if (!plugin) continue;

        // API key
        if (state.hasProperty("apiKey"))
        {
            juce::String key = state.getProperty("apiKey", "").toString();
            if (key.isNotEmpty())
                plugin->setApiKey(key);
        }

        // Playing state
        if (state.hasProperty("playing"))
        {
            bool shouldPlay = state.getProperty("playing", false);
            if (shouldPlay)
                plugin->play();
            else
                plugin->pause();
        }

        // Prompts
        if (state.hasProperty("prompts"))
        {
            auto promptsVar = state.getProperty("prompts", {});
            if (promptsVar.isArray())
            {
                auto* promptsArray = promptsVar.getArray();
                std::vector<magenta::Prompt> prompts;
                for (int i = 0; i < promptsArray->size(); i++)
                {
                    auto p = (*promptsArray)[i];
                    magenta::Prompt prompt;
                    prompt.id = std::to_string(i);
                    prompt.text = p.getProperty("text", "").toString().toStdString();
                    prompt.weight = static_cast<float>((double)p.getProperty("weight", 1.0));
                    prompts.push_back(prompt);
                }
                plugin->setPrompts(prompts);
            }
        }

        // Config
        if (state.hasProperty("config"))
        {
            auto configVar = state.getProperty("config", {});
            if (configVar.isObject())
            {
                magenta::LyriaConfig config;
                config.temperature = static_cast<float>((double)configVar.getProperty("temperature", 1.0));
                config.guidance = static_cast<float>((double)configVar.getProperty("guidance", 3.0));
                config.topk = static_cast<int>(configVar.getProperty("topK", 250));
                config.bpm = static_cast<int>(configVar.getProperty("bpm", 120));
                config.useBpm = (bool)configVar.getProperty("useBpm", false);
                config.density = static_cast<float>((double)configVar.getProperty("density", 0.5));
                config.useDensity = (bool)configVar.getProperty("useDensity", false);
                config.brightness = static_cast<float>((double)configVar.getProperty("brightness", 0.5));
                config.useBrightness = (bool)configVar.getProperty("useBrightness", false);
                config.muteBass = (bool)configVar.getProperty("muteBass", false);
                config.muteDrums = (bool)configVar.getProperty("muteDrums", false);
                config.muteOther = (bool)configVar.getProperty("muteOther", false);
                plugin->setConfig(config);
            }
        }
    }
}

void SongbirdEditor::addGeneratedTrack()
{
    if (!edit) return;

    int numTracks = te::getAudioTracks(*edit).size();
    edit->ensureNumberOfAudioTracks(numTracks + 1);
    auto* track = te::getAudioTracks(*edit)[numTracks];
    if (!track) return;

    int trackId = numTracks + 1;
    track->setName("Generated " + juce::String(trackId));

    // Create and add the LyriaPlugin to the track
    auto pluginInfo = te::PluginCreationInfo(*edit, track->state.getOrCreateChildWithName(
        te::IDs::PLUGIN, nullptr), true);

    auto lyriaPlugin = new magenta::LyriaPlugin(pluginInfo);
    track->pluginList.insertPlugin(*lyriaPlugin, 0, nullptr);
    lyriaPlugins[trackId] = lyriaPlugin;

    // Wire status callbacks to push to UI
    lyriaPlugin->onStatusChange = [this](bool connected, bool buffering) {
        // Push status back to React UI via emitEvent
        if (webView)
        {
            juce::String js = "window.dispatchEvent(new CustomEvent('lyria-status', {detail: {connected: "
                + juce::String(connected ? "true" : "false") + ", buffering: "
                + juce::String(buffering ? "true" : "false") + "}}));";
            webView->evaluateJavascript(js, nullptr);
        }
    };

    DBG("Added generated track " + juce::String(trackId));
}

void SongbirdEditor::removeGeneratedTrack(int trackId)
{
    if (!edit) return;

    auto it = lyriaPlugins.find(trackId);
    if (it != lyriaPlugins.end())
    {
        lyriaPlugins.erase(it);
    }

    // Find and remove the track from the edit
    auto audioTracks = te::getAudioTracks(*edit);
    if (trackId > 0 && trackId <= audioTracks.size())
    {
        auto* track = audioTracks[trackId - 1];
        if (track)
            edit->deleteTrack(track);
    }

    DBG("Removed generated track " + juce::String(trackId));
}

//==============================================================================
// Plugin window management
//==============================================================================

void SongbirdEditor::openPluginWindow(int trackId, const juce::String& slotType, const juce::String& pluginId)
{
    if (!edit) return;

    auto audioTracks = te::getAudioTracks(*edit);
    if (trackId < 1 || trackId > audioTracks.size()) return;

    auto* track = audioTracks[trackId - 1];
    if (!track) return;

    te::Plugin::Ptr targetPlugin;

    if (slotType == "instrument")
    {
        // Instrument is the first non-volume plugin on a MIDI track
        for (auto* plugin : track->pluginList)
        {
            if (plugin != track->getVolumePlugin())
            {
                targetPlugin = plugin;
                break;
            }
        }
    }
    else if (slotType == "channelStrip")
    {
        // Channel strip — look for external plugins (not the built-in volume/pan)
        for (auto* plugin : track->pluginList)
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
            {
                targetPlugin = ext;
                break;
            }
        }
    }

    if (targetPlugin)
    {
        targetPlugin->showWindowExplicitly();
        DBG("Opened plugin window: " + targetPlugin->getName() + " on track " + juce::String(trackId));
    }
    else
    {
        DBG("No plugin found for slot " + slotType + " on track " + juce::String(trackId));
    }
}
