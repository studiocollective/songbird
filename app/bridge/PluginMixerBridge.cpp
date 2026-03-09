#include "SongbirdEditor.h"
#include "WebViewHelpers.h"

//==============================================================================
// Bridge: Plugin management, mixer controls, UI lifecycle
//==============================================================================

void SongbirdEditor::registerPluginMixerBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        // Open a plugin's editor window
        .withNativeFunction("openPlugin", [this](auto& args, auto complete) {
            logToJS("Bridge: openPlugin called from JS with " + juce::String(args.size()) + " args");
            if (args.size() > 2) {
                int trackId = static_cast<int>(args[0]);
                juce::String slotType = args[1].toString();
                juce::String pluginId = args[2].toString();
                
                logToJS("Bridge: scheduling openPluginWindow(" + juce::String(trackId) + ", " + slotType + ", " + pluginId + ")");
                
                juce::MessageManager::callAsync([this, trackId, slotType, pluginId]() {
                    logToJS("Bridge: async executing openPluginWindow");
                    openPluginWindow(trackId, slotType, pluginId);
                });
            } else {
                logToJS("Bridge: openPlugin called with insufficient args");
            }
            complete("ok");
        })
        // Change plugin on a track (swap instrument or channel strip)
        .withNativeFunction("changePlugin", [this](auto& args, auto complete) {
            logToJS("Bridge: changePlugin called from JS with " + juce::String(args.size()) + " args");
            if (args.size() > 2) {
                int trackId = static_cast<int>(args[0]);
                juce::String slotType = args[1].toString();
                juce::String pluginName = args[2].toString();
                
                logToJS("Bridge: scheduling changePlugin(" + juce::String(trackId) + ", " + slotType + ", '" + pluginName + "')");
                
                juce::MessageManager::callAsync([this, trackId, slotType, pluginName]() {
                    changePlugin(trackId, slotType, pluginName);
                });
            } else {
                logToJS("Bridge: changePlugin called with insufficient args");
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
        // Query system theme
        .withNativeFunction("getSystemTheme", [](auto&, auto complete) {
            bool isDark = juce::Desktop::getInstance().isDarkModeActive();
            complete(isDark ? "dark" : "light");
        })
        // Set WebView page zoom
        .withNativeFunction("setZoom", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                double zoom = static_cast<double>(args[0]);
                zoomLevel = zoom;
                juce::MessageManager::callAsync([this, zoom]() {
                    #if JUCE_MAC
                    if (webView)
                        setWebViewPageZoom(webView.get(), zoom);
                    #endif
                });
            }
            complete("ok");
        })
        // Signal from React that the UI has mounted
        .withNativeFunction("uiReady", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                startBackgroundLoading();
            });
            complete("ok");
        })
        // Get track notes JSON for the UI
        .withNativeFunction("getTrackState", [this](auto&, auto complete) {
            complete(getTrackStateJSON());
        })
        // Enumerate all automatable parameters on a track's plugins (for AI tool use)
        .withNativeFunction("getPluginParams", [this](auto& args, auto complete) {
            if (!edit || args.size() == 0) { complete("[]"); return; }
            int trackId = static_cast<int>(args[0]);
            auto audioTracks = te::getAudioTracks(*edit);
            if (trackId < 0 || trackId >= audioTracks.size()) { complete("[]"); return; }
            auto* track = audioTracks[trackId];
            if (!track) { complete("[]"); return; }

            juce::Array<juce::var> result;
            for (auto* plugin : track->pluginList.getPlugins()) {
                // Skip utility plugins
                if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) continue;
                if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) continue;
                if (dynamic_cast<te::AuxSendPlugin*>(plugin)) continue;
                if (dynamic_cast<te::AuxReturnPlugin*>(plugin)) continue;

                auto params = plugin->getAutomatableParameters();
                if (params.isEmpty()) continue;

                juce::Array<juce::var> paramList;
                for (auto* param : params) {
                    auto range = param->getValueRange();
                    float norm = param->getCurrentNormalisedValue();
                    
                    juce::String pName = param->getParameterName();
                    if (pName.isEmpty()) pName = param->paramID;
                    if (pName.isEmpty()) pName = "Unknown Param";

                    auto* obj = new juce::DynamicObject();
                    obj->setProperty("id",           param->paramID);
                    obj->setProperty("name",         pName);
                    obj->setProperty("value",        norm);
                    
                    juce::String displayVal = "0.0";
                    // Catch potential assertion in getCurrentValueAsString too
                    try { displayVal = param->getCurrentValueAsString(); } catch (...) {}
                    obj->setProperty("displayValue", displayVal);
                    
                    obj->setProperty("min",          range.getStart());
                    obj->setProperty("max",          range.getEnd());
                    paramList.add(juce::var(obj));
                }

                auto* pluginObj = new juce::DynamicObject();
                pluginObj->setProperty("plugin", plugin->getName());
                pluginObj->setProperty("params", paramList);
                result.add(juce::var(pluginObj));
            }
            complete(juce::JSON::toString(juce::var(result)));
        })
        // Set a plugin parameter by name/id (value is normalized 0.0–1.0)
        .withNativeFunction("setPluginParam", [this](auto& args, auto complete) {
            if (!edit || args.size() < 3) { complete("{\"success\":false,\"error\":\"Bad args\"}"); return; }
            int trackId       = static_cast<int>(args[0]);
            juce::String name = args[1].toString();
            float normValue   = juce::jlimit(0.0f, 1.0f, static_cast<float>(args[2]));

            auto audioTracks = te::getAudioTracks(*edit);
            if (trackId < 0 || trackId >= audioTracks.size()) {
                complete("{\"success\":false,\"error\":\"Track not found\"}"); return;
            }
            auto* track = audioTracks[trackId];

            juce::MessageManager::callAsync([this, track, name, normValue, complete = std::move(complete)]() mutable {
                for (auto* plugin : track->pluginList.getPlugins()) {
                    if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) continue;
                    if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) continue;
                    if (dynamic_cast<te::AuxSendPlugin*>(plugin)) continue;
                    if (dynamic_cast<te::AuxReturnPlugin*>(plugin)) continue;

                    for (auto* param : plugin->getAutomatableParameters()) {
                        bool match = param->paramID.equalsIgnoreCase(name)
                                  || param->getParameterName().equalsIgnoreCase(name)
                                  || param->getParameterName().containsIgnoreCase(name);
                        if (!match) continue;

                        param->setNormalisedParameter(normValue, juce::sendNotification);
                        DBG("PluginParam: Set '" + name + "' on '" + plugin->getName()
                            + "' norm=" + juce::String(normValue));
                        complete("{\"success\":true,\"plugin\":\"" + plugin->getName()
                               + "\",\"param\":\"" + param->getParameterName()
                               + "\",\"value\":" + juce::String(normValue) + "}");
                        return;
                    }
                }
                complete("{\"success\":false,\"error\":\"Parameter not found: " + name + "\"}");
            });
        })
        // Set sidechain source track for a channel strip compressor
        // Args: (destTrackId: int, sourceTrackId: int)  — sourceTrackId = -1 clears sidechain
        .withNativeFunction("setSidechainSource", [this](auto& args, auto complete) {
            if (!edit || args.size() < 2) { complete("{\"success\":false,\"error\":\"Bad args\"}"); return; }
            int destTrackId   = static_cast<int>(args[0]);
            int sourceTrackId = static_cast<int>(args[1]);
            logToJS("Sidechain: dest=" + juce::String(destTrackId) + " src=" + juce::String(sourceTrackId));
            juce::MessageManager::callAsync([this, destTrackId, sourceTrackId, complete = std::move(complete)]() mutable {
                setSidechainSource(destTrackId, sourceTrackId);
                complete("{\"success\":true}");
            });
        })

        // Read current .bird file content (for AI chat context)
        .withNativeFunction("readBird", [this](auto&, auto complete) {
            if (currentBirdFile.existsAsFile())
                complete(currentBirdFile.loadFileAsString());
            else
                complete("");
        })
        // Load a .bird file
        .withNativeFunction("loadBird", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String path = args[0].toString();
                juce::MessageManager::callAsync([this, path]() {
                    auto file = juce::File(path);
                    currentBirdFile = file;
                    loadBirdFile(file); // full synchronous load for explicit file-open
                });
            }
            complete("ok");
        })
        // Update current .bird file in-place (from AI tool call)
        .withNativeFunction("updateBird", [this](auto& args, auto complete) {
            if (args.size() > 0 && currentBirdFile.existsAsFile()) {
                juce::String content = args[0].toString();
                // Auto-commit before LLM change for undo support
                saveEditState();  // flush plugin state first
                commitAndNotify("Pre-LLM state", ProjectState::LLM);
                currentBirdFile.replaceWithText(content);

                // Soft reload: parse + populateEdit (diffs against live state)
                auto result = BirdLoader::parse(currentBirdFile.getFullPathName().toStdString());
                if (result.error.empty()) {
                    BirdLoader::populateEdit(*edit, result, engine, nullptr);
                    lastParseResult = result;
                }

                commitAndNotify("LLM edit", ProjectState::LLM);
                emitTrackState();
            }
            complete("ok");
        })
        // Write current .bird file from user editor (⌘S save)
        // Validates bird content before saving; returns {success, error?}
        .withNativeFunction("writeBirdUser", [this](auto& args, auto complete) {
            if (args.size() == 0 || !currentBirdFile.existsAsFile()) {
                complete("{\"success\":false,\"error\":\"No file loaded\"}");
                return;
            }
            juce::String content = args[0].toString();

            // Validate: write to temp file, parse, check for errors
            auto tmpFile = currentBirdFile.getSiblingFile(".bird_validate_tmp");
            tmpFile.replaceWithText(content);
            auto result = BirdLoader::parse(tmpFile.getFullPathName().toStdString());
            tmpFile.deleteFile();

            if (!result.error.empty()) {
                juce::String errMsg = juce::String(result.error)
                    .replace("\"", "\\\"");  // escape for JSON
                complete("{\"success\":false,\"error\":\"" + errMsg + "\"}");
                return;
            }

            // Valid — write to real file and soft-reload (same as undo path)
            // Full loadBirdFile is too heavy: it re-registers listeners, pumps
            // the message loop, and reloads state caches — causing beach ball.
            juce::MessageManager::callAsync([this, content]() {
                currentBirdFile.replaceWithText(content);

                // Soft reload: parse + populateEdit with no progress callback
                auto result = BirdLoader::parse(currentBirdFile.getFullPathName().toStdString());
                if (result.error.empty()) {
                    BirdLoader::populateEdit(*edit, result, engine, nullptr);
                    lastParseResult = result;
                }

                commitAndNotify("Edit bird file", ProjectState::User);

                // Push updated track state to React
                emitTrackState();
            });

            complete("{\"success\":true}");
        })
        // Set mixer state for a single track (AI-driven mix control)
        // volume_db: float (-60 to +12), pan: float (-1.0 to 1.0), mute/solo: bool
        .withNativeFunction("setTrackMixer", [this](auto& args, auto complete) {
            if (!edit || args.size() < 5) { complete("{\"success\":false,\"error\":\"Bad args\"}"); return; }
            int    trackId   = static_cast<int>(args[0]);
            float  volumeDb  = static_cast<float>(args[1]);
            float  pan       = juce::jlimit(-1.0f, 1.0f, static_cast<float>(args[2]));
            bool   mute      = static_cast<bool>(args[3]);
            bool   solo      = static_cast<bool>(args[4]);

            juce::MessageManager::callAsync([this, trackId, volumeDb, pan, mute, solo, complete = std::move(complete)]() mutable {
                auto audioTracks = te::getAudioTracks(*edit);
                te::AudioTrack* track = nullptr;

                if (trackId == (int)audioTracks.size())
                    track = dynamic_cast<te::AudioTrack*>(edit->getMasterTrack());
                else if (trackId >= 0 && trackId < (int)audioTracks.size())
                    track = audioTracks[trackId];

                if (!track) { complete("{\"success\":false,\"error\":\"Track not found\"}"); return; }

                if (auto vol = track->getVolumePlugin()) {
                    vol->setVolumeDb(volumeDb);
                    vol->setPan(pan);
                }
                track->setMute(mute);
                track->setSolo(solo);

                DBG("MixerSet: track=" + juce::String(trackId) + " vol=" + juce::String(volumeDb)
                    + "dB pan=" + juce::String(pan) + " mute=" + (mute ? "Y" : "N") + " solo=" + (solo ? "Y" : "N"));

                // Re-emit trackState so React mixer panel reflects the change
                emitTrackState();

                complete("{\"success\":true}");
            });
        })
        // Real-time mixer param for slider drags — sets engine directly, no state cache / commit
        // Args: (trackId: int, param: string, value: number)
        // param: "volume" (0-127), "pan" (-64 to 63), "send0"-"send3" (0.0-1.0)
        .withNativeFunction("setMixerParamRT", [this](auto& args, auto complete) {
            if (!edit || args.size() < 3) { complete("ok"); return; }
            int trackId       = static_cast<int>(args[0]);
            juce::String param = args[1].toString();
            double value      = static_cast<double>(args[2]);

            juce::MessageManager::callAsync([this, trackId, param, value]() {
                auto audioTracks = te::getAudioTracks(*edit);
                te::AudioTrack* track = nullptr;

                if (trackId == (int)audioTracks.size())
                    track = dynamic_cast<te::AudioTrack*>(edit->getMasterTrack());
                else if (trackId >= 0 && trackId < (int)audioTracks.size())
                    track = audioTracks[trackId];
                if (!track) return;

                if (param == "volume") {
                    double vol = value / 127.0;
                    float volDb = juce::Decibels::gainToDecibels(static_cast<float>(vol));
                    if (auto vp = track->getVolumePlugin())
                        vp->setVolumeDb(volDb);
                } else if (param == "pan") {
                    float pan = static_cast<float>(value / 64.0);
                    if (auto vp = track->getVolumePlugin())
                        vp->setPan(pan);
                } else if (param.startsWith("send")) {
                    int bus = param.getTrailingIntValue();
                    if (auto* sp = track->getAuxSendPlugin(bus))
                        sp->setGainDb(juce::Decibels::gainToDecibels(static_cast<float>(value)));
                }
            });
            complete("ok");
        });
}
