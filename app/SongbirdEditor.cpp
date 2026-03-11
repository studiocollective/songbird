#include "SongbirdEditor.h"
#include "WebViewHelpers.h"
#include "ValueTreeJSON.h"

//==============================================================================
// Constructor / Destructor
//==============================================================================

SongbirdEditor::SongbirdEditor()
{
    // Create the virtual MIDI input for computer keyboard injection
    if (auto res = engine.getDeviceManager().createVirtualMidiDevice("Computer Keyboard"); !res.wasOk())
        DBG("Failed to create Computer Keyboard virtual MIDI device: " + res.getErrorMessage());
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

    // Start dropout detector (monitors audio callback timing)
    dropoutDetector.setPlaybackInfo(&playbackInfo);
    dropoutDetector.start(engine.getDeviceManager().deviceManager, webView.get(), &engine);

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
            dropoutDetector.setEdit(edit.get());
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

    // --- Non-MIDI timer work: defer heavy I/O to background thread ---
    // Collect state snapshots on message thread (fast), then write to disk in background.
    // This prevents message-thread file I/O from causing priority inversion with the audio thread.

    // Snapshot: session state JSON string (fast — just JSON::toString on in-memory data)
    juce::String sessionJson;
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        for (auto& pair : stateCache)
        {
            if (pair.first == "songbird-mixer") continue;
            obj->setProperty(pair.first, juce::JSON::parse(pair.second));
        }
        if (obj->getProperties().size() > 0)
            sessionJson = juce::JSON::toString(obj.get());
    }
    auto sessionFile = currentBirdFile.existsAsFile()
        ? currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".session.json")
        : juce::File();

    // Snapshot: edit state (flush dirty plugins on message thread — requires edit access)
    juce::String editJson;
    juce::File editFile;
    bool hasEditWork = false;
    if (edit && currentBirdFile.existsAsFile() && !dirtyPlugins.empty())
    {
        for (auto* plugin : dirtyPlugins)
            if (plugin) edit->flushPluginStateIfNeeded(*plugin);
        dirtyPlugins.clear();
        editJson = ValueTreeJSON::toJsonString(edit->state);
        editFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".edit.json");
        hasEditWork = true;
    }
    else
    {
        dirtyPlugins.clear(); // clear even if no work
    }

    DBG("  timer: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms] snapshots taken (message thread done)");

    // Determine commit work (stays on message thread for now — just collecting params)
    bool needsCommit = false;
    juce::String commitMsg;
    ProjectState::Source commitSource = ProjectState::Autosave;

    if (pendingProjectLoadCommit)
    {
        if (!reactHydrated)
        {
            DBG("  timer: React not hydrated yet, retrying in 200ms");
            startTimer(200);
            return;
        }
        pendingProjectLoadCommit = false;
        // Also snapshot stateCache for the project-load commit
        {
            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            auto it = stateCache.find("songbird-mixer");
            if (it != stateCache.end())
                obj->setProperty(it->first, juce::JSON::parse(it->second));
            auto stateFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".state.json");
            if (obj->getProperties().size() > 0)
                stateFile.replaceWithText(juce::JSON::toString(obj.get()));
        }
        needsCommit = true;
        commitMsg = "Project loaded";
        commitSource = ProjectState::Autosave;
        isLoadFinished = true;
    }
    else if (pluginParamsDirty && !undoRedoInProgress.load())
    {
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
        needsCommit = true;
        commitSource = ProjectState::Plugin;
    }

    // Launch background thread for all file I/O + git commit
    juce::Thread::launch([this, sessionJson, sessionFile, editJson, editFile,
                          hasEditWork, needsCommit, commitMsg, commitSource]() {
        // Write session state
        if (sessionJson.isNotEmpty() && sessionFile != juce::File())
            sessionFile.replaceWithText(sessionJson);

        // Write edit state
        if (hasEditWork && editJson.isNotEmpty())
            editFile.replaceWithText(editJson);

        // Git commit
        if (needsCommit)
        {
            projectState.commit(commitMsg, commitSource, true);

            juce::MessageManager::callAsync([this]() {
                if (webView)
                    webView->emitEventIfBrowserIsVisible("historyChanged", juce::var("ok"));
            });
        }
    });
}

