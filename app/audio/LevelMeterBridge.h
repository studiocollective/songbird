#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

/**
 * Polls audio levels from all tracks and master, pushes them to the WebView.
 * Runs on a timer (~30Hz) to give smooth meter animation.
 */
class LevelMeterBridge : private juce::Timer
{
public:
    LevelMeterBridge();
    ~LevelMeterBridge() override;

    void setEdit(te::Edit* edit);
    void setWebView(juce::WebBrowserComponent* wv);

private:
    void timerCallback() override;

    te::Edit* currentEdit = nullptr;
    juce::WebBrowserComponent* webView = nullptr;

    // One client per track + one for master
    std::vector<std::unique_ptr<te::LevelMeasurer::Client>> trackClients;
    te::LevelMeasurer::Client masterClient;

    void attachClients();
    void detachClients();
};
