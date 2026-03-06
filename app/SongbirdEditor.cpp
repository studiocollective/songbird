#include "SongbirdEditor.h"
#include "WebViewHelpers.h"
#include "ValueTreeJSON.h"

//==============================================================================
// Constructor / Destructor
//==============================================================================

SongbirdEditor::SongbirdEditor()
{
    // Check command line arguments for a sketch name
    auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
    juce::String sketchName;
    if (args.size() > 0) {
        sketchName = args[0];
        if (!sketchName.endsWith(".bird")) {
            sketchName += ".bird";
        }
        DBG("BirdLoader: Requested sketch '" + sketchName + "' via CLI");
    }

    // Search upward from the app bundle for the project root directory
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    juce::File projectRoot = appFile.getParentDirectory();
    for (int i = 0; i < 8; i++) {
        if (projectRoot.getChildFile("files").isDirectory()) {
            break; // Found root
        }
        projectRoot = projectRoot.getParentDirectory();
    }

    juce::File birdFile;

    if (sketchName.isNotEmpty()) {
        auto sketchesDir = projectRoot.getChildFile("files").getChildFile("sketches");
        if (!sketchesDir.exists()) sketchesDir.createDirectory();
        
        juce::String baseName = sketchName.dropLastCharacters(5); // removes ".bird"
        auto projectDir = sketchesDir.getChildFile(baseName);
        birdFile = projectDir.getChildFile(sketchName);

        // Migrate existing flat file if it exists and the folder doesn't
        auto oldFlatFile = sketchesDir.getChildFile(sketchName);
        if (oldFlatFile.existsAsFile() && !projectDir.exists()) {
            projectDir.createDirectory();
            oldFlatFile.moveFileTo(birdFile);
            
            // Also move state file if it exists
            auto oldStateFile = sketchesDir.getChildFile(sketchName + ".state.json");
            if (oldStateFile.existsAsFile()) {
                oldStateFile.moveFileTo(projectDir.getChildFile(sketchName + ".state.json"));
            }
            // Also move edit state file if it exists (migrate to JSON)
            auto oldEditFile = sketchesDir.getChildFile(sketchName + ".edit.xml");
            if (oldEditFile.existsAsFile()) {
                oldEditFile.moveFileTo(projectDir.getChildFile(sketchName + ".edit.json"));
            }
            DBG("BirdLoader: Migrated " + sketchName + " to project folder");
        }

        if (!projectDir.exists()) projectDir.createDirectory();

        if (!birdFile.existsAsFile()) {
            DBG("BirdLoader: Sketch does not exist, creating template");
            birdFile.create();
            birdFile.replaceWithText(
                "ch 1 kick\n"
                "  plugin kick\n"
                "\n"
                "arr intro 4\n"
                "\n"
                "sec intro\n"
                "  ch 1 kick\n"
                "    p x x x _\n"
                "      v 80\n"
                "        n 36\n"
            );
        }
    } else {
        auto projectDir = projectRoot.getChildFile("files/sketches/daw");
        birdFile = projectDir.getChildFile("daw.bird");
        
        // Migration logic for default daw.bird just in case
        auto oldFlatFile = projectRoot.getChildFile("files/sketches/daw.bird");
        if (oldFlatFile.existsAsFile() && !projectDir.exists()) {
            projectDir.createDirectory();
            oldFlatFile.moveFileTo(birdFile);
            
            auto oldStateFile = projectRoot.getChildFile("files/sketches/daw.bird.state.json");
            if (oldStateFile.existsAsFile()) {
                oldStateFile.moveFileTo(projectDir.getChildFile("daw.state.json"));
            }
            auto oldEditFile = projectRoot.getChildFile("files/sketches/daw.bird.edit.xml");
            if (oldEditFile.existsAsFile()) {
                oldEditFile.moveFileTo(projectDir.getChildFile("daw.edit.json"));
            }
        }

        if (!projectDir.exists()) projectDir.createDirectory();

        if (!birdFile.existsAsFile()) {
            birdFile = juce::File::getCurrentWorkingDirectory().getChildFile("files/sketches/daw/daw.bird");
        }
    }

    DBG("BirdLoader: App location: " + appFile.getFullPathName());
    DBG("BirdLoader: Bird file path: " + birdFile.getFullPathName());

    currentBirdFile = birdFile;

    // Create WebView with native function bridge
    auto options = createWebViewOptions();
    webView = std::make_unique<juce::WebBrowserComponent>(options);
    addAndMakeVisible(*webView);

    // Start audio level metering (Edit is set later)
    playbackInfo.setWebView(webView.get());

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

    // We don't load plugins here yet.
    // We wait for the 'uiReady' native function call from React UI.
}

void SongbirdEditor::startBackgroundLoading()
{
    if (isLoadingStarted) {
        // If React reloads (HMR / Strict Mode), it might mount the LoadingScreen and call uiReady again.
        // We only tell it we're done immediately if we actually FINISHED. 
        // If we are still in progress, we simply return and let the existing loop keep emitting events.
        if (isLoadFinished && webView) {
            juce::MessageManager::getInstance()->callAsync([this]() {
                if (webView) {
                    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
                    obj->setProperty("message", "done");
                    obj->setProperty("progress", 1.0);
                    juce::var jsonVar(obj.get());
                    juce::String jsonStr = juce::JSON::toString(jsonVar, true);
                    webView->emitEventIfBrowserIsVisible("loadingProgress", juce::var(jsonStr));
                }
            });
        }
        return;
    }
    isLoadingStarted = true;

    juce::MessageManager::getInstance()->callAsync([this]() {
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        DBG("=== LOAD START ===");
        scanForPlugins();
        DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] scanForPlugins");
        
        // currentBirdFile was already set in constructor
        projectState.setProjectDir(currentBirdFile);
        loadBirdFile(currentBirdFile);
        DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] loadBirdFile complete");
        
        if (edit)
            playbackInfo.setEdit(edit.get());
        DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] playbackInfo.setEdit");
        // NOTE: trackState is already emitted at the end of loadBirdFile() — 
        // do NOT emit it again here (212KB JSON blocks the message thread for ~40s).

        // Defer the "Project loaded" commit — plugins are still settling.
        // The timer fires 500ms after the last audioProcessorParameterChanged,
        // at which point we create the commit with all plugins stable.
        saveStateCache();
        DBG("  [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] saveStateCache");
        pendingProjectLoadCommit = true;
        startTimer(1000);  // wait for plugins to settle
        DBG("=== LOAD END [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms total] ===");
    });
}

SongbirdEditor::~SongbirdEditor()
{
    if (edit) {
        edit->getTransport().stop(false, false);
        unregisterPluginListeners();
        // Mark all plugins dirty for final save
        for (auto* track : te::getAllTracks(*edit))
            for (auto* plugin : track->pluginList.getPlugins())
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
                    dirtyPlugins.insert(ext);
        saveEditState();
        commitAndNotify("Session end", ProjectState::Autosave);
    }
    edit = nullptr;
}

//==============================================================================
// Component overrides
//==============================================================================

void SongbirdEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void SongbirdEditor::resized()
{
    if (webView)
        webView->setBounds(getLocalBounds());

    #if JUCE_MAC && JUCE_DEBUG
    if (!inspectorEnabled && webView)
    {
        enableWebViewInspector(webView.get());
        inspectorEnabled = true;
    }
    #endif
}

void SongbirdEditor::checkLoadFinished()
{
    if (projectLoadComplete && reactHydrated && !isLoadFinished)
    {
        isLoadFinished = true;
        DBG("StateSync: Both project and React ready — commits enabled");
    }
}

void SongbirdEditor::commitAndNotify(const juce::String& message, ProjectState::Source source, bool includeEditXml)
{
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    projectState.commit(message, source, includeEditXml);
    DBG("  commitAndNotify: git commit took " + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms (" + message + ")");
    if (webView)
        webView->emitEventIfBrowserIsVisible("historyChanged", juce::var("ok"));
}

void SongbirdEditor::timerCallback()
{
    stopTimer();
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    DBG("=== TIMER CALLBACK (isLoadFinished=" + juce::String(isLoadFinished ? "Y" : "N") + " pendingProjectLoad=" + juce::String(pendingProjectLoadCommit ? "Y" : "N") + ") ===");
    if (!isLoadFinished && !pendingProjectLoadCommit) return;

    // --- MIDI edit flush (most work goes to background thread) ---
    if (pendingMidiEdit.trackId >= 0) {
        auto midiEdit = pendingMidiEdit;
        pendingMidiEdit.trackId = -1;

        // Read notes from Tracktion clip ON message thread (fast)
        auto clipNotes = collectNotesFromClip(midiEdit.trackId, midiEdit.secOffset, midiEdit.secBars);

        dirtyPluginParams.clear();
        pluginParamsDirty = false;

        // Build lightweight notes JSON and emit immediately (message thread, fast)
        // This replaces the expensive getTrackStateJSON — only one track's notes
        if (webView) {
            juce::DynamicObject::Ptr notesObj = new juce::DynamicObject();
            notesObj->setProperty("trackId", midiEdit.trackId);
            juce::Array<juce::var> notesArray;
            
            auto audioTracks = te::getAudioTracks(*edit);
            if (midiEdit.trackId >= 0 && midiEdit.trackId < (int)audioTracks.size()) {
                te::MidiClip* mc = nullptr;
                for (auto* clip : audioTracks[midiEdit.trackId]->getClips())
                    if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
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
            }
            notesObj->setProperty("notes", notesArray);
            webView->emitEventIfBrowserIsVisible("notesChanged", juce::var(notesObj.get()));
        }

        // Launch background thread for heavy I/O (bird file write + git commit)
        juce::String commitMsg = "MIDI Edit: edit notes";
        juce::Thread::launch([this, midiEdit, clipNotes, commitMsg]() {
            writeBirdFromClip(midiEdit.trackId, midiEdit.sectionName,
                              midiEdit.secOffset, midiEdit.secBars, clipNotes);

            saveSessionState();
            saveEditState();

            projectState.commit(commitMsg, ProjectState::Plugin, true);
            midiEditPending = false;

            juce::MessageManager::callAsync([this]() {
                if (webView)
                    webView->emitEventIfBrowserIsVisible("historyChanged", juce::var("ok"));
            });
        });
        return;
    }

    // --- Non-MIDI timer work (plugin params, project load) stays on message thread ---
    DBG("  timer: saveSessionState...");
    saveSessionState();
    DBG("  timer: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] saveSessionState done");
    DBG("  timer: saveEditState...");
    saveEditState();
    DBG("  timer: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] saveEditState done");

    if (pendingProjectLoadCommit)
    {
        if (!reactHydrated)
        {
            DBG("  timer: React not hydrated yet, retrying in 200ms");
            startTimer(200);  // React not ready yet, check again soon
            return;
        }
        pendingProjectLoadCommit = false;
        DBG("  timer: saveStateCache...");
        saveStateCache();
        DBG("  timer: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] saveStateCache done");
        DBG("  timer: commitAndNotify...");
        commitAndNotify("Project loaded", ProjectState::Autosave);
        DBG("  timer: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] commitAndNotify done");
        isLoadFinished = true;
        DBG("  timer: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] === PROJECT LOAD COMMIT COMPLETE ===");
    }
    else if (pluginParamsDirty && !undoRedoInProgress.load())
    {
        juce::String commitMsg;
        if (dirtyPluginParams.empty())
        {
            commitMsg = "Plugin parameter change";
        }
        else
        {
            juce::StringArray parts;
            for (auto& [pluginName, params] : dirtyPluginParams)
            {
                juce::StringArray paramNames;
                for (auto& p : params)
                    paramNames.add(p);
                if (paramNames.size() > 3)
                {
                    auto count = paramNames.size();
                    paramNames.removeRange(3, paramNames.size() - 3);
                    paramNames.add("+" + juce::String(count - 3) + " more");
                }
                parts.add(pluginName + ": " + paramNames.joinIntoString(", "));
            }
            commitMsg = parts.joinIntoString("; ");
        }
        dirtyPluginParams.clear();
        pluginParamsDirty = false;
        commitAndNotify(commitMsg, ProjectState::Plugin);
    }
}

void SongbirdEditor::saveStateCache()
{
    if (!currentBirdFile.existsAsFile()) return;
    
    // Only save mixer state to daw.state.json (git-tracked for undo/redo)
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    auto it = stateCache.find("songbird-mixer");
    if (it != stateCache.end())
        obj->setProperty(it->first, juce::JSON::parse(it->second));
        
    auto stateFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".state.json");
    if (obj->getProperties().size() > 0)
    {
        stateFile.replaceWithText(juce::JSON::toString(obj.get()));
    }
}

void SongbirdEditor::saveSessionState()
{
    if (!currentBirdFile.existsAsFile()) return;
    
    // Save transport/chat/lyria to daw.session.json (gitignored, not part of undo)
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    for (auto& pair : stateCache)
    {
        if (pair.first == "songbird-mixer") continue;  // mixer is in state.json
        obj->setProperty(pair.first, juce::JSON::parse(pair.second));
    }
        
    auto sessionFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".session.json");
    if (obj->getProperties().size() > 0)
    {
        sessionFile.replaceWithText(juce::JSON::toString(obj.get()));
    }
}

void SongbirdEditor::loadStateCache()
{
    if (!currentBirdFile.existsAsFile()) return;
    stateCache.clear();
    
    // Load git-tracked mixer state from daw.state.json
    auto stateFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".state.json");
    
    // Backward compatibility: migrate old daw.bird.state.json → daw.state.json
    if (!stateFile.existsAsFile()) {
        auto oldFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileName() + ".state.json");
        if (oldFile.existsAsFile())
            oldFile.moveFileTo(stateFile);
    }
    if (stateFile.existsAsFile())
    {
        auto parsed = juce::JSON::parse(stateFile);
        if (auto* obj = parsed.getDynamicObject())
        {
            for (auto& prop : obj->getProperties())
                stateCache[prop.name.toString()] = juce::JSON::toString(prop.value);
        }
    }
    
    // Load session state (transport/chat/lyria) from daw.session.json
    auto sessionFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".session.json");
    if (sessionFile.existsAsFile())
    {
        auto parsed = juce::JSON::parse(sessionFile);
        if (auto* obj = parsed.getDynamicObject())
        {
            for (auto& prop : obj->getProperties())
                stateCache[prop.name.toString()] = juce::JSON::toString(prop.value);
        }
    }
    
    DBG("StateSync: Loaded state cache (" + juce::String((int)stateCache.size()) + " stores)");
}

void SongbirdEditor::saveEditState()
{
    if (!edit || !currentBirdFile.existsAsFile()) return;
    if (dirtyPlugins.empty())
    {
        DBG("EditState: All plugins clean, skipping save");
        return;
    }

    // Only flush plugins that actually changed (not all 50+)
    int flushed = 0;
    for (auto* plugin : dirtyPlugins)
    {
        if (plugin != nullptr)
        {
            edit->flushPluginStateIfNeeded(*plugin);
            flushed++;
        }
    }
    dirtyPlugins.clear();

    auto editFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".edit.json");
    editFile.replaceWithText(ValueTreeJSON::toJsonString(edit->state));
    DBG("EditState: Flushed " + juce::String(flushed) + " dirty plugins, saved to " + editFile.getFullPathName());
}

//==============================================================================
// Reactive plugin state tracking
//==============================================================================

void SongbirdEditor::registerPluginListeners()
{
    if (!edit) return;
    unregisterPluginListeners();  // clear any stale listeners
    
    auto registerForTrack = [this](te::Track* track)
    {
        if (!track) return;
        for (auto* plugin : track->pluginList.getPlugins())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
            {
                if (auto* proc = ext->getAudioPluginInstance())
                    proc->addListener(this);
            }
        }
    };
    
    for (auto* track : te::getAllTracks(*edit))
        registerForTrack(track);
    registerForTrack(edit->getMasterTrack());
    
    // Count registered plugins for logging
    int count = 0;
    for (auto* track : te::getAllTracks(*edit))
        for (auto* plugin : track->pluginList.getPlugins())
            if (dynamic_cast<te::ExternalPlugin*>(plugin))
                count++;
    
    // Plugins start clean — disk matches memory after load.
    // Listeners will mark specific plugins dirty when they actually change.
    dirtyPlugins.clear();
    dirtyPluginParams.clear();
    pluginParamsDirty = false;
    
    DBG("EditState: Registered listeners on " + juce::String(count) + " plugins (all clean)");
}

void SongbirdEditor::unregisterPluginListeners()
{
    if (!edit) return;
    
    auto unregisterForTrack = [this](te::Track* track)
    {
        if (!track) return;
        for (auto* plugin : track->pluginList.getPlugins())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
            {
                if (auto* proc = ext->getAudioPluginInstance())
                    proc->removeListener(this);
            }
        }
    };
    
    for (auto* track : te::getAllTracks(*edit))
        unregisterForTrack(track);
    unregisterForTrack(edit->getMasterTrack());
}

te::ExternalPlugin* SongbirdEditor::findExternalPlugin(juce::AudioProcessor* processor)
{
    if (!edit || !processor) return nullptr;
    
    auto findInTrack = [processor](te::Track* track) -> te::ExternalPlugin*
    {
        if (!track) return nullptr;
        for (auto* plugin : track->pluginList.getPlugins())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
            {
                if (ext->getAudioPluginInstance() == processor)
                    return ext;
            }
        }
        return nullptr;
    };
    
    for (auto* track : te::getAllTracks(*edit))
        if (auto* found = findInTrack(track))
            return found;
    return findInTrack(edit->getMasterTrack());
}

// Called from audio thread — must be lock-free
void SongbirdEditor::audioProcessorParameterChanged(juce::AudioProcessor* processor, int paramIndex, float)
{
    // Capture param name on audio thread before posting (lightweight string copy)
    juce::String paramName;
    if (auto* param = processor->getParameters()[paramIndex])
        paramName = param->getName(64);

    // Post to message thread for safe dirty marking
    juce::MessageManager::callAsync([this, processor, paramName]()
    {
        if (undoRedoInProgress.load()) return;
        if (auto* ext = findExternalPlugin(processor))
        {
            dirtyPlugins.insert(ext);
            if (paramName.isNotEmpty())
                dirtyPluginParams[ext->getName()].insert(paramName);
            pluginParamsDirty = true;
            startTimer(500);
        }
    });
}

// Called when plugin signals a major internal state change (UI open, preset load, etc.)
// Only commit when a meaningful flag is set — UI open/close fires with all flags false
void SongbirdEditor::audioProcessorChanged(juce::AudioProcessor* processor, const ChangeDetails& details)
{
    bool meaningful = details.programChanged || details.nonParameterStateChanged
                   || details.parameterInfoChanged || details.latencyChanged;
    juce::MessageManager::callAsync([this, processor, meaningful, details]()
    {
        if (undoRedoInProgress.load()) return;
        if (auto* ext = findExternalPlugin(processor))
        {
            dirtyPlugins.insert(ext);
            if (meaningful)
            {
                auto label = details.programChanged ? "preset change"
                           : details.nonParameterStateChanged ? "state change"
                           : "config change";
                dirtyPluginParams[ext->getName()].insert(label);
                pluginParamsDirty = true;
            }
            startTimer(500);
        }
    });
}

void SongbirdEditor::loadEditState()
{
    if (!edit || !currentBirdFile.existsAsFile()) return;
    auto t0 = juce::Time::getMillisecondCounterHiRes();

    auto editFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".edit.json");
    
    // Backward compatibility: migrate old .edit.xml if .edit.json doesn't exist
    if (!editFile.existsAsFile()) {
        auto oldXml = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".edit.xml");
        if (oldXml.existsAsFile()) {
            auto xml = juce::parseXML(oldXml);
            if (xml) {
                auto tree = juce::ValueTree::fromXml(*xml);
                editFile.replaceWithText(ValueTreeJSON::toJsonString(tree));
                oldXml.deleteFile();
                DBG("EditState: Migrated .edit.xml -> .edit.json");
            }
        }
    }
    if (!editFile.existsAsFile()) return;

    auto jsonText = editFile.loadFileAsString();
    DBG("    loadEdit: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] file read (" + juce::String(jsonText.length()) + " bytes)");
    
    auto savedState = ValueTreeJSON::fromJsonString(jsonText);
    DBG("    loadEdit: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] JSON parse + ValueTree");
    
    if (!savedState.isValid()) {
        DBG("    loadEdit: INVALID saved state!");
        return;
    }

    // Build a map of saved tracks: trackName -> ValueTree
    std::map<juce::String, juce::ValueTree> savedTracks;
    for (int i = 0; i < savedState.getNumChildren(); i++)
    {
        auto child = savedState.getChild(i);
        if (child.hasType("TRACK") || child.hasType("MASTERTRACK"))
        {
            juce::String name = child.getProperty("name", "").toString();
            if (name.isNotEmpty())
                savedTracks[name] = child;
        }
    }
    DBG("    loadEdit: " + juce::String((int)savedTracks.size()) + " saved tracks, " 
        + juce::String(te::getAllTracks(*edit).size()) + " live tracks");

    // Restore plugin states — only for plugins whose state actually changed
    int restored = 0, skipped = 0;
    
    auto restorePluginsForTrack = [&](te::Track* track)
    {
        if (!track) return;
        auto it = savedTracks.find(track->getName());
        if (it == savedTracks.end()) return;

        auto& savedTrackVT = it->second;

        for (auto* livePlugin : track->pluginList.getPlugins())
        {
            auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(livePlugin);
            if (!extPlugin) continue;

            juce::String liveName = extPlugin->getName();

            // Find matching plugin in saved state (plugins are direct TRACK children)
            for (int p = 0; p < savedTrackVT.getNumChildren(); p++)
            {
                auto savedPluginVT = savedTrackVT.getChild(p);
                if (!savedPluginVT.hasType("PLUGIN")) continue;

                // Match by name — Tracktion uses "vst" not "external" for type,
                // so just match on plugin name (live-side dynamic_cast already filters)
                juce::String savedName = savedPluginVT.getProperty("name", "").toString();
                if (savedName != liveName) continue;

                // Compare state blobs — skip restore if unchanged
                auto savedBlob = savedPluginVT.getProperty("state");
                auto& liveVT = extPlugin->state;
                auto liveBlob = liveVT.getProperty("state");
                
                if (savedBlob == liveBlob) {
                    skipped++;
                } else {
                    extPlugin->restorePluginStateFromValueTree(savedPluginVT);
                    restored++;
                }
                break;
            }
        }
    };

    // Restore for all audio tracks
    for (auto* track : te::getAllTracks(*edit))
        restorePluginsForTrack(track);

    // Restore for master track
    restorePluginsForTrack(edit->getMasterTrack());

    DBG("    loadEdit: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] " 
        + juce::String(restored) + " restored, " + juce::String(skipped) + " unchanged");
}

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
        }
        return;
    }

    if (!edit) {
        edit = std::make_unique<te::Edit>(engine, te::Edit::EditRole::forEditing);
        edit->tempoSequence.getTempos()[0]->setBpm(120.0);
        playbackInfo.setEdit(edit.get());
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

void SongbirdEditor::exportSheetMusic()
{
    if (currentBirdFile == juce::File() || lastParseResult.channels.empty()) {
        logToJS("Export error: No valid bird file loaded.");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Sheet Music (MIDI)",
        currentBirdFile.getParentDirectory().getChildFile(currentBirdFile.getFileNameWithoutExtension() + ".mid"),
        "*.mid"
    );

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File()) {
                juce::MidiFile midiFile;
                midiFile.setTicksPerQuarterNote(960);
                
                for (const auto& ch : lastParseResult.channels) {
                    juce::MidiMessageSequence seq;
                    
                    // Key signature meta event
                    if (lastParseResult.hasKeySignature) {
                        seq.addEvent(juce::MidiMessage::keySignatureMetaEvent(lastParseResult.keySharpsFlats, lastParseResult.keyIsMinor), 0.0);
                    }

                    // Track name meta event (type 3)
                    seq.addEvent(juce::MidiMessage::textMetaEvent(3, juce::String(ch.name)), 0.0);
                    
                    // Channel number (1-16)
                    int midiChannel = juce::jlimit(1, 16, ch.channel + 1);

                    for (const auto& note : ch.notes) {
                        double startTick = note.beatPos * 960.0;
                        double endTick = (note.beatPos + note.duration) * 960.0;
                        
                        auto noteOn = juce::MidiMessage::noteOn(midiChannel, note.pitch, (juce::uint8)note.velocity);
                        auto noteOff = juce::MidiMessage::noteOff(midiChannel, note.pitch, (juce::uint8)0);
                        
                        seq.addEvent(noteOn, startTick);
                        seq.addEvent(noteOff, endTick);
                    }
                    
                    seq.updateMatchedPairs();
                    midiFile.addTrack(seq);
                }
                
                juce::FileOutputStream stream(file);
                if (stream.openedOk()) {
                    stream.setPosition(0);
                    stream.truncate();
                    midiFile.writeTo(stream);
                    logToJS("Successfully exported MIDI to: " + file.getFullPathName());
                } else {
                    logToJS("Failed to open file for writing: " + file.getFullPathName());
                }
            }
        });
}

juce::String SongbirdEditor::getTrackStateJSON()
{
    if (!edit) return "{}";
    return BirdLoader::getTrackStateJSON(*edit, &lastParseResult);
}

void SongbirdEditor::exportStems(bool includeReturnFx)
{
    if (!edit || currentBirdFile == juce::File()) {
        logToJS("Export error: No valid edit loaded.");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Stems",
        currentBirdFile.getParentDirectory(),
        ""
    );

    chooser->launchAsync(juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::openMode,
        [this, chooser, includeReturnFx](const juce::FileChooser& fc) {
            auto destDir = fc.getResult();
            if (destDir == juce::File() || !destDir.isDirectory()) return;

            auto& transport = edit->getTransport();
            te::TimeRange range = transport.looping
                ? transport.getLoopRange()
                : te::TimeRange(te::TimePosition::fromSeconds(0.0), edit->getLength());

            auto allTracks = te::getAllTracks(*edit);

            juce::Array<te::Track*> tracksToProcess;
            for (auto* t : allTracks)
                if (t->isAudioTrack() && !t->isMasterTrack() && !t->isFolderTrack())
                    tracksToProcess.add(t);

            if (tracksToProcess.isEmpty()) {
                logToJS("Export error: No audio tracks to export.");
                return;
            }

            // Build full bitset (all tracks) for wet mode
            juce::BigInteger fullBitset;
            for (int i = 0; i < allTracks.size(); ++i)
                fullBitset.setBit(i);

            int total = tracksToProcess.size();
            if (webView) {
                juce::String startJson = "{\"current\":0,\"total\":" + juce::String(total) + ",\"name\":\"\"}";
                webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(startJson));
            }

            struct RenderJob : public juce::Thread
            {
                RenderJob(SongbirdEditor& editor_,
                          te::Edit& edit_,
                          juce::Array<te::Track*> tracksToProcess_,
                          juce::Array<te::Track*> allTracks_,
                          te::TimeRange range_,
                          juce::File destDir_,
                          bool includeReturnFx_,
                          juce::BigInteger fullBitset_)
                    : juce::Thread("StemExport"),
                      editor(editor_), edit(edit_),
                      tracksToProcess(tracksToProcess_),
                      allTracks(allTracks_),
                      range(range_), destDir(destDir_),
                      includeReturnFx(includeReturnFx_),
                      fullBitset(fullBitset_)
                {}

                void run() override
                {
                    int total = tracksToProcess.size();
                    for (int i = 0; i < total; ++i)
                    {
                        if (threadShouldExit()) break;

                        auto* track = tracksToProcess[i];
                        juce::String trackName = track->getName()
                            .replaceCharacter(' ', '_')
                            .replaceCharacter('/', '_')
                            .replaceCharacter('\\', '_');

                        juce::File outputFile = destDir.getChildFile(trackName + ".wav");

                        // Emit "starting" BEFORE the render so bar shows partial progress
                        // while the render is in flight (current = i, not i+1)
                        juce::MessageManager::callAsync([this, i, total, trackName]() {
                            if (editor.webView) {
                                juce::String json = "{\"current\":" + juce::String(i)
                                    + ",\"total\":" + juce::String(total)
                                    + ",\"name\":\"" + trackName + "\"}";
                                editor.webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(json));
                            }
                        });

                        if (includeReturnFx) {
                            // Mute all OTHER tracks so only the target's audio flows through
                            // the master chain (and aux returns).
                            // Use mute — NOT solo — because the offline renderer respects
                            // isMuted() but ignores isSolo().
                            juce::Array<bool> muteWas;
                            for (auto* t : tracksToProcess)
                                if (auto* at = dynamic_cast<te::AudioTrack*>(t))
                                    muteWas.add(at->isMuted(false));

                            for (auto* t : tracksToProcess)
                                if (auto* at = dynamic_cast<te::AudioTrack*>(t))
                                    at->setMute(t != track); // mute everything except target

                            te::Renderer::renderToFile("Exporting " + trackName, outputFile,
                                                       edit, range, fullBitset, true, true, {}, false);

                            int idx = 0;
                            for (auto* t : tracksToProcess)
                                if (auto* at = dynamic_cast<te::AudioTrack*>(t))
                                    at->setMute(muteWas[idx++]);
                        } else {
                            // Dry: render only this track's chain directly, no master
                            juce::BigInteger bitset;
                            int trackIdx = allTracks.indexOf(track);
                            if (trackIdx >= 0)
                                bitset.setBit(trackIdx);

                            te::Renderer::renderToFile("Exporting " + trackName, outputFile,
                                                       edit, range, bitset, true, true, {}, false);
                        }

                        // Emit "done" AFTER the render
                        juce::MessageManager::callAsync([this, i, total, trackName]() {
                            if (editor.webView) {
                                juce::String json = "{\"current\":" + juce::String(i + 1)
                                    + ",\"total\":" + juce::String(total)
                                    + ",\"name\":\"" + trackName + "\"}";
                                editor.webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(json));
                            }
                        });
                    }

                    juce::MessageManager::callAsync([this]() {
                        if (editor.webView)
                            editor.webView->emitEventIfBrowserIsVisible("exportDone", juce::var("null"));
                        editor.logToJS("Successfully exported stems to: " + destDir.getFullPathName());
                        editor.stemExportThread = nullptr;
                    });
                }

                SongbirdEditor& editor;
                te::Edit& edit;
                juce::Array<te::Track*> tracksToProcess;
                juce::Array<te::Track*> allTracks;
                te::TimeRange range;
                juce::File destDir;
                bool includeReturnFx;
                juce::BigInteger fullBitset;
            };

            stemExportThread = std::make_unique<RenderJob>(
                *this, *edit, tracksToProcess, allTracks, range, destDir, includeReturnFx, fullBitset);
            stemExportThread->startThread();
        });
}

void SongbirdEditor::exportMaster()
{
    if (!edit || currentBirdFile == juce::File()) {
        logToJS("Export error: No valid edit loaded.");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Master",
        currentBirdFile.getParentDirectory(),
        "*.wav"
    );

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
            auto outputFile = fc.getResult();
            if (outputFile == juce::File()) return;
            if (!outputFile.hasFileExtension("wav"))
                outputFile = outputFile.withFileExtension("wav");

            auto& transport = edit->getTransport();
            te::TimeRange range = transport.looping
                ? transport.getLoopRange()
                : te::TimeRange(te::TimePosition::fromSeconds(0.0), edit->getLength());

            auto allTracks = te::getAllTracks(*edit);
            juce::BigInteger fullBitset;
            for (int i = 0; i < allTracks.size(); ++i)
                fullBitset.setBit(i);

            if (webView) {
                juce::String startJson = "{\"current\":0,\"total\":1,\"name\":\"Master\"}";
                webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(startJson));
            }

            struct MasterRenderJob : public juce::Thread
            {
                MasterRenderJob(SongbirdEditor& editor_, te::Edit& edit_,
                                te::TimeRange range_, juce::File outputFile_,
                                juce::BigInteger bitset_)
                    : juce::Thread("MasterExport"),
                      editor(editor_), edit(edit_),
                      range(range_), outputFile(outputFile_), bitset(bitset_)
                {}

                void run() override
                {
                    juce::MessageManager::callAsync([this]() {
                        if (editor.webView) {
                            juce::String json = "{\"current\":0,\"total\":1,\"name\":\"Master\"}";
                            editor.webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(json));
                        }
                    });

                    te::Renderer::renderToFile("Exporting Master", outputFile,
                                               edit, range, bitset, true, true, {}, false);

                    juce::MessageManager::callAsync([this]() {
                        if (editor.webView) {
                            juce::String json = "{\"current\":1,\"total\":1,\"name\":\"Master\"}";
                            editor.webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(json));
                        }
                    });

                    juce::MessageManager::callAsync([this]() {
                        if (editor.webView)
                            editor.webView->emitEventIfBrowserIsVisible("exportDone", juce::var("null"));
                        editor.logToJS("Successfully exported master to: " + outputFile.getFullPathName());
                        editor.stemExportThread = nullptr;
                    });
                }

                SongbirdEditor& editor;
                te::Edit& edit;
                te::TimeRange range;
                juce::File outputFile;
                juce::BigInteger bitset;
            };

            stemExportThread = std::make_unique<MasterRenderJob>(
                *this, *edit, range, outputFile, fullBitset);
            stemExportThread->startThread();
        });
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

void SongbirdEditor::setSidechainSource(int destTrackId, int sourceTrackId)
{
    if (!edit) return;

    auto audioTracks = te::getAudioTracks(*edit);
    
    te::Track* destTrack = nullptr;
    if (destTrackId >= 0 && destTrackId < audioTracks.size())
        destTrack = audioTracks[destTrackId];

    if (!destTrack) return;

    // Let's use bus #1 as the dedicated SC bus for now
    const int SC_BUS = 1;

    // Helper to auto-set parameters
    auto autoSetParam = [](te::Plugin* plugin, const juce::String& nameSubstr, float normValue)
    {
        for (auto* param : plugin->getAutomatableParameters())
        {
            if (param->getParameterName().containsIgnoreCase(nameSubstr))
            {
                param->setNormalisedParameter(normValue, juce::sendNotification);
                DBG("SC Auto-set: " + param->getParameterName() + " → " + juce::String(normValue));
                return;
            }
        }
        DBG("SC Auto-set: no param matching '" + nameSubstr + "' found (plugin=" + plugin->getName() + ")");
    };

    bool enabling = (sourceTrackId >= 0);
    for (auto* plugin : destTrack->pluginList)
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) continue;
        if (dynamic_cast<te::LevelMeterPlugin*>(plugin))   continue;
        if (dynamic_cast<te::AuxSendPlugin*>(plugin))      continue;
        if (dynamic_cast<te::AuxReturnPlugin*>(plugin))    continue;

        // Exact parameter names from: ScanPluginParams "/Library/Audio/Plug-Ins/VST3/Console 1.vst3"
        // Index 24:  "Compressor"                  – 2 steps  (0=off, 1=on)
        // Index 31:  "External Sidechain"           – 3 steps  (0=Int, 0.5=Ext 1, 1.0=Ext 2)
        // Index 119: "Ext. Sidechain to Subsystem"  – 2 steps  (0=off, 1=on)
        // Index 29:  "Compression"                  – continuous amount/threshold proxy
        autoSetParam(plugin, "Compressor",                 enabling ? 1.0f : 0.0f);
        autoSetParam(plugin, "External Sidechain",         enabling ? 0.5f : 0.0f); // 0.5 = Ext 1
        autoSetParam(plugin, "Ext. Sidechain to Subsystem", enabling ? 1.0f : 0.0f);

        if (enabling)
        {
            // Default starting compression amount — user can adjust via sensitivity slider
            autoSetParam(plugin, "Compression", 0.4f);
        }
    }

    DBG("Sidechain: Track " + juce::String(sourceTrackId)
        + " -> Track " + juce::String(destTrackId)
        + " via bus " + juce::String(SC_BUS));
}

// ====================================================================
// MIDI editing helpers (piano roll)
// ====================================================================

void SongbirdEditor::emitTrackState(bool emitLoadingDone)
{
    if (!webView || !edit) return;

    juce::MessageManager::getInstance()->callAsync([this, emitLoadingDone]() {
        if (!webView || !edit) return;

        // Step 1: Build complete trackState as a var, then strip notes for metadata-only emit
        auto fullJsonStr = getTrackStateJSON();
        auto parsed = juce::JSON::parse(fullJsonStr);
        
        // Strip notes from each track (we'll send them per-track via notesChanged)
        if (auto* tracks = parsed.getProperty("tracks", {}).getArray()) {
            for (auto& t : *tracks) {
                if (auto* obj = t.getDynamicObject())
                    obj->setProperty("notes", juce::Array<juce::var>());
            }
        }

        webView->emitEventIfBrowserIsVisible("trackState", parsed);

        // Step 2: Emit per-track notes via notesChanged after a delay.
        juce::Timer::callAfterDelay(100, [this, emitLoadingDone]() {
            if (!webView || !edit) return;

            auto audioTracks = te::getAudioTracks(*edit);
            for (int t = 0; t < audioTracks.size(); t++) {
                auto* audioTrack = audioTracks[t];
                if (!audioTrack) continue;

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

            if (emitLoadingDone) {
                juce::DynamicObject::Ptr doneObj = new juce::DynamicObject();
                doneObj->setProperty("message", "done");
                doneObj->setProperty("progress", 1.0);
                webView->emitEventIfBrowserIsVisible("loadingProgress", juce::var(doneObj.get()));
            }
        });
    });
}

void SongbirdEditor::scheduleMidiCommit()
{
    midiEditPending = true;
    dirtyPluginParams.clear();
    dirtyPluginParams["MIDI Edit"].insert("edit notes");
    pluginParamsDirty = true;
    startTimer(800);
}

std::vector<SongbirdEditor::ClipNote> SongbirdEditor::collectNotesFromClip(
    int trackId, double secOffset, int secBars)
{
    // MUST run on message thread — reads from Tracktion MidiSequence
    std::vector<ClipNote> result;
    if (!edit) return result;

    auto audioTracks = te::getAudioTracks(*edit);
    if (trackId < 0 || trackId >= (int)audioTracks.size()) return result;

    te::MidiClip* mc = nullptr;
    for (auto* clip : audioTracks[trackId]->getClips())
        if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
    if (!mc) return result;

    auto& seq = mc->getSequence();
    double secEnd = secOffset + secBars * 4.0;

    for (int i = 0; i < seq.getNumNotes(); ++i) {
        auto* note = seq.getNote(i);
        double b = note->getStartBeat().inBeats();
        if (b >= secOffset - 0.01 && b < secEnd + 0.01) {
            result.push_back({ note->getNoteNumber(), note->getVelocity(), b - secOffset });
        }
    }
    return result;
}

void SongbirdEditor::writeBirdFromClip(int trackId, const juce::String& sectionName,
                                        double /*secOffset*/, int secBars,
                                        const std::vector<ClipNote>& clipNotes)
{
    // Safe to run on background thread — only does string manipulation + file I/O.
    // No Tracktion or JUCE ValueTree access.
    if (!currentBirdFile.existsAsFile()) return;
    if (trackId < 0 || trackId >= (int)lastParseResult.channels.size()) return;

    auto& chInfo = lastParseResult.channels[trackId];
    juce::String channelName = chInfo.name;
    int channelNumInFile = chInfo.channel + 1;

    // Convert notes to step map
    struct StepNote { int pitch; int velocity; };
    int totalSteps = secBars * 16;
    std::map<int, std::vector<StepNote>> stepMap;

    for (auto& cn : clipNotes) {
        int step = static_cast<int>(std::round(cn.relBeat * 4.0));
        if (step >= 0 && step < totalSteps)
            stepMap[step].push_back({ cn.pitch, cn.velocity });
    }

    // Build p/v/n lines
    int maxVoices = 0;
    for (auto& [step, notes] : stepMap)
        maxVoices = std::max(maxVoices, (int)notes.size());
    if (maxVoices == 0) maxVoices = 1;

    for (auto& [step, notes] : stepMap)
        std::sort(notes.begin(), notes.end(),
            [](const StepNote& a, const StepNote& b) { return a.pitch > b.pitch; });

    juce::String pLine = "    p";
    juce::String vLine = "      v";
    std::vector<juce::String> nLines(maxVoices, "        n");

    for (int s = 0; s < totalSteps; s++) {
        auto it = stepMap.find(s);
        if (it != stepMap.end() && !it->second.empty()) {
            pLine += " x";
            vLine += " " + juce::String(it->second[0].velocity);
            for (int v = 0; v < maxVoices; v++) {
                if (v < (int)it->second.size())
                    nLines[v] += " " + juce::String(it->second[v].pitch);
                else
                    nLines[v] += " -";
            }
        } else {
            pLine += " _";
        }
    }

    juce::String newBlock = pLine + "\n" + vLine + "\n";
    for (auto& nl : nLines)
        newBlock += nl + "\n";

    // --- Replace the channel block in the bird file ---
    auto birdText = currentBirdFile.loadFileAsString();
    auto lines = juce::StringArray::fromLines(birdText);

    int secStart = -1, secEndLine = lines.size();
    juce::String secMarker = "sec " + sectionName;
    for (int i = 0; i < lines.size(); i++) {
        auto trimmed = lines[i].trim();
        if (trimmed == secMarker || trimmed.startsWith(secMarker + " ")) {
            secStart = i;
            for (int j = i + 1; j < lines.size(); j++) {
                auto t = lines[j].trim();
                if (t.startsWith("sec ") && !t.startsWith(secMarker)) { secEndLine = j; break; }
            }
            break;
        }
    }
    if (secStart < 0) return;

    juce::String chMarker = "ch " + juce::String(channelNumInFile) + " " + channelName;
    int chStart = -1, chEnd = secEndLine;
    for (int i = secStart + 1; i < secEndLine; i++) {
        auto trimmed = lines[i].trim();
        if (trimmed == chMarker || trimmed.startsWith(chMarker + " ")) {
            chStart = i;
            for (int j = i + 1; j < secEndLine; j++) {
                if (lines[j].trim().startsWith("ch ")) { chEnd = j; break; }
            }
            break;
        }
    }

    if (chStart < 0) {
        lines.insert(secEndLine, "  " + chMarker + "\n" + newBlock);
    } else {
        juce::StringArray preserved;
        for (int i = chStart + 1; i < chEnd; i++) {
            auto t = lines[i].trim();
            if (!t.startsWith("p ") && !t.startsWith("v ") && !t.startsWith("n "))
                preserved.add(lines[i]);
        }
        lines.removeRange(chStart + 1, chEnd - chStart - 1);
        int insertAt = chStart + 1;
        auto newLines = juce::StringArray::fromLines(newBlock);
        if (newLines.size() > 0 && newLines[newLines.size() - 1].isEmpty())
            newLines.remove(newLines.size() - 1);
        for (int i = 0; i < newLines.size(); i++)
            lines.insert(insertAt + i, newLines[i]);
        insertAt += newLines.size();
        for (int i = 0; i < preserved.size(); i++)
            lines.insert(insertAt + i, preserved[i]);
    }

    currentBirdFile.replaceWithText(lines.joinIntoString("\n"));
    
    // Notify frontend that the raw .bird file on disk has changed
    // so the Bird File Viewer can refresh its contents
    if (webView) {
        juce::MessageManager::callAsync([this]() {
            if (webView)
                webView->emitEventIfBrowserIsVisible("birdContentChanged", juce::var());
        });
    }

    DBG("MidiEdit: writeBirdFromClip sec=" + sectionName + " ch=" + channelName);
}
