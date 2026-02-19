#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_MAC

// Enable the Safari Web Inspector for a WebView component (debug only)
void enableWebViewInspector(juce::Component* webViewComponent);

// Set the page zoom level on the underlying WKWebView
void setWebViewPageZoom(juce::Component* webViewComponent, double zoom);

#endif
