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
        
        birdFile = sketchesDir.getChildFile(sketchName);
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
        birdFile = projectRoot.getChildFile("files/daw.bird");
        if (!birdFile.existsAsFile()) {
            birdFile = juce::File::getCurrentWorkingDirectory().getChildFile("files/daw.bird");
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
    lastParseResult = result;  // store for JSON serialization

    // Push track notes to UI if webview is up
    if (webView) {
        auto json = getTrackNotesJSON();
        webView->emitEventIfBrowserIsVisible("trackNotes", juce::var(json));
    }
}

juce::String SongbirdEditor::getTrackNotesJSON()
{
    if (!edit) return "[]";
    return BirdLoader::getTrackNotesJSON(*edit, &lastParseResult);
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
                "Heartbeat", "Kick 2",
                "Console 1", "American Class A", "British Class A",
                "Weiss DS1-MK3", "Summit Audio Grand Channel"
            };
            for (auto& name : required)
            {
                bool found = false;
                for (auto& type : list.getTypes())
                {
                    if (type.name == name) { found = true; break; }
                }
                if (!found)
                {
                    // Only invalidate if the plugin file actually exists on disk
                    juce::File vst3("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3");
                    juce::File au("/Library/Audio/Plug-Ins/Components/" + name + ".component");
                    if (vst3.exists() || au.exists())
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
        "Heartbeat"
    };
    for (auto& name : synths)
        scanFile("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3", "VST3");

    // 2. Kick 2 (AU)
    scanFile("/Library/Audio/Plug-Ins/Components/Kick 2.component", "AudioUnit");

    // 3. Effects (VST3 preferred, then AU)
    juce::StringArray effects = {
        "Console 1", "American Class A", "British Class A",
        "Weiss DS1-MK3", "Summit Audio Grand Channel"
    };
    for (auto& name : effects) {
        juce::File vst3("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3");
        if (vst3.exists())
            scanFile(vst3.getFullPathName(), "VST3");
        else
            scanFile("/Library/Audio/Plug-Ins/Components/" + name + ".component", "AudioUnit");
    }

    DBG("PluginScan: Complete. " + juce::String(list.getNumTypes()) + " plugins ready.");

    // Save to cache
    if (auto xml = list.createXml())
    {
        xml->writeTo(cacheFile);
        DBG("PluginScan: Cache saved to " + cacheFile.getFullPathName());
    }
}
