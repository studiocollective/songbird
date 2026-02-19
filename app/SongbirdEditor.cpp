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

    // Start audio level metering
    levelMeterBridge.setEdit(edit.get());
    levelMeterBridge.setWebView(webView.get());

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

    BirdLoader::populateEdit(*edit, result);

    // Re-attach level meters to the new edit
    levelMeterBridge.setEdit(edit.get());

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
