#include "SongbirdEditor.h"
#include "WebViewHelpers.h"

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
                oldStateFile.moveFileTo(projectDir.getChildFile("daw.bird.state.json"));
            }
        }

        if (!projectDir.exists()) projectDir.createDirectory();

        if (!birdFile.existsAsFile()) {
            birdFile = juce::File::getCurrentWorkingDirectory().getChildFile("files/sketches/daw/daw.bird");
        }
    }

    DBG("BirdLoader: App location: " + appFile.getFullPathName());
    DBG("BirdLoader: Bird file path: " + birdFile.getFullPathName());

    scanForPlugins();
    currentBirdFile = birdFile;
    loadBirdFile(birdFile);

    // Create WebView with native function bridge
    auto options = createWebViewOptions();
    webView = std::make_unique<juce::WebBrowserComponent>(options);
    addAndMakeVisible(*webView);

    // Start audio level metering
    playbackInfo.setEdit(edit.get());
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
    DBG("SongbirdEditor initialized - engine and edit ready");
}

SongbirdEditor::~SongbirdEditor()
{
    if (edit)
        edit->getTransport().stop(false, false);
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

void SongbirdEditor::timerCallback()
{
    stopTimer();
    saveStateCache();
}

void SongbirdEditor::saveStateCache()
{
    if (!currentBirdFile.existsAsFile()) return;
    
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    for (auto& pair : stateCache)
    {
        // Add parsed JSON directly
        obj->setProperty(pair.first, juce::JSON::parse(pair.second));
    }
        
    auto stateFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileName() + ".state.json");
    if (obj->getProperties().size() > 0)
    {
        stateFile.replaceWithText(juce::JSON::toString(obj.get()));
        DBG("StateSync: Saved state to " + stateFile.getFullPathName());
    }
}

void SongbirdEditor::loadStateCache()
{
    if (!currentBirdFile.existsAsFile()) return;
    
    auto stateFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileName() + ".state.json");
    if (!stateFile.existsAsFile()) return;
    
    auto parsed = juce::JSON::parse(stateFile);
    if (auto* obj = parsed.getDynamicObject())
    {
        stateCache.clear();
        for (auto& prop : obj->getProperties())
        {
            stateCache[prop.name.toString()] = juce::JSON::toString(prop.value);
        }
        DBG("StateSync: Loaded state from " + stateFile.getFullPathName());
    }
}

//==============================================================================
// Bird file loading
//==============================================================================

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

    BirdLoader::populateEdit(*edit, result, engine);
    playbackInfo.reattachAnalyzer(); // Re-insert analyzer AFTER populateEdit adds plugins
    lastParseResult = result;  // store for JSON serialization

    // Load companion state file if any
    loadStateCache();

    // Push track notes to UI if webview is up
    if (webView) {
        auto json = getTrackNotesJSON();
        webView->emitEventIfBrowserIsVisible("trackNotes", juce::var(json));
    }
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

juce::String SongbirdEditor::getTrackNotesJSON()
{
    if (!edit) return "[]";
    return BirdLoader::getTrackNotesJSON(*edit, &lastParseResult);
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
