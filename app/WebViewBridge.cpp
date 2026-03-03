#include "SongbirdEditor.h"
#include "WebViewHelpers.h"

//==============================================================================
// WebView native function bridge
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
                // Dispatch to message thread so JUCE Timer and audio APIs work correctly
                juce::MessageManager::callAsync([this, storeName, value]() {
                    handleStateUpdate(storeName, value);
                });
                complete("ok");
            }
        })
        // React signals all Zustand stores have finished hydrating
        .withNativeFunction("reactReady", [this](auto& /*args*/, auto complete) {
            juce::MessageManager::callAsync([this]() {
                reactHydrated = true;
                DBG("StateSync: React hydrated");
            });
            complete("ok");
        })
        // Get git history for UI
        .withNativeFunction("getHistory", [this](auto& /*args*/, auto complete) {
            juce::String headHash;
            auto history = projectState.getHistory(50, &headHash);

            // Redo-tip hash
            juce::String redoTipHash;
            git_reference* redoRef = nullptr;
            if (projectState.getRepo() && git_reference_lookup(&redoRef, projectState.getRepo(), "refs/redo-tip") == 0)
            {
                char buf[GIT_OID_SHA1_HEXSIZE + 1];
                git_oid_tostr(buf, sizeof(buf), git_reference_target(redoRef));
                redoTipHash = juce::String(buf).substring(0, 7);
                git_reference_free(redoRef);
            }

            // Build commits array and find HEAD index
            juce::Array<juce::var> commits;
            int headIndex = 0;
            for (int i = 0; i < history.size(); i++)
            {
                const auto& entry = history[i];
                auto* obj = new juce::DynamicObject();
                auto shortHash = entry.hash.substring(0, 7);
                obj->setProperty("hash", shortHash);
                obj->setProperty("message", entry.message.trimEnd());
                obj->setProperty("timestamp", entry.timestamp);
                commits.add(juce::var(obj));

                if (headHash.startsWith(shortHash) || shortHash.startsWith(headHash.substring(0, 7)))
                    headIndex = i;
            }

            auto* result = new juce::DynamicObject();
            result->setProperty("commits", commits);
            result->setProperty("headIndex", headIndex);
            result->setProperty("redoTipHash", redoTipHash);
            complete(juce::JSON::toString(juce::var(result)));
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

                    auto* obj = new juce::DynamicObject();
                    obj->setProperty("id",           param->paramID);
                    obj->setProperty("name",         param->getParameterName());
                    obj->setProperty("value",        norm);
                    obj->setProperty("displayValue", param->getCurrentValueAsString());
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
                // Write to disk synchronously (fast), then schedule a debounced
                // background parse + apply so the main thread is never blocked.
                currentBirdFile.replaceWithText(content);
                DBG("BirdUpdate: Wrote " + currentBirdFile.getFullPathName() + ", scheduling reload");
                scheduleReload(content);
            }
            complete("ok");
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
                if (webView) {
                    auto json = BirdLoader::getTrackStateJSON(*edit, &lastParseResult);
                    webView->emitEventIfBrowserIsVisible("trackState", juce::var(json));
                }

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
        })
        // Set project BPM
        .withNativeFunction("setBpm", [this](auto& args, auto complete) {
            if (!edit || args.size() < 1) { complete("{\"success\":false}"); return; }
            double bpm = static_cast<double>(args[0]);
            if (bpm < 20.0 || bpm > 300.0) { complete("{\"success\":false,\"error\":\"BPM out of range 20-300\"}"); return; }
            juce::MessageManager::callAsync([this, bpm, complete = std::move(complete)]() mutable {
                edit->tempoSequence.getTempos()[0]->setBpm(bpm);
                DBG("Transport: BPM set to " + juce::String(bpm));
                complete("{\"success\":true,\"bpm\":" + juce::String(bpm) + "}");
            });
        })
        
        // Explicit Transport Controls
        .withNativeFunction("transportPlay", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                if (edit) {
                    edit->getTransport().play(false);
                    DBG("Transport: Playing (Native)");
                }
            });
            complete("ok");
        })
        .withNativeFunction("transportStop", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                if (edit) {
                    edit->getTransport().stop(false, false);
                    edit->getTransport().setPosition(te::TimePosition::fromSeconds(0.0));
                    DBG("Transport: Stopped & Rewound (Native)");
                }
            });
            complete("ok");
        })
        .withNativeFunction("transportSeek", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                double pos = static_cast<double>(args[0]);
                juce::MessageManager::callAsync([this, pos]() {
                    if (edit) {
                        edit->getTransport().setPosition(te::TimePosition::fromSeconds(pos));
                        DBG("Transport: Seeked to " + juce::String(pos) + " (Native)");
                    }
                });
            }
            complete("ok");
        })
        .withNativeFunction("setLoopRange", [this](auto& args, auto complete) {
            if (!edit || args.size() < 2) { complete("ok"); return; }
            int startBar = static_cast<int>(args[0]);
            int endBar   = static_cast<int>(args[1]);
            juce::MessageManager::callAsync([this, startBar, endBar]() {
                if (!edit) return;
                double bpm = edit->tempoSequence.getTempos()[0]->getBpm();
                double secPerBar = (60.0 / bpm) * 4.0;
                auto startTime = te::TimePosition::fromSeconds(secPerBar * startBar);
                auto endTime   = te::TimePosition::fromSeconds(secPerBar * endBar);
                edit->getTransport().setLoopRange(te::TimeRange(startTime, endTime));
                DBG("Transport: Loop range set to bar " + juce::String(startBar) + " - " + juce::String(endBar));
            });
            complete("ok");
        })

        // Export to MIDI (Sheet Music)
        .withNativeFunction("exportSheetMusic", [this](auto& args, auto complete) {
            juce::MessageManager::callAsync([this]() {
                exportSheetMusic();
            });
            complete("ok");
        })
        // Export Stems
        .withNativeFunction("exportStems", [this](auto& args, auto complete) {
            bool includeReturnFx = false;
            if (args.size() > 0) {
                includeReturnFx = static_cast<bool>(args[0]);
            }
            juce::MessageManager::callAsync([this, includeReturnFx]() {
                exportStems(includeReturnFx);
            });
            complete("ok");
        })
        // Export Master (full mix)
        .withNativeFunction("exportMaster", [this](auto& args, auto complete) {
            juce::MessageManager::callAsync([this]() {
                exportMaster();
            });
            complete("ok");
        })
        // Save a .bird file (from AI generation)
        .withNativeFunction("saveBird", [this](auto& args, auto complete) {
            if (args.size() > 1) {
                juce::String relPath = args[0].toString();
                juce::String content = args[1].toString();
                
                // Resolve path relative to app location similar to constructor
                auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
                auto searchDir = appFile.getParentDirectory();
                juce::File targetFile;
                
                // Try to find the "files" directory
                juce::File filesDir;
                for (int i = 0; i < 8; i++) {
                    auto candidate = searchDir.getChildFile("files");
                    if (candidate.isDirectory()) {
                        filesDir = candidate;
                        break;
                    }
                    searchDir = searchDir.getParentDirectory();
                }
                
                if (filesDir.isDirectory()) {
                    targetFile = filesDir.getChildFile(relPath);
                } else {
                    // Fallback to CWD
                    targetFile = juce::File::getCurrentWorkingDirectory().getChildFile(relPath);
                }

                juce::MessageManager::callAsync([this, targetFile, content]() {
                    if (targetFile.existsAsFile()) {
                        targetFile.replaceWithText(content);
                        DBG("BirdSave: Updated " + targetFile.getFullPathName());
                    } else {
                        targetFile.create();
                        targetFile.replaceWithText(content);
                        DBG("BirdSave: Created " + targetFile.getFullPathName());
                    }

                    // Hot-reload: schedule debounced background reload
                    currentBirdFile = targetFile;
                    scheduleReload(content);
                });
            }
            complete("ok");
        })
        // Get list of available plugins — instruments from filesystem, effects curated
        .withNativeFunction("getAvailablePlugins", [this](const juce::Array<juce::var>&, std::function<void(juce::var)> complete) {
            juce::Array<juce::var> instruments;
            
            juce::StringArray validSynths = {
                "Augmented Strings", "Buchla Easel V", "CS-80 V4", "DX7 V",
                "Jun-6 V", "Jup-8 V4", "Mini V3", "OB-Xa V", "Prophet-5 V",
                "Heartbeat"
            };
            
            // List all .vst3 bundles in the standard directory
            juce::File vst3Dir("/Library/Audio/Plug-Ins/VST3");
            if (vst3Dir.isDirectory()) {
                auto files = vst3Dir.findChildFiles(
                    juce::File::findDirectories, false, "*.vst3");
                files.sort();
                
                for (auto& f : files) {
                    auto name = f.getFileNameWithoutExtension();
                    
                    // Filter: only show if in our valid synth list
                    // (prevents effects from cluttering the instrument dropdown)
                    if (!validSynths.contains(name)) continue;
                    
                    juce::DynamicObject* obj = new juce::DynamicObject();
                    obj->setProperty("id", f.getFullPathName());
                    obj->setProperty("name", name);
                    obj->setProperty("vendor", "");
                    obj->setProperty("category", "instrument");
                    instruments.add(juce::var(obj));
                }
            }
            
            // Add Kick 2 (AudioUnit only)
            juce::File kick2AU("/Library/Audio/Plug-Ins/Components/Kick 2.component");
            if (kick2AU.exists()) {
                juce::DynamicObject* obj = new juce::DynamicObject();
                obj->setProperty("id", kick2AU.getFullPathName());
                obj->setProperty("name", "Kick 2");
                obj->setProperty("vendor", "Sonic Academy");
                obj->setProperty("category", "instrument");
                instruments.add(juce::var(obj));
            }
            
            // Curated channel strip / effects list
            juce::Array<juce::var> channelStrips;
            juce::StringArray stripNames = {
                "Console 1", "American Class A", "British Class A",
                "Weiss DS1-MK3", "Summit Audio Grand Channel"
            };
            for (auto& name : stripNames) {
                // Try VST3 first, then AU
                juce::File vst3("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3");
                juce::File au("/Library/Audio/Plug-Ins/Components/" + name + ".component");
                juce::String path = vst3.exists() ? vst3.getFullPathName()
                                  : au.exists()   ? au.getFullPathName()
                                  : "";
                if (path.isNotEmpty()) {
                    juce::DynamicObject* obj = new juce::DynamicObject();
                    obj->setProperty("id", path);
                    obj->setProperty("name", name);
                    obj->setProperty("vendor", "");
                    obj->setProperty("category", "channel-strip");
                    channelStrips.add(juce::var(obj));
                }
            }

            // Curated FX list
            juce::Array<juce::var> fxPlugins;
            juce::StringArray fxNames = {
                "Tube Delay", "ValhallaRoom", "Widener", "soothe2", "Dist TUBE-CULTURE"
            };
            for (auto& name : fxNames) {
                juce::File vst3("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3");
                juce::File au("/Library/Audio/Plug-Ins/Components/" + name + ".component");
                juce::String path = vst3.exists() ? vst3.getFullPathName()
                                  : au.exists()   ? au.getFullPathName()
                                  : "";
                // Sometimes Valhalla or others might be in a vendor subfolder for VST3, 
                // but the scanner just needs an ID to tell the UI. 
                // We'll use the path if found, or just the name as ID if we have to, 
                // though Tracktion Engine scan will find it.
                // Let's just output it anyway so the UI knows it's available.
                juce::DynamicObject* obj = new juce::DynamicObject();
                obj->setProperty("id", path.isNotEmpty() ? path : name);
                obj->setProperty("name", name);
                obj->setProperty("vendor", "");
                obj->setProperty("category", "fx");
                fxPlugins.add(juce::var(obj));
            }
            
            juce::DynamicObject* result = new juce::DynamicObject();
            result->setProperty("instruments", instruments);
            result->setProperty("effects", channelStrips);
            result->setProperty("fx", fxPlugins);
            complete(juce::JSON::toString(juce::var(result)));
        })
        // Get Gemini API key from application settings
        .withNativeFunction("getApiKey", [](auto&, auto complete) {
            // Helper to create Songbird-specific PropertiesFile options
            juce::PropertiesFile::Options opts;
            opts.applicationName = "Songbird Player";
            opts.filenameSuffix = ".settings";
            opts.osxLibrarySubFolder = "Application Support";
            opts.folderName = juce::String("Songbird") +
                              juce::File::getSeparatorString() +
                              juce::String("Songbird Player");
            opts.storageFormat = juce::PropertiesFile::storeAsXML;

            juce::ApplicationProperties appProps;
            appProps.setStorageParameters(opts);
            
            auto* settings = appProps.getUserSettings();
            DBG("ApiKey: Loading from " + settings->getFile().getFullPathName());
            
            juce::String key = settings->getValue("gemini-api-key", juce::String());
            DBG("ApiKey: Found key? " + juce::String(key.isNotEmpty() ? "Yes" : "No"));

            complete(key);
        })
        // Save Gemini API key to application settings
        .withNativeFunction("setApiKey", [](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String key = args[0].toString();

                juce::PropertiesFile::Options opts;
                opts.applicationName = "Songbird Player";
                opts.filenameSuffix = ".settings";
                opts.osxLibrarySubFolder = "Application Support";
                opts.folderName = juce::String("Songbird") +
                                  juce::File::getSeparatorString() +
                                  juce::String("Songbird Player");
                opts.storageFormat = juce::PropertiesFile::storeAsXML;

                juce::ApplicationProperties appProps;
                appProps.setStorageParameters(opts);
                appProps.getUserSettings()->setValue("gemini-api-key", key);
                appProps.saveIfNeeded();
                DBG("ApiKey: saved to Songbird Player settings");
            }
            complete("ok");
        })

        // ====================================================================
        // MIDI Recording
        // ====================================================================

        .withNativeFunction("listMidiInputs", [this](auto&, auto complete) {
            auto devices = MidiRecorder::listMidiDevices();
            juce::Array<juce::var> arr;
            for (auto& d : devices)
                arr.add(juce::var(d));
            complete(juce::JSON::toString(juce::var(arr)));
        })

        .withNativeFunction("setMidiInputDevice", [this](auto& args, auto complete) {
            if (args.size() > 0 && midiRecorder) {
                juce::String name = args[0].toString();
                bool ok = midiRecorder->openDevice(name);
                complete(ok ? "{\"success\":true}" : "{\"success\":false}");
            } else complete("{\"success\":false}");
        })

        .withNativeFunction("setMidiRecordArm", [this](auto& args, auto complete) {
            { complete("{\"success\":false}"); return; }
            int  trackId = static_cast<int>(args[0]);
            bool armed   = static_cast<bool>(args[1]);
            juce::MessageManager::callAsync([this, trackId, armed, complete = std::move(complete)]() mutable {
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId < 0 || trackId >= (int)audioTracks.size())
                { complete("{\"success\":false}"); return; }
                auto* track = audioTracks[trackId];
                if (armed) {
                    double beatNow = edit->tempoSequence.toBeats(edit->getTransport().getPosition()).inBeats();
                    midiRecorder->startRecording(track, beatNow);
                    midiRecordTrackId = trackId;
                } else {
                    midiRecorder->stopRecording();
                    midiRecordTrackId = -1;
                }
                complete("{\"success\":true}");
            });
        })

        .withNativeFunction("clearRecordedMidi", [this](auto& args, auto complete) {
            int trackId = static_cast<int>(args[0]);
            juce::MessageManager::callAsync([this, trackId]() {
                auto tracks = te::getAudioTracks(*edit);
                if (trackId >= 0 && trackId < (int)tracks.size()) {
                    for (auto* clip : tracks[trackId]->getClips())
                        clip->state.getParent().removeChild(clip->state, nullptr);
                    BirdLoader::populateEdit(*edit, lastParseResult, engine);
                    if (webView)
                        webView->emitEventIfBrowserIsVisible("trackState", juce::var(BirdLoader::getTrackStateJSON(*edit, &lastParseResult)));
                }
            });
            complete("{\"success\":true}");
        })

        // ====================================================================
        // Audio Tracks
        // ====================================================================

        .withNativeFunction("listAudioInputs", [](auto&, auto complete) {
            auto names = AudioRecorder::listAudioInputs();
            juce::Array<juce::var> arr;
            for (auto& n : names) arr.add(juce::var(n));
            complete(juce::JSON::toString(juce::var(arr)));
        })

        .withNativeFunction("addAudioTrack", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this, complete = std::move(complete)]() mutable {
                int id = audioRecorder->addAudioTrack();
                if (webView)
                    webView->emitEventIfBrowserIsVisible("trackState", juce::var(BirdLoader::getTrackStateJSON(*edit, &lastParseResult)));
                complete("{\"success\":true,\"trackId\":" + juce::String(id) + "}");
            });
        })

        .withNativeFunction("removeAudioTrack", [this](auto& args, auto complete) {
            int trackId = static_cast<int>(args[0]);
            juce::MessageManager::callAsync([this, trackId]() {
                audioRecorder->removeAudioTrack(trackId);
                if (webView)
                    webView->emitEventIfBrowserIsVisible("trackState", juce::var(BirdLoader::getTrackStateJSON(*edit, &lastParseResult)));
            });
            complete("{\"success\":true}");
        })

        .withNativeFunction("setAudioRecordSource", [this](auto& args, auto complete) {
            int trackId = static_cast<int>(args[0]);
            juce::String type = args[1].toString();
            juce::MessageManager::callAsync([this, trackId, type, args, complete = std::move(complete)]() mutable {
                if (type == "hardware")
                    audioRecorder->setHardwareInputSource(trackId, args.size() > 2 ? args[2].toString() : "");
                else if (type == "loopback")
                    audioRecorder->setLoopbackSource(trackId, args.size() > 2 ? (int)args[2] : -1);
                complete("{\"success\":true}");
            });
        })

        .withNativeFunction("setAudioRecordArm", [this](auto& args, auto complete) {
            int  trackId = static_cast<int>(args[0]);
            bool armed   = static_cast<bool>(args[1]);
            juce::MessageManager::callAsync([this, trackId, armed, complete = std::move(complete)]() mutable {
                if (armed) audioRecorder->startRecording(trackId);
                else       audioRecorder->stopRecording(trackId);
                complete("{\"success\":true}");
            });
        })

        // ====================================================================
        // Per-track Lyria control
        // ====================================================================

        .withNativeFunction("setLyriaTrackConfig", [this](auto& args, auto complete) {
            if (args.size() < 2) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            auto config = juce::JSON::parse(args[1].toString());
            juce::MessageManager::callAsync([this, trackId, config]() { setLyriaTrackConfig(trackId, config); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("setLyriaTrackPrompts", [this](auto& args, auto complete) {
            if (args.size() < 2) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            auto prompts = juce::JSON::parse(args[1].toString());
            juce::MessageManager::callAsync([this, trackId, prompts]() { setLyriaTrackPrompts(trackId, prompts); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("setLyriaQuantize", [this](auto& args, auto complete) {
            if (args.size() < 2) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            int bars    = static_cast<int>(args[1]);
            juce::MessageManager::callAsync([this, trackId, bars]() { setLyriaQuantize(trackId, bars); });
            complete("{\"success\":true}");
        })

        // ====================================================================
        // MIDI Note Editing (Piano Roll Editor)
        // ====================================================================
        //
        // Individual note operations: each modifies a single note in the
        // Tracktion MIDI clip (instant on message thread). Bird file write
        // and trackState emit are debounced via scheduleMidiCommit().

        // --- Add a MIDI note ---
        // Args: (trackId, sectionName, pitch, beat, duration, velocity)
        //   beat is relative to section start, in beats (0-based)
        .withNativeFunction("midiAddNote", [this](auto& args, auto complete) {
            if (!edit || !currentBirdFile.existsAsFile() || args.size() < 6) {
                complete("{\"success\":false}"); return;
            }
            int trackId          = static_cast<int>(args[0]);
            juce::String secName = args[1].toString();
            int pitch            = static_cast<int>(args[2]);
            double beat          = static_cast<double>(args[3]);
            double duration      = static_cast<double>(args[4]);
            int velocity         = static_cast<int>(args[5]);

            juce::MessageManager::callAsync([this, trackId, secName, pitch, beat, duration, velocity]() {
                double secOffset = 0.0;
                int secBars = 4;
                for (auto& e : lastParseResult.arrangement) {
                    if (juce::String(e.sectionName) == secName) { secBars = e.bars; break; }
                    secOffset += e.bars * 4.0;
                }

                // Add note to Tracktion clip (instant)
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId >= 0 && trackId < (int)audioTracks.size()) {
                    te::MidiClip* mc = nullptr;
                    for (auto* clip : audioTracks[trackId]->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                    if (mc) {
                        mc->getSequence().addNote(pitch,
                            te::BeatPosition::fromBeats(secOffset + beat),
                            te::BeatDuration::fromBeats(duration * 0.9),
                            velocity, 0, nullptr);
                    }
                }

                // Debounce: bird write + trackState + commit happen after edits settle
                pendingMidiEdit = { trackId, secName, secOffset, secBars };
                scheduleMidiCommit();
            });
            complete("{\"success\":true}");
        })

        // --- Remove a MIDI note ---
        // Args: (trackId, sectionName, pitch, beat)
        .withNativeFunction("midiRemoveNote", [this](auto& args, auto complete) {
            if (!edit || !currentBirdFile.existsAsFile() || args.size() < 4) {
                complete("{\"success\":false}"); return;
            }
            int trackId          = static_cast<int>(args[0]);
            juce::String secName = args[1].toString();
            int pitch            = static_cast<int>(args[2]);
            double beat          = static_cast<double>(args[3]);

            juce::MessageManager::callAsync([this, trackId, secName, pitch, beat]() {
                double secOffset = 0.0;
                int secBars = 4;
                for (auto& e : lastParseResult.arrangement) {
                    if (juce::String(e.sectionName) == secName) { secBars = e.bars; break; }
                    secOffset += e.bars * 4.0;
                }

                double absBeat = secOffset + beat;
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId >= 0 && trackId < (int)audioTracks.size()) {
                    te::MidiClip* mc = nullptr;
                    for (auto* clip : audioTracks[trackId]->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                    if (mc) {
                        auto& seq = mc->getSequence();
                        for (int i = seq.getNumNotes() - 1; i >= 0; --i) {
                            auto* note = seq.getNote(i);
                            if (note->getNoteNumber() == pitch &&
                                std::abs(note->getStartBeat().inBeats() - absBeat) < 0.05) {
                                seq.removeNote(*note, nullptr);
                                break;
                            }
                        }
                    }
                }

                pendingMidiEdit = { trackId, secName, secOffset, secBars };
                scheduleMidiCommit();
            });
            complete("{\"success\":true}");
        })

        // --- Move / resize a MIDI note ---
        // Args: (trackId, sectionName, oldPitch, oldBeat, newPitch, newBeat, newDuration)
        .withNativeFunction("midiMoveNote", [this](auto& args, auto complete) {
            if (!edit || !currentBirdFile.existsAsFile() || args.size() < 7) {
                complete("{\"success\":false}"); return;
            }
            int trackId          = static_cast<int>(args[0]);
            juce::String secName = args[1].toString();
            int oldPitch         = static_cast<int>(args[2]);
            double oldBeat       = static_cast<double>(args[3]);
            int newPitch         = static_cast<int>(args[4]);
            double newBeat       = static_cast<double>(args[5]);
            double newDuration   = static_cast<double>(args[6]);

            juce::MessageManager::callAsync([this, trackId, secName, oldPitch, oldBeat, newPitch, newBeat, newDuration]() {
                double secOffset = 0.0;
                int secBars = 4;
                for (auto& e : lastParseResult.arrangement) {
                    if (juce::String(e.sectionName) == secName) { secBars = e.bars; break; }
                    secOffset += e.bars * 4.0;
                }

                double absOld = secOffset + oldBeat;
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId >= 0 && trackId < (int)audioTracks.size()) {
                    te::MidiClip* mc = nullptr;
                    for (auto* clip : audioTracks[trackId]->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                    if (mc) {
                        auto& seq = mc->getSequence();
                        int vel = 100;
                        for (int i = seq.getNumNotes() - 1; i >= 0; --i) {
                            auto* note = seq.getNote(i);
                            if (note->getNoteNumber() == oldPitch &&
                                std::abs(note->getStartBeat().inBeats() - absOld) < 0.05) {
                                vel = note->getVelocity();
                                seq.removeNote(*note, nullptr);
                                break;
                            }
                        }
                        seq.addNote(newPitch,
                            te::BeatPosition::fromBeats(secOffset + newBeat),
                            te::BeatDuration::fromBeats(newDuration * 0.9),
                            vel, 0, nullptr);
                    }
                }

                pendingMidiEdit = { trackId, secName, secOffset, secBars };
                scheduleMidiCommit();
            });
            complete("{\"success\":true}");
        })

        // ====================================================================
        // Project Undo / Redo / History
        // ====================================================================

        .withNativeFunction("undo", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() { undoProject(); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("redo", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() { redoProject(); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("revertLLM", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() { revertLLM(); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("commitProject", [this](auto& args, auto complete) {
            juce::String message = args.size() > 0 ? args[0].toString() : "Manual save";
            juce::MessageManager::callAsync([this, message]() {
                saveEditState();
                commitAndNotify(message, ProjectState::User);
            });
            complete("{\"success\":true}");
        });
}

