# WebView Bridge (`app/bridge/`)

Communication layer between the React UI and C++ backend via JUCE WebBrowserComponent native functions.

## Files

| File | Purpose |
|------|---------|
| `WebViewBridge.cpp` | Registers all JS↔C++ native function handlers in `createWebViewOptions()`. |
| `WebViewHelpers.cpp/.h` | Utility functions for WebView content loading and JS evaluation. |
