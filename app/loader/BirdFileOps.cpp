#include "SongbirdEditor.h"

//==============================================================================
// Bird file loading
//==============================================================================

void SongbirdEditor::undoProject()
{
    if (!edit) return;
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    DBG("=== UNDO START ===");
    
    // Prevent React persist echoes from re-applying stale mixer state
    undoRedoInProgress = true;
    
    stopTimer();  // cancel any pending debounce timer
    saveStateCache();  // flush pending mixer state to daw.state.json
    saveEditState();   // flush current plugin state to daw.edit.json
    DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] saveStateCache + saveEditState");
    
    auto changedFiles = projectState.undo();
    DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] projectState.undo() returned " + juce::String(changedFiles.size()) + " files");
    
    if (!changedFiles.isEmpty())
    {
        bool needsHeavyReload = false;
        bool needsSoftReload = false;
        bool stateXmlChanged = false;
        bool stateJsonChanged = false;
        
        for (auto& f : changedFiles) {
            DBG("  changed: " + f.filename);
            if (f.filename.endsWith(".bird")) {
                needsSoftReload = true;
            }
            if (f.filename.endsWith(".edit.json") || f.filename.endsWith(".edit.xml")) {
                stateXmlChanged = true;
            }
            if (f.filename.endsWith(".state.json")) {
                stateJsonChanged = true;
            }
        }

        if (needsHeavyReload) {
            loadBirdFile(currentBirdFile);
        } else {
            if (needsSoftReload) {
                auto result = BirdLoader::parse(currentBirdFile.getFullPathName().toStdString());
                DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] BirdLoader::parse");
                if (result.error.empty()) {
                    BirdLoader::populateEdit(*edit, result, engine, nullptr);
                    DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] populateEdit");
                }
            }
            
            if (stateXmlChanged) {
                loadEditState();
                DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] loadEditState");
            }
            
            // Reload mixer state and apply to Tracktion engine
            if (stateJsonChanged) {
                loadStateCache();
                DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] loadStateCache");
                
                auto mixerIt = stateCache.find("songbird-mixer");
                if (mixerIt != stateCache.end())
                {
                    auto parsed = juce::JSON::parse(mixerIt->second);
                    auto mixerState = parsed.getProperty("state", {});
                    if (mixerState.isObject())
                        applyMixerState(mixerState);
                }
                pushMixerStateToReact();
            }
        }
        
        // Push to React UI
        if (webView && (needsHeavyReload || needsSoftReload))
        {
            // Full refresh only when tracks/notes changed (.bird file)
            // Mixer-only changes are already handled by pushMixerStateToReact() above
            emitTrackState();
        }
        DBG("=== UNDO COMPLETE in " + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms ===");
    }

    if (webView)
        webView->emitEventIfBrowserIsVisible("historyChanged", juce::var("ok"));
    
    // Clear the guard after 200ms so React persist echoes are safely ignored.
    // callAsync is not enough — the echo can arrive on a later message loop iteration.
    juce::Timer::callAfterDelay(200, [this]() { undoRedoInProgress = false; });
}

void SongbirdEditor::redoProject()
{
    if (!edit) return;
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    DBG("=== REDO START ===");
    
    undoRedoInProgress = true;
    
    stopTimer();
    saveStateCache();
    saveEditState();
    
    auto changedFiles = projectState.redo();
    if (!changedFiles.isEmpty())
    {
        bool needsHeavyReload = false;
        bool needsSoftReload = false;
        bool stateXmlChanged = false;
        bool stateJsonChanged = false;
        
        for (auto& f : changedFiles) {
            if (f.filename.endsWith(".bird")) needsSoftReload = true;
            if (f.filename.endsWith(".edit.json") || f.filename.endsWith(".edit.xml")) stateXmlChanged = true;
            if (f.filename.endsWith(".state.json")) stateJsonChanged = true;
        }

        if (needsHeavyReload) {
            loadBirdFile(currentBirdFile);
        } else {
            if (needsSoftReload) {
                auto result = BirdLoader::parse(currentBirdFile.getFullPathName().toStdString());
                if (result.error.empty())
                    BirdLoader::populateEdit(*edit, result, engine, nullptr);
            }
            if (stateXmlChanged)
                loadEditState();
            if (stateJsonChanged) {
                loadStateCache();
                
                auto mixerIt = stateCache.find("songbird-mixer");
                if (mixerIt != stateCache.end())
                {
                    auto parsed = juce::JSON::parse(mixerIt->second);
                    auto mixerState = parsed.getProperty("state", {});
                    if (mixerState.isObject())
                        applyMixerState(mixerState);
                }
                pushMixerStateToReact();
            }
        }
        
        if (webView && (needsHeavyReload || needsSoftReload)) {
            emitTrackState();
        }
        DBG("=== REDO COMPLETE in " + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms ===");
    }

    if (webView)
        webView->emitEventIfBrowserIsVisible("historyChanged", juce::var("ok"));
    
    juce::Timer::callAfterDelay(200, [this]() { undoRedoInProgress = false; });
}

void SongbirdEditor::revertLLM()
{
    if (!edit) return;
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    DBG("=== REVERT START ===");
    saveEditState();  // flush current plugin state before revert
    
    auto changedFiles = projectState.revertLastLLM();
    if (!changedFiles.isEmpty())
    {
        bool needsHeavyReload = false;
        bool needsSoftReload = false;
        bool stateXmlChanged = false;
        bool stateJsonChanged = false;
        
        for (auto& f : changedFiles) {
            if (f.filename.endsWith(".bird")) needsSoftReload = true;
            if (f.filename.endsWith(".edit.json") || f.filename.endsWith(".edit.xml")) stateXmlChanged = true;
            if (f.filename.endsWith(".state.json")) stateJsonChanged = true;
        }

        if (needsHeavyReload) {
            loadBirdFile(currentBirdFile);
        } else {
            if (needsSoftReload) {
                auto result = BirdLoader::parse(currentBirdFile.getFullPathName().toStdString());
                if (result.error.empty())
                    BirdLoader::populateEdit(*edit, result, engine, nullptr);
            }
            if (stateXmlChanged)
                loadEditState();
            if (stateJsonChanged) {
                loadStateCache();
                for (auto& pair : stateCache)
                    handleStateUpdate(pair.first, pair.second);
            }
        }
        
        if (webView) {
            if (needsHeavyReload || needsSoftReload)
                emitTrackState();
            if (stateJsonChanged) {
                for (auto& pair : stateCache) {
                    auto parsed = juce::JSON::parse(pair.second);
                    auto inner = parsed.getProperty("state", {});
                    if (inner.isObject())
                        webView->emitEventIfBrowserIsVisible(pair.first, juce::var(juce::JSON::toString(inner)));
                }
            }
        }
        DBG("=== REVERT COMPLETE in " + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms ===");
    }
}

void SongbirdEditor::loadBirdFile(const juce::File& birdFile)
{
    if (!birdFile.existsAsFile()) {
        DBG("BirdLoader: File not found: " + birdFile.getFullPathName());
        if (!edit) {
            edit = std::make_unique<te::Edit>(engine, te::Edit::EditRole::forEditing);
            edit->tempoSequence.getTempos()[0]->setBpm(120.0);
            playbackInfo.setEdit(edit.get());
            dropoutDetector.setEdit(edit.get());
        }
        return;
    }

    DBG("BirdLoader: Loading " + birdFile.getFullPathName());
    auto result = BirdLoader::parse(birdFile.getFullPathName().toStdString());

    if (!result.error.empty()) {
        DBG("BirdLoader: Parse error: " + juce::String(result.error));
        if (!edit) {
            edit = std::make_unique<te::Edit>(engine, te::Edit::EditRole::forEditing);
            edit->tempoSequence.getTempos()[0]->setBpm(120.0);
            playbackInfo.setEdit(edit.get());
            dropoutDetector.setEdit(edit.get());
        }
        return;
    }

    if (!edit) {
        edit = std::make_unique<te::Edit>(engine, te::Edit::EditRole::forEditing);
        edit->tempoSequence.getTempos()[0]->setBpm(120.0);
        playbackInfo.setEdit(edit.get());
        dropoutDetector.setEdit(edit.get());
    }

    BirdLoader::populateEdit(*edit, result, engine, [this](const juce::String& msg, float progress) {
        if (webView) {
            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty("message", msg);
            obj->setProperty("progress", progress);
            juce::var jsonVar(obj.get());

            juce::String jsonStr = juce::JSON::toString(jsonVar, true);
            
            // Emit directly since we are already on the MessageManager thread
            webView->emitEventIfBrowserIsVisible("loadingProgress", juce::var(jsonStr));
            
            // CRITICAL: We are performing a heavy synchronous operation that blocks the main thread.
            // Pump the message loop for 20ms to allow the WebView IPC to process the event
            // and the OS window manager to remain responsive (avoid the "beachball").
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
    });

    auto postPopT0 = juce::Time::getMillisecondCounterHiRes();
    DBG("  loadBird post-populate: START");

    // Helper: emit a progress message and pump the message loop so the WebView paints it
    auto emitProgress = [this](const juce::String& msg, float progress) {
        if (!webView) return;
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("message", msg);
        obj->setProperty("progress", progress);
        webView->emitEventIfBrowserIsVisible("loadingProgress", juce::var(obj.get()));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    };

    // (AudioIODeviceCallback handles spectrum + stereo analysis — no plugin needed)
    lastParseResult = result;  // store for JSON serialization

    emitProgress("Registering plugin listeners...", 0.80f);
    registerPluginListeners();  // start tracking per-plugin state changes
    createTrackWatchers();      // start per-track reactive mixer sync
    DBG("  loadBird post-populate: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - postPopT0, 0) + "ms] reattach+listeners+watchers");

    // Load companion state files if any
    emitProgress("Restoring session state...", 0.84f);
    loadStateCache();
    DBG("  loadBird post-populate: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - postPopT0, 0) + "ms] loadStateCache");

    emitProgress("Restoring plugin state...", 0.87f);
    loadEditState();
    DBG("  loadBird post-populate: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - postPopT0, 0) + "ms] loadEditState");

    // After loadEditState restored plugin states, mark all plugins as CLEAN.
    // No need to flush back to disk — the .edit.json is already up to date.
    // Listeners will mark specific plugins dirty when they actually change later.
    dirtyPlugins.clear();
    DBG("  loadBird post-populate: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - postPopT0, 0) + "ms] plugins marked clean");

    // Build mixer state JSON directly from Tracktion edit state.
    // This ensures ALL tracks are captured, merging with any saved volumes
    // from the previous session's stateCache (loaded above by loadStateCache).
    emitProgress("Building mixer state...", 0.93f);
    auto mixerT0 = juce::Time::getMillisecondCounterHiRes();
    {
        // Extract saved track properties by name from the old state cache
        std::map<juce::String, juce::var> savedTracksByName;
        if (stateCache.count("songbird-mixer"))
        {
            auto oldParsed = juce::JSON::parse(stateCache["songbird-mixer"]);
            auto oldState = oldParsed.getProperty("state", {});
            if (oldState.isObject())
            {
                auto oldTracks = oldState.getProperty("tracks", {});
                if (oldTracks.isArray())
                {
                    for (int i = 0; i < oldTracks.getArray()->size(); i++)
                    {
                        auto t = (*oldTracks.getArray())[i];
                        auto name = t.getProperty("name", "").toString();
                        if (name.isNotEmpty())
                            savedTracksByName[name] = t;
                    }
                }
            }
        }
        DBG("EditState: Found " + juce::String((int)savedTracksByName.size()) + " saved tracks to merge");

        // Build complete track array from current Tracktion edit
        juce::Array<juce::var> tracksArray;
        const juce::String TRACK_COLORS[] = {"#FF6B6B", "#4ECDC4", "#45B7D1", "#96CEB4", "#FFEAA7", "#DDA0DD", "#98D8C8", "#F7DC6F"};
        auto audioTracks = te::getAudioTracks(*edit);

        for (int i = 0; i < audioTracks.size(); i++)
        {
            auto* at = audioTracks[i];
            auto name = at->getName();

            // Merge with saved values if they exist
            int volume = 80, pan = 0;
            bool muted = false, solo = false;
            if (savedTracksByName.count(name))
            {
                auto& saved = savedTracksByName[name];
                volume = (int)saved.getProperty("volume", 80);
                pan = (int)saved.getProperty("pan", 0);
                muted = (bool)saved.getProperty("muted", false);
                solo = (bool)saved.getProperty("solo", false);
            }
            else if (auto vp = at->getVolumePlugin())
            {
                // No saved state — read from Tracktion
                volume = juce::roundToInt(juce::Decibels::decibelsToGain(vp->getVolumeDb()) * 127.0f);
                pan = juce::roundToInt(vp->getPan() * 64.0f);
                muted = at->isMuted(false);
                solo = at->isSolo(false);
            }

            auto* obj = new juce::DynamicObject();
            obj->setProperty("id", i);
            obj->setProperty("name", name);
            obj->setProperty("type", "midi");
            obj->setProperty("color", TRACK_COLORS[i % 8]);
            obj->setProperty("volume", volume);
            obj->setProperty("pan", pan);
            obj->setProperty("muted", muted);
            obj->setProperty("solo", solo);
            obj->setProperty("isMaster", false);
            obj->setProperty("isReturn", false);
            tracksArray.add(juce::var(obj));
        }

        // Add master track
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty("id", (int)audioTracks.size());
            obj->setProperty("name", "Master");
            obj->setProperty("type", "midi");
            obj->setProperty("color", TRACK_COLORS[audioTracks.size() % 8]);
            int masterVol = 80;
            if (savedTracksByName.count("Master"))
                masterVol = (int)savedTracksByName["Master"].getProperty("volume", 80);
            obj->setProperty("volume", masterVol);
            obj->setProperty("pan", 0);
            obj->setProperty("muted", false);
            obj->setProperty("solo", false);
            obj->setProperty("isMaster", true);
            obj->setProperty("isReturn", false);
            tracksArray.add(juce::var(obj));
        }

        // Build and inject the complete mixer state into stateCache
        juce::DynamicObject::Ptr mixerObj = new juce::DynamicObject();
        mixerObj->setProperty("tracks", tracksArray);
        mixerObj->setProperty("initialized", false);
        mixerObj->setProperty("sections", juce::Array<juce::var>());
        mixerObj->setProperty("totalBars", 1);
        mixerObj->setProperty("mixerOpen", true);
        mixerObj->setProperty("returnsOpen", false);

        juce::DynamicObject::Ptr wrapper = new juce::DynamicObject();
        wrapper->setProperty("state", juce::var(mixerObj.get()));
        wrapper->setProperty("version", 1);

        stateCache["songbird-mixer"] = juce::JSON::toString(juce::var(wrapper.get()));
        DBG("    mixer: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - mixerT0, 0) + "ms] JSON::toString done");
        DBG("EditState: Built mixer state from C++ with " + juce::String(tracksArray.size()) + " tracks");
        
        // Apply the merged mixer state to Tracktion's audio engine
        applyMixerState(juce::var(mixerObj.get()));
        DBG("    mixer: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - mixerT0, 0) + "ms] applyMixerState done");
    }

    DBG("  loadBird post-populate: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - postPopT0, 0) + "ms] mixer state built+applied (mixer block took " + juce::String(juce::Time::getMillisecondCounterHiRes() - mixerT0, 0) + "ms)");

    // Push track state to UI — pass as juce::var (DynamicObject), NOT string.
    // JUCE's emitEvent calls JSON::toString on the var. If we pass a string,
    // it double-encodes (escapes every quote), causing 68s freeze on 303KB.
    // Passing a DynamicObject means single clean serialization.
    if (webView) {
        juce::MessageManager::getInstance()->callAsync([this, postPopT0]() {
            if (!webView || !edit) return;
            // Show progress
            {
                juce::DynamicObject::Ptr obj = new juce::DynamicObject();
                obj->setProperty("message", "Loading project state...");
                obj->setProperty("progress", 0.96);
                juce::var jsonVar(obj.get());
                webView->emitEventIfBrowserIsVisible("loadingProgress", jsonVar);
            }

            // Step 1: Build complete trackState as a var, then strip notes for metadata-only emit
            auto fullJsonStr = getTrackStateJSON();
            DBG("  loadBird deferred: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - postPopT0, 0) + "ms] getTrackStateJSON (" + juce::String(fullJsonStr.length()) + " bytes)");

            auto parsed = juce::JSON::parse(fullJsonStr);
            // Strip notes from each track (we'll send them per-track via notesChanged)
            if (auto* tracks = parsed.getProperty("tracks", {}).getArray()) {
                for (auto& t : *tracks) {
                    if (auto* obj = t.getDynamicObject())
                        obj->setProperty("notes", juce::Array<juce::var>());
                }
            }

            webView->emitEventIfBrowserIsVisible("trackState", parsed);
            DBG("  loadBird deferred: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - postPopT0, 0) + "ms] trackState (metadata) emitted");

            // Step 2: Emit per-track notes via notesChanged after a delay.
            // The delay ensures JS setTimeout(0) in the trackState listener fires first,
            // populating the tracks store before notes arrive.
            juce::Timer::callAfterDelay(100, [this, postPopT0]() {
                if (!webView || !edit) return;

                auto audioTracks = te::getAudioTracks(*edit);
                for (int t = 0; t < audioTracks.size(); t++) {
                    auto* audioTrack = audioTracks[t];
                    if (!audioTrack) continue;

                    // Build notesChanged as a DynamicObject (not a string)
                    juce::DynamicObject::Ptr notesObj = new juce::DynamicObject();
                    notesObj->setProperty("trackId", t);

                    double loopLen = 0;
                    te::MidiClip* mc = nullptr;
                    for (auto* clip : audioTrack->getClips()) {
                        if (auto* m = dynamic_cast<te::MidiClip*>(clip)) {
                            mc = m;
                            if (m->isLooping()) loopLen = m->getLoopLengthBeats().inBeats();
                            break;
                        }
                    }
                    notesObj->setProperty("loopLengthBeats", loopLen);

                    juce::Array<juce::var> notesArray;
                    if (mc) {
                        auto& seq = mc->getSequence();
                        for (int n = 0; n < seq.getNumNotes(); n++) {
                            auto* note = seq.getNote(n);
                            juce::DynamicObject::Ptr noteObj = new juce::DynamicObject();
                            noteObj->setProperty("pitch", note->getNoteNumber());
                            noteObj->setProperty("beat", note->getStartBeat().inBeats());
                            noteObj->setProperty("duration", note->getLengthBeats().inBeats());
                            noteObj->setProperty("velocity", note->getVelocity());
                            notesArray.add(juce::var(noteObj.get()));
                        }
                    }
                    notesObj->setProperty("notes", notesArray);

                    webView->emitEventIfBrowserIsVisible("notesChanged", juce::var(notesObj.get()));
                }
                DBG("  loadBird deferred: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - postPopT0, 0) + "ms] all notesChanged emitted");

                // Signal loading complete
                {
                    juce::DynamicObject::Ptr doneObj = new juce::DynamicObject();
                    doneObj->setProperty("message", "done");
                    doneObj->setProperty("progress", 1.0);
                    webView->emitEventIfBrowserIsVisible("loadingProgress", juce::var(doneObj.get()));
                }
            });
        });
    }
    DBG("  loadBird post-populate: DONE [" + juce::String(juce::Time::getMillisecondCounterHiRes() - postPopT0, 0) + "ms total]");
}



//==============================================================================
// Plugin scanning — targeted scan of only the plugins we need
//==============================================================================

void SongbirdEditor::scanForPlugins()
{
    auto& pm = engine.getPluginManager();
    auto& list = pm.knownPluginList;

    // Cache file location: ~/Library/Application Support/Songbird/Songbird Player/plugin-cache.xml
    juce::File cacheDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("Songbird")
                              .getChildFile("Songbird Player");
    cacheDir.createDirectory();
    juce::File cacheFile = cacheDir.getChildFile("plugin-cache.xml");

    // Try to load from cache
    if (cacheFile.existsAsFile())
    {
        if (auto xml = juce::parseXML(cacheFile))
        {
            list.recreateFromXml(*xml);
            DBG("PluginScan: Loaded " + juce::String(list.getNumTypes()) + " plugins from cache.");

            // Verify all curated plugins are present
            bool allFound = true;
            juce::StringArray required = {
                "Augmented Strings", "Buchla Easel V", "CS-80 V4", "DX7 V",
                "Jun-6 V", "Jup-8 V4", "Mini V3", "OB-Xa V", "Prophet-5 V",
                "Heartbeat", "Kick 3",
                "Surge XT", "Monoment Bass", "SubLabXL",
                "Console 1", "American Class A", "British Class A",
                "Weiss DS1-MK3", "Summit Audio Grand Channel",
                "Dist TUBE-CULTURE", "Widener", "Tube Delay", "ValhallaRoom"
            };
            for (auto& name : required)
            {
                bool found = false;
                for (auto& type : list.getTypes())
                {
                    if (type.name.equalsIgnoreCase(name)) { found = true; break; }
                }
                if (!found)
                {
                    // Only invalidate if the plugin file actually exists on disk
                    juce::File vst3("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3");
                    if (vst3.exists())
                    {
                        DBG("PluginScan: Cache missing installed plugin: " + name + ", re-scanning...");
                        allFound = false;
                        break;
                    }
                }
            }

            if (allFound)
            {
                DBG("PluginScan: Cache valid, skipping scan.");
                return;
            }
        }
    }

    // Full scan
    list.clear();
    
    auto scanFile = [&](const juce::String& path, const juce::String& formatName) {
        juce::File file(path);
        if (!file.exists()) return;
        
        for (int f = 0; f < pm.pluginFormatManager.getNumFormats(); f++) {
            auto* format = pm.pluginFormatManager.getFormat(f);
            if (!format || format->getName() != formatName) continue;
            
            juce::OwnedArray<juce::PluginDescription> results;
            format->findAllTypesForFile(results, path);
            for (auto* desc : results) {
                list.addType(*desc);
                DBG("PluginScan: Loaded " + desc->name + " (" + desc->pluginFormatName + ")");
            }
        }
    };

    DBG("PluginScan: Scanning curated shortlist...");

    // 1. Synths (VST3)
    juce::StringArray synths = {
        "Augmented Strings", "Buchla Easel V", "CS-80 V4", "DX7 V",
        "Jun-6 V", "Jup-8 V4", "Mini V3", "OB-Xa V", "Prophet-5 V",
        "Heartbeat", "Kick 3",
        "Surge XT", "Monoment Bass", "SubLabXL"
    };
    for (auto& name : synths)
        scanFile("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3", "VST3");

    // 2. Effects (VST3)
    juce::StringArray effects = {
        "Console 1", "American Class A", "British Class A",
        "Weiss DS1-MK3", "Summit Audio Grand Channel",
        "Dist TUBE-CULTURE", "Widener", "Tube Delay", "ValhallaRoom"
    };
    for (auto& name : effects) {
        juce::File vst3("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3");
        if (vst3.exists())
            scanFile(vst3.getFullPathName(), "VST3");
    }

    DBG("PluginScan: Complete. " + juce::String(list.getNumTypes()) + " plugins ready.");

    // Save to cache
    if (auto xml = list.createXml())
    {
        xml->writeTo(cacheFile);
        DBG("PluginScan: Cache saved to " + cacheFile.getFullPathName());
    }
}

void SongbirdEditor::scheduleReload(const juce::String& content)
{
    // Ensure we parse on the message thread
    juce::MessageManager::getInstance()->callAsync([this, content]() {
        loadBirdFile(content);
    });
}
