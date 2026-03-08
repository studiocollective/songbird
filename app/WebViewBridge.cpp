#include "SongbirdEditor.h"
#include "WebViewHelpers.h"

//==============================================================================
// WebView native function bridge
//==============================================================================

juce::WebBrowserComponent::Options SongbirdEditor::createWebViewOptions()
{
    // Helper: rewrite the bpm/scale metadata block in the bird file.
    // Places it after the leading comment header, separated by blank lines.
    auto updateBirdMetaSection = [this]() {
        if (!currentBirdFile.existsAsFile()) return;
        auto content = currentBirdFile.loadFileAsString();
        juce::StringArray lines;
        lines.addLines(content);

        // Remove existing bpm and scale lines
        for (int i = lines.size() - 1; i >= 0; i--) {
            auto trimmed = lines[i].trimStart();
            if (trimmed.startsWith("bpm ") ||
                (trimmed.startsWith("scale ") && !trimmed.startsWith("scale_")))
                lines.remove(i);
        }

        // Find the end of the leading comment header (only # lines, not blanks)
        int headerEnd = 0;
        while (headerEnd < lines.size() && lines[headerEnd].trimStart().startsWith("#"))
            headerEnd++;

        // Strip ALL blank lines between header and first content line
        while (headerEnd < lines.size() && lines[headerEnd].trim().isEmpty())
            lines.remove(headerEnd);

        // Build meta lines
        juce::StringArray meta;
        if (lastParseResult.bpm > 0)
            meta.add("bpm " + juce::String(lastParseResult.bpm));
        if (!lastParseResult.scaleRoot.empty() && !lastParseResult.scaleMode.empty())
            meta.add("scale " + juce::String(lastParseResult.scaleRoot) + " " + juce::String(lastParseResult.scaleMode));

        // Insert meta section after the header with single blank line separators
        if (meta.size() > 0) {
            // Blank line after meta block (before content)
            lines.insert(headerEnd, "");
            // Insert meta lines
            for (int i = meta.size() - 1; i >= 0; i--)
                lines.insert(headerEnd, meta[i]);
            // Blank line before meta block (after header) — only if there's a header
            if (headerEnd > 0)
                lines.insert(headerEnd, "");
        }

        auto newContent = lines.joinIntoString("\n") + "\n";
        // Skip write if nothing actually changed (avoids spurious reload triggers)
        if (newContent != content)
            currentBirdFile.replaceWithText(newContent);
    };

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
                // Source tag for the UI to show who made the commit
                juce::String sourceStr = "system";
                if (entry.source == ProjectState::LLM)      sourceStr = "ai";
                else if (entry.source == ProjectState::User) sourceStr = "user";
                else if (entry.source == ProjectState::Mixer) sourceStr = "user";
                else if (entry.source == ProjectState::Plugin) sourceStr = "user";
                obj->setProperty("author", sourceStr);
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
        })

        // Set project BPM
        .withNativeFunction("setBpm", [this, updateBirdMetaSection](auto& args, auto complete) {
            if (!edit || args.size() < 1) { complete("{\"success\":false}"); return; }
            double bpm = static_cast<double>(args[0]);
            if (bpm < 20.0 || bpm > 300.0) { complete("{\"success\":false,\"error\":\"BPM out of range 20-300\"}"); return; }
            juce::MessageManager::callAsync([this, bpm, updateBirdMetaSection, complete = std::move(complete)]() mutable {
                edit->tempoSequence.getTempos()[0]->setBpm(bpm);
                lastParseResult.bpm = static_cast<int>(bpm);
                updateBirdMetaSection();
                // Notify bird file panel of content change
                if (webView)
                    webView->emitEventIfBrowserIsVisible("birdContentChanged", juce::var("ok"));
                DBG("Transport: BPM set to " + juce::String(bpm));
                complete("{\"success\":true,\"bpm\":" + juce::String(bpm) + "}");
            });
        })
        // Set project scale (lightweight — no re-parse / populateEdit)
        .withNativeFunction("setProjectScale", [this, updateBirdMetaSection](auto& args, auto complete) {
            // Args: (root: string, mode: string) or ("", "") to clear
            juce::MessageManager::callAsync([this, args, updateBirdMetaSection, complete = std::move(complete)]() mutable {
                juce::String root = args.size() > 0 ? args[0].toString() : "";
                juce::String mode = args.size() > 1 ? args[1].toString() : "";

                lastParseResult.scaleRoot = root.toStdString();
                lastParseResult.scaleMode = mode.toStdString();
                updateBirdMetaSection();

                // Notify bird file panel — no trackState emission needed,
                // scale is already set optimistically in React by setScale()
                if (webView)
                    webView->emitEventIfBrowserIsVisible("birdContentChanged", juce::var("ok"));

                DBG("Scale: set to " + root + " " + mode);
                complete("{\"success\":true}");
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
        .withNativeFunction("transportPause", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                if (edit) {
                    edit->getTransport().stop(false, false);
                    DBG("Transport: Paused (Native)");
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
                    if (!targetFile.existsAsFile())
                        targetFile.create();
                    targetFile.replaceWithText(content);
                    currentBirdFile = targetFile;

                    // Soft reload: parse + populateEdit (diffs against live state)
                    auto result = BirdLoader::parse(currentBirdFile.getFullPathName().toStdString());
                    if (result.error.empty()) {
                        BirdLoader::populateEdit(*edit, result, engine, nullptr);
                        lastParseResult = result;
                    }
                    emitTrackState();
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

        // Per-track MIDI input selection (from React RecordStrip)
        // Args: (trackId, inputType)
        //   inputType: "" or "all" = open all devices, "computer-keyboard" = close hardware,
        //              or a specific device name
        .withNativeFunction("setMidiInput", [this](auto& args, auto complete) {
            if (args.size() < 2 || !midiRecorder) { complete("{\"success\":false}"); return; }
            juce::String inputType = args[1].toString();
            juce::MessageManager::callAsync([this, inputType]() {
                if (inputType.isEmpty() || inputType == "all") {
                    // Open first available MIDI device (or keep current)
                    auto devices = MidiRecorder::listMidiDevices();
                    if (devices.size() > 0 && midiRecorder->getOpenDeviceName().isEmpty())
                        midiRecorder->openDevice(devices[0]);
                    DBG("setMidiInput: All Inputs");
                } else if (inputType == "computer-keyboard") {
                    // Close hardware device — keyboard MIDI is injected via sendKeyboardMidi
                    midiRecorder->closeDevice();
                    DBG("setMidiInput: Computer Keyboard");
                } else {
                    // Open specific device
                    midiRecorder->openDevice(inputType);
                    DBG("setMidiInput: " + inputType);
                }
            });
            complete("{\"success\":true}");
        })

        // Toggle input monitoring on a track
        // Args: (trackId, enabled)
        // Note: actual audio monitoring pass-through requires AudioRecorder support;
        // this stores the intent and logs it for now.
        .withNativeFunction("setInputMonitoring", [this](auto& args, auto complete) {
            if (args.size() < 2 || !edit) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            bool enabled = static_cast<bool>(args[1]);
            DBG("setInputMonitoring: track " + juce::String(trackId) + " = " + (enabled ? "ON" : "OFF"));
            // TODO: Wire up audio pass-through via AudioRecorder for live monitoring
            complete("{\"success\":true}");
        })

        // Send keyboard MIDI note (from computer keyboard used as MIDI input)
        // Args: (noteNumber, velocity)
        //   velocity > 0 = note-on, velocity == 0 = note-off
        .withNativeFunction("sendKeyboardMidi", [this](auto& args, auto complete) {
            if (args.size() < 2 || !edit) { complete("{\"success\":false}"); return; }
            int note     = static_cast<int>(args[0]);
            int velocity = static_cast<int>(args[1]);

            juce::MessageManager::callAsync([this, note, velocity]() {
                // Find the armed MIDI track and inject the note into it
                auto audioTracks = te::getAudioTracks(*edit);
                te::AudioTrack* targetTrack = nullptr;

                // If we have an armed MIDI recording track, use that
                if (midiRecordTrackId >= 0 && midiRecordTrackId < (int)audioTracks.size())
                    targetTrack = audioTracks[midiRecordTrackId];

                if (!targetTrack) {
                    // Fall back: find the first track with a loaded instrument plugin
                    for (auto* track : audioTracks) {
                        for (auto* plugin : track->pluginList) {
                            if (dynamic_cast<te::ExternalPlugin*>(plugin)) {
                                targetTrack = track;
                                break;
                            }
                        }
                        if (targetTrack) break;
                    }
                }

                if (targetTrack) {
                    juce::MidiMessage msg = velocity > 0
                        ? juce::MidiMessage::noteOn(1, note, (juce::uint8)velocity)
                        : juce::MidiMessage::noteOff(1, note);

                    // Inject into Tracktion's audio graph — this flows through
                    // the track's plugin chain and produces audible output
                    targetTrack->injectLiveMidiMessage(msg, {});

                    // If recording is armed, also feed to MidiRecorder
                    if (midiRecorder && midiRecorder->isRecording()) {
                        midiRecorder->handleIncomingMidiMessage(nullptr, msg);
                    }

                    DBG("sendKeyboardMidi: note=" + juce::String(note) + " vel=" + juce::String(velocity)
                        + " -> track " + targetTrack->getName());
                }
            });
            complete("{\"success\":true}");
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
                    // Emit lightweight notesChanged for just this track (not full trackState)
                    if (webView) {
                        juce::String notesJson = "{\"trackId\":" + juce::String(trackId) + ",\"notes\":[";
                        te::MidiClip* mc = nullptr;
                        for (auto* clip : tracks[trackId]->getClips())
                            if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                        if (mc) {
                            auto& seq = mc->getSequence();
                            for (int n = 0; n < seq.getNumNotes(); n++) {
                                auto* note = seq.getNote(n);
                                if (n > 0) notesJson += ",";
                                notesJson += "{\"pitch\":" + juce::String(note->getNoteNumber()) +
                                             ",\"beat\":" + juce::String(note->getStartBeat().inBeats(), 3) +
                                             ",\"duration\":" + juce::String(note->getLengthBeats().inBeats(), 3) +
                                             ",\"velocity\":" + juce::String(note->getVelocity()) + "}";
                            }
                        }
                        notesJson += "]}";
                        webView->emitEventIfBrowserIsVisible("notesChanged", juce::var(notesJson));
                    }
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

        // ====================================================================
        // Audio Device Settings (for Settings panel)
        // ====================================================================

        .withNativeFunction("getAudioDeviceInfo", [this](auto&, auto complete) {
            auto& dm = engine.getDeviceManager().deviceManager;
            auto* device = dm.getCurrentAudioDevice();
            juce::DynamicObject* result = new juce::DynamicObject();
            if (device) {
                result->setProperty("deviceName", device->getName());
                result->setProperty("deviceType", device->getTypeName());
                result->setProperty("sampleRate", device->getCurrentSampleRate());
                result->setProperty("bufferSize", device->getCurrentBufferSizeSamples());
                result->setProperty("inputLatency", device->getInputLatencyInSamples());
                result->setProperty("outputLatency", device->getOutputLatencyInSamples());

                // Available buffer sizes
                juce::Array<juce::var> bufSizes;
                for (auto sz : device->getAvailableBufferSizes())
                    bufSizes.add(juce::var(sz));
                result->setProperty("availableBufferSizes", bufSizes);

                // Available sample rates
                juce::Array<juce::var> sampleRates;
                for (auto sr : device->getAvailableSampleRates())
                    sampleRates.add(juce::var(sr));
                result->setProperty("availableSampleRates", sampleRates);

                // Input channel names
                juce::Array<juce::var> inputs;
                for (auto& n : device->getInputChannelNames())
                    inputs.add(juce::var(n));
                result->setProperty("inputChannels", inputs);

                // Output channel names
                juce::Array<juce::var> outputs;
                for (auto& n : device->getOutputChannelNames())
                    outputs.add(juce::var(n));
                result->setProperty("outputChannels", outputs);
            }

            // Available audio device names (for device switching)
            juce::Array<juce::var> deviceNames;
            for (auto type : dm.getAvailableDeviceTypes()) {
                for (auto& name : type->getDeviceNames(false))  // output devices
                    deviceNames.add(juce::var(name));
            }
            result->setProperty("availableDevices", deviceNames);

            complete(juce::JSON::toString(juce::var(result)));
        })

        .withNativeFunction("listAudioOutputs", [this](auto&, auto complete) {
            auto& dm = engine.getDeviceManager().deviceManager;
            juce::Array<juce::var> arr;
            for (auto type : dm.getAvailableDeviceTypes()) {
                for (auto& name : type->getDeviceNames(false))  // false = output devices
                    arr.add(juce::var(name));
            }
            complete(juce::JSON::toString(juce::var(arr)));
        })

        .withNativeFunction("setAudioDevice", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            juce::String deviceName = args[0].toString();
            auto& dm = engine.getDeviceManager().deviceManager;
            auto setup = dm.getAudioDeviceSetup();
            setup.outputDeviceName = deviceName;
            setup.inputDeviceName = deviceName;  // typically same device on macOS
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty()) {
                DBG("setAudioDevice: switched to " + deviceName);
                complete("{\"success\":true}");
            } else {
                DBG("setAudioDevice: error " + err);
                complete("{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
            }
        })

        .withNativeFunction("setAudioSampleRate", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            double sampleRate = static_cast<double>(args[0]);
            auto& dm = engine.getDeviceManager().deviceManager;
            auto setup = dm.getAudioDeviceSetup();
            setup.sampleRate = sampleRate;
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty()) {
                DBG("setAudioSampleRate: set to " + juce::String(sampleRate));
                complete("{\"success\":true}");
            } else {
                DBG("setAudioSampleRate: error " + err);
                complete("{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
            }
        })

        .withNativeFunction("setAudioBufferSize", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            int bufferSize = static_cast<int>(args[0]);
            auto& dm = engine.getDeviceManager().deviceManager;
            auto* device = dm.getCurrentAudioDevice();
            if (device) {
                auto setup = dm.getAudioDeviceSetup();
                setup.bufferSize = bufferSize;
                auto err = dm.setAudioDeviceSetup(setup, true);
                if (err.isEmpty())
                    complete("{\"success\":true}");
                else
                    complete("{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
            } else {
                complete("{\"success\":false,\"error\":\"No audio device\"}");
            }
        })

        .withNativeFunction("addAudioTrack", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this, complete = std::move(complete)]() mutable {
                if (!audioRecorder || !edit) { complete("{\"success\":false}"); return; }
                int targetIndex = static_cast<int>(lastParseResult.channels.size());
                int id = audioRecorder->addAudioTrack(targetIndex);
                auto audioTracks = te::getAudioTracks(*edit);
                auto* track = (id >= 0 && id < (int)audioTracks.size()) ? audioTracks[id] : nullptr;
                juce::String name = track ? track->getName() : ("audio" + juce::String(id + 1));
                int vol = track && track->getVolumePlugin()
                    ? static_cast<int>(std::round(juce::Decibels::decibelsToGain(track->getVolumePlugin()->getVolumeDb()) * 127.0))
                    : 80;
                int pan = track && track->getVolumePlugin()
                    ? static_cast<int>(std::round(track->getVolumePlugin()->getPan() * 64.0f))
                    : 0;
                juce::String trackJson = "{\"success\":true,\"trackId\":" + juce::String(id)
                    + ",\"name\":\"" + name.replace("\"", "\\\"") + "\""
                    + ",\"trackType\":\"audio\""
                    + ",\"volume\":" + juce::String(vol)
                    + ",\"pan\":" + juce::String(pan)
                    + "}";
                
                // Register in lastParseResult so it persists
                BirdChannel newCh;
                newCh.channel = id;
                newCh.name = name.toStdString();
                lastParseResult.channels.push_back(newCh);

                if (lastParseResult.arrangement.size() > 0) {
                    // Inject global channel definition into the raw .bird file
                    if (currentBirdFile.existsAsFile()) {
                        auto birdText = currentBirdFile.loadFileAsString();
                        auto lines = juce::StringArray::fromLines(birdText);
                        
                        // Check if it already exists globally
                        juce::String chMarker = "ch " + juce::String(id + 1) + " " + name;
                        bool foundGlobal = false;
                        int insertIdx = lines.size();
                        
                        for (int i = 0; i < lines.size(); ++i) {
                            auto trimmed = lines[i].trim();
                            if (trimmed == chMarker || trimmed.startsWith(chMarker + " ")) {
                                foundGlobal = true;
                                break;
                            }
                            // Stop searching for a global def once we hit arr or sec
                            if (trimmed == "arr" || trimmed.startsWith("sec ")) {
                                insertIdx = i;
                                break;
                            }
                        }

                        if (!foundGlobal) {
                            while (insertIdx > 0 && lines[insertIdx - 1].trim().isEmpty()) {
                                insertIdx--;
                            }
                            lines.insert(insertIdx, chMarker);
                            lines.insert(insertIdx + 1, "  type audio");
                            lines.insert(insertIdx + 2, "  strip console1");
                            currentBirdFile.replaceWithText(lines.joinIntoString("\n"));
                        }
                    }

                    juce::String secNameArg = lastParseResult.arrangement[0].sectionName;
                    int secBars = lastParseResult.arrangement[0].bars;
                    juce::Thread::launch([this, id, secName = juce::String(secNameArg), secBars]() {
                        writeBirdFromClip(id, secName, 0.0, secBars, {});
                    });
                }
                
                commitAndNotify("Add audio track", ProjectState::User);
                
                complete(trackJson);
            });
        })

        .withNativeFunction("addMidiTrack", [this](auto& args, auto complete) {
            juce::MessageManager::callAsync([this, complete = std::move(complete)]() mutable {
                if (!edit) { complete("{\"success\":false}"); return; }

                int targetIndex = static_cast<int>(lastParseResult.channels.size());
                auto allTracks = te::getAudioTracks(*edit);
                te::Track* preceding = targetIndex > 0 && targetIndex <= (int)allTracks.size() ? allTracks[targetIndex - 1] : nullptr;
                auto track = edit->insertNewAudioTrack(te::TrackInsertPoint(nullptr, preceding), nullptr);
                if (!track) { complete("{\"success\":false}"); return; }

                int id = targetIndex;
                track->setName("track" + juce::String(id + 1));

                // Set default volume (0 dB) and pan (center)
                if (auto vp = track->getVolumePlugin()) {
                    vp->setVolumeDb(0.0f);
                    vp->setPan(0.0f);
                }

                // Add 4 AuxSend plugins (for return bus sends) — matching populateEdit
                for (int bus = 0; bus < 4; bus++) {
                    if (auto plugin = edit->getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {})) {
                        track->pluginList.insertPlugin(*plugin, -1, nullptr);
                        auto* sendPlugin = dynamic_cast<te::AuxSendPlugin*>(plugin.get());
                        if (sendPlugin) {
                            sendPlugin->busNumber = bus;
                            sendPlugin->setGainDb(-100.0f); // Default to muted
                        }
                    }
                }

                // Create an empty MIDI clip spanning the project
                double totalBeats = lastParseResult.bars * 4.0;
                if (totalBeats < 4.0) totalBeats = 16.0;
                auto clipRange = te::TimeRange(
                    edit->tempoSequence.toTime(te::BeatPosition::fromBeats(0.0)),
                    edit->tempoSequence.toTime(te::BeatPosition::fromBeats(totalBeats)));
                track->insertMIDIClip(clipRange, nullptr);

                juce::String trackJson = "{\"success\":true,\"trackId\":" + juce::String(id)
                    + ",\"name\":\"" + track->getName().replace("\"", "\\\"") + "\""
                    + ",\"trackType\":\"midi\""
                    + ",\"volume\":" + juce::String(static_cast<int>(std::round((track->getVolumePlugin() ? juce::Decibels::decibelsToGain(track->getVolumePlugin()->getVolumeDb()) : 0.5) * 127.0)))
                    + ",\"pan\":" + juce::String(static_cast<int>(std::round((track->getVolumePlugin() ? track->getVolumePlugin()->getPan() : 0.0f) * 64.0f)))
                    + "}";

                // Register in lastParseResult so note drawing works
                BirdChannel newCh;
                newCh.channel = id;
                newCh.name = track->getName().toStdString();
                lastParseResult.channels.push_back(newCh);

                if (lastParseResult.arrangement.size() > 0) {
                    // Inject global channel definition into the raw .bird file
                    if (currentBirdFile.existsAsFile()) {
                        auto birdText = currentBirdFile.loadFileAsString();
                        auto lines = juce::StringArray::fromLines(birdText);
                        
                        // Check if it already exists globally
                        juce::String chMarker = "ch " + juce::String(id + 1) + " " + track->getName();
                        bool foundGlobal = false;
                        int insertIdx = lines.size();
                        
                        for (int i = 0; i < lines.size(); ++i) {
                            auto trimmed = lines[i].trim();
                            if (trimmed == chMarker || trimmed.startsWith(chMarker + " ")) {
                                foundGlobal = true;
                                break;
                            }
                            // Stop searching for a global def once we hit arr or sec
                            if (trimmed == "arr" || trimmed.startsWith("sec ")) {
                                insertIdx = i;
                                break;
                            }
                        }

                        if (!foundGlobal) {
                            while (insertIdx > 0 && lines[insertIdx - 1].trim().isEmpty()) {
                                insertIdx--;
                            }
                            lines.insert(insertIdx, chMarker);
                            lines.insert(insertIdx + 1, "  type midi");
                            lines.insert(insertIdx + 2, "  strip console1");
                            currentBirdFile.replaceWithText(lines.joinIntoString("\n"));
                        }
                    }

                    juce::String secNameArg = lastParseResult.arrangement[0].sectionName;
                    int secBars = lastParseResult.arrangement[0].bars;
                    juce::Thread::launch([this, id, secName = juce::String(secNameArg), secBars]() {
                        writeBirdFromClip(id, secName, 0.0, secBars, {});
                    });
                }

                commitAndNotify("Add MIDI track", ProjectState::User);

                complete(trackJson);
            });
        })

        .withNativeFunction("removeAudioTrack", [this](auto& args, auto complete) {
            int trackId = static_cast<int>(args[0]);
            juce::MessageManager::callAsync([this, trackId]() {
                audioRecorder->removeAudioTrack(trackId);
                // No trackState emit — JS already filters the track from the store locally
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
            int secIndex         = static_cast<int>(args[1]);
            int pitch            = static_cast<int>(args[2]);
            double beat          = static_cast<double>(args[3]);
            double duration      = static_cast<double>(args[4]);
            int velocity         = static_cast<int>(args[5]);

            juce::MessageManager::callAsync([this, trackId, secIndex, pitch, beat, duration, velocity]() {
                double secOffset = 0.0;
                int secBars = 4;
                juce::String secName = "intro";
                for (int i = 0; i < (int)lastParseResult.arrangement.size(); i++) {
                    if (i == secIndex) { secBars = lastParseResult.arrangement[i].bars; secName = juce::String(lastParseResult.arrangement[i].sectionName); break; }
                    secOffset += lastParseResult.arrangement[i].bars * 4.0;
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
                            te::BeatDuration::fromBeats(duration),
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
            int secIndex         = static_cast<int>(args[1]);
            int pitch            = static_cast<int>(args[2]);
            double beat          = static_cast<double>(args[3]);

            juce::MessageManager::callAsync([this, trackId, secIndex, pitch, beat]() {
                double secOffset = 0.0;
                int secBars = 4;
                juce::String secName = "intro";
                for (int i = 0; i < (int)lastParseResult.arrangement.size(); i++) {
                    if (i == secIndex) { secBars = lastParseResult.arrangement[i].bars; secName = juce::String(lastParseResult.arrangement[i].sectionName); break; }
                    secOffset += lastParseResult.arrangement[i].bars * 4.0;
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
            int secIndex         = static_cast<int>(args[1]);
            int oldPitch         = static_cast<int>(args[2]);
            double oldBeat       = static_cast<double>(args[3]);
            int newPitch         = static_cast<int>(args[4]);
            double newBeat       = static_cast<double>(args[5]);
            double newDuration   = static_cast<double>(args[6]);

            juce::MessageManager::callAsync([this, trackId, secIndex, oldPitch, oldBeat, newPitch, newBeat, newDuration]() {
                double secOffset = 0.0;
                int secBars = 4;
                juce::String secName = "intro";
                for (int i = 0; i < (int)lastParseResult.arrangement.size(); i++) {
                    if (i == secIndex) { secBars = lastParseResult.arrangement[i].bars; secName = juce::String(lastParseResult.arrangement[i].sectionName); break; }
                    secOffset += lastParseResult.arrangement[i].bars * 4.0;
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
                            te::BeatDuration::fromBeats(newDuration),
                            vel, 0, nullptr);
                    }
                }

                pendingMidiEdit = { trackId, secName, secOffset, secBars };
                scheduleMidiCommit();
            });
            complete("{\"success\":true}");
        })
        // --- Set loop length on a MIDI clip ---
        // Args: (trackId, loopBeats)  — 0 = disable looping
        .withNativeFunction("midiSetLoopLength", [this](auto& args, auto complete) {
            if (!edit || args.size() < 2) {
                complete("{\"success\":false}"); return;
            }
            int trackId       = static_cast<int>(args[0]);
            double loopBeats  = static_cast<double>(args[1]);

            juce::MessageManager::callAsync([this, trackId, loopBeats]() {
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId < 0 || trackId >= (int)audioTracks.size()) return;

                te::MidiClip* mc = nullptr;
                for (auto* clip : audioTracks[trackId]->getClips())
                    if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                if (!mc) return;

                if (loopBeats > 0) {
                    // Remove notes beyond the loop boundary
                    auto& seq = mc->getSequence();
                    for (int i = seq.getNumNotes() - 1; i >= 0; --i) {
                        auto* note = seq.getNote(i);
                        if (note->getStartBeat().inBeats() >= loopBeats)
                            seq.removeNote(*note, nullptr);
                    }
                    mc->setLoopRangeBeats(te::BeatRange(
                        te::BeatPosition(),
                        te::BeatDuration::fromBeats(loopBeats)));
                    mc->loopedSequenceType = te::MidiClip::LoopedSequenceType::loopRangeDefinesAllRepetitions;
                    DBG("midiSetLoopLength: track " + juce::String(trackId) + " loop = " + juce::String(loopBeats) + " beats");
                } else {
                    mc->disableLooping();
                    DBG("midiSetLoopLength: track " + juce::String(trackId) + " looping disabled");
                }

                // Emit lightweight notesChanged (notes may have been trimmed at loop boundary)
                if (webView) {
                    juce::String notesJson = "{\"trackId\":" + juce::String(trackId) + ",\"loopLengthBeats\":" + juce::String(loopBeats > 0 ? loopBeats : 0) + ",\"notes\":[";
                    if (mc) {
                        auto& seq2 = mc->getSequence();
                        for (int n = 0; n < seq2.getNumNotes(); n++) {
                            auto* note = seq2.getNote(n);
                            if (n > 0) notesJson += ",";
                            notesJson += "{\"pitch\":" + juce::String(note->getNoteNumber()) +
                                         ",\"beat\":" + juce::String(note->getStartBeat().inBeats(), 3) +
                                         ",\"duration\":" + juce::String(note->getLengthBeats().inBeats(), 3) +
                                         ",\"velocity\":" + juce::String(note->getVelocity()) + "}";
                        }
                    }
                    notesJson += "]}";
                    webView->emitEventIfBrowserIsVisible("notesChanged", juce::var(notesJson));
                }
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

