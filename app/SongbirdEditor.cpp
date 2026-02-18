#include "SongbirdEditor.h"

SongbirdEditor::SongbirdEditor()
{
    // Create WebView with options
    juce::WebBrowserComponent::Options options;

    webView = std::make_unique<juce::WebBrowserComponent>(options);
    addAndMakeVisible(*webView);

    // Load the React UI
    // In dev mode: load from Vite dev server
    // In production: load from bundled resources
    #if JUCE_DEBUG
        webView->goToURL("http://localhost:5173");
        DBG("Loading React UI from dev server (localhost:5173)");
    #else
        // Production: load from bundled HTML
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
}

SongbirdEditor::~SongbirdEditor()
{
}

void SongbirdEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void SongbirdEditor::resized()
{
    if (webView)
        webView->setBounds(getLocalBounds());
}
