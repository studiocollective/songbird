#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>

class SongbirdEditor : public juce::Component {
public:
    SongbirdEditor();
    ~SongbirdEditor() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // WebView for React UI
    std::unique_ptr<juce::WebBrowserComponent> webView;

    // Audio device manager for standalone playback
    juce::AudioDeviceManager deviceManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongbirdEditor)
};
