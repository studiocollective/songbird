#include "SongbirdEditor.h"
#include "WebViewHelpers.h"

//==============================================================================
// WebView native function bridge — dispatcher
//
// Each domain registers its native functions via a dedicated method.
// See the individual Bridge files in this directory for implementation.
//==============================================================================

juce::WebBrowserComponent::Options SongbirdEditor::createWebViewOptions()
{
    auto options = juce::WebBrowserComponent::Options{};
    registerStateBridge(options);
    registerPluginMixerBridge(options);
    registerBirdFileBridge(options);
    registerTransportBridge(options);
    registerSettingsBridge(options);
    registerRecordingBridge(options);
    registerTrackBridge(options);
    registerLyriaBridge(options);
    registerMidiEditBridge(options);
    registerUndoRedoBridge(options);
    return options;
}
