#include "SongbirdEditor.h"
#include "WebViewHelpers.h"

//==============================================================================
// Constructor / Destructor
//==============================================================================

SongbirdEditor::SongbirdEditor()
{
    // Load the default .bird file into the Edit
    // Search upward from the app bundle for the files/ directory
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    juce::File birdFile;
    auto searchDir = appFile.getParentDirectory();
    for (int i = 0; i < 8; i++) {
        auto candidate = searchDir.getChildFile("files/daw.bird");
        if (candidate.existsAsFile()) {
            birdFile = candidate;
            break;
        }
        searchDir = searchDir.getParentDirectory();
    }

    // Fallback: try relative to CWD
    if (!birdFile.existsAsFile())
        birdFile = juce::File::getCurrentWorkingDirectory().getChildFile("files/daw.bird");

    DBG("BirdLoader: App location: " + appFile.getFullPathName());
    DBG("BirdLoader: Bird file path: " + birdFile.getFullPathName());

    scanForPlugins();
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

    edit = std::make_unique<te::Edit>(engine, te::Edit::EditRole::forEditing);
    edit->tempoSequence.getTempos()[0]->setBpm(120.0);

    BirdLoader::populateEdit(*edit, result, engine);
    lastParseResult = result;  // store for JSON serialization

    // Re-attach level meters to the new edit
    playbackInfo.setEdit(edit.get());

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

    // If we already have plugins cached from a previous run, skip scanning
    if (list.getNumTypes() > 0) {
        DBG("PluginScan: Found " + juce::String(list.getNumTypes()) + " cached plugins, skipping scan");
        return;
    }

    DBG("PluginScan: Starting targeted plugin scan...");

    // Specific plugin files to scan (macOS AU components + VST3 bundles)
    juce::StringArray targetPlugins = {
        // Arturia classic emulations
        "/Library/Audio/Plug-Ins/Components/Pigments.component",
        "/Library/Audio/Plug-Ins/Components/Mini V.component",
        "/Library/Audio/Plug-Ins/Components/CS-80 V.component",
        "/Library/Audio/Plug-Ins/Components/Prophet V.component",
        "/Library/Audio/Plug-Ins/Components/Jup-8 V.component",
        "/Library/Audio/Plug-Ins/Components/DX7 V.component",
        "/Library/Audio/Plug-Ins/Components/Buchla Easel V.component",
        // Drums, bass, channel strip
        "/Library/Audio/Plug-Ins/Components/Kick 2.component",
        "/Library/Audio/Plug-Ins/Components/Heartbeat.component",
        "/Library/Audio/Plug-Ins/Components/SubLab XL.component",
        "/Library/Audio/Plug-Ins/Components/Console1.component",
        // VST3 variants
        "/Library/Audio/Plug-Ins/VST3/Pigments.vst3",
        "/Library/Audio/Plug-Ins/VST3/Mini V.vst3",
        "/Library/Audio/Plug-Ins/VST3/CS-80 V.vst3",
        "/Library/Audio/Plug-Ins/VST3/Prophet V.vst3",
        "/Library/Audio/Plug-Ins/VST3/Jup-8 V.vst3",
        "/Library/Audio/Plug-Ins/VST3/DX7 V.vst3",
        "/Library/Audio/Plug-Ins/VST3/Buchla Easel V.vst3",
        "/Library/Audio/Plug-Ins/VST3/Kick 2.vst3",
        "/Library/Audio/Plug-Ins/VST3/Heartbeat.vst3",
        "/Library/Audio/Plug-Ins/VST3/SubLab XL.vst3",
        "/Library/Audio/Plug-Ins/VST3/Console1.vst3",
    };

    for (auto& path : targetPlugins) {
        juce::File pluginFile(path);
        if (!pluginFile.exists()) continue;

        // Try each registered format (AU, VST3)
        for (int f = 0; f < pm.pluginFormatManager.getNumFormats(); f++) {
            auto* format = pm.pluginFormatManager.getFormat(f);
            if (!format) continue;

            // Check if this format can handle this file
            if (format->fileMightContainThisPluginType(path)) {
                juce::OwnedArray<juce::PluginDescription> results;
                format->findAllTypesForFile(results, path);

                for (auto* desc : results) {
                    list.addType(*desc);
                    DBG("PluginScan: Found '" + desc->name + "' (" + desc->pluginFormatName + ")");
                }
            }
        }
    }

    DBG("PluginScan: Scan complete — " + juce::String(list.getNumTypes()) + " plugins found");
}
