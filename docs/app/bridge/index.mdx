# WebView Bridge (`app/bridge/`)

Communication layer between the React UI and C++ backend. Every JS↔C++ interaction flows through native functions registered in this module.

## Architecture

```
React (WebView)                        C++ (JUCE)
──────────────                         ──────────
nativeFunction('transportPlay')   →    TransportBridge.cpp → te::TransportControl
addStateListener('rtFrame', cb)   ←    PlaybackInfo.cpp → emitEventIfBrowserIsVisible()
juceBridge.setItem(store, json)   →    StateBridge.cpp → handleStateUpdate()
```

The bridge is split into **domain-specific files**, each registering native functions for one area of functionality. `WebViewBridge.cpp` is the entrypoint that calls all domain registrations, and `SongbirdEditor.h` declares the `register*Bridge()` methods.

## Files

| File | Purpose |
|------|---------|
| `WebViewBridge.cpp` | Main `createWebViewOptions()` — calls all domain bridge registrations, sets up WebView content source. |
| `StateBridge.cpp` | State persistence: `loadState`, `updateState`, `resetState`, `reactReady`, `getHistory`. |
| `PluginMixerBridge.cpp` | Plugin + mixer: `openPlugin`, `changePlugin`, `getPluginParams`, `setPluginParam`, `setTrackMixer`, `setMixerParamRT`, `getAvailablePlugins`, `setSidechainSource`. |
| `BirdFileBridge.cpp` | Bird files: `readBird`, `loadBird`, `updateBird`, `writeBirdUser`, `saveBird`. |
| `TransportBridge.cpp` | Transport: `transportPlay`, `transportPause`, `transportStop`, `transportSeek`, `setLoopRange`, `setBpm`, `setProjectScale`. |
| `SettingsBridge.cpp` | Settings: `getApiKey`, `setApiKey`, `getSystemTheme`, `setZoom`, `uiReady`, audio device config. |
| `RecordingBridge.cpp` | Recording: `listMidiInputs`, `setMidiInput`, `sendKeyboardMidi`, MIDI/audio recording start/stop. |
| `TrackBridge.cpp` | Track management: add/remove tracks, rename, reorder. |
| `LyriaBridge.cpp` | AI generation: `addGeneratedTrack`, `removeGeneratedTrack`, Lyria config/prompts/quantize. |
| `MidiEditBridge.cpp` | Piano roll: MIDI editing operations — note add/delete/move, section editing. |
| `UndoRedoBridge.cpp` | Undo/redo: `undo`, `redo`, `revertLLM`. |
| `WebViewHelpers.cpp/.h` | Utility functions for WebView content loading, JS evaluation, `emitEventIfBrowserIsVisible()`. |

## Design Principles

- **One domain per file** — Each bridge file should handle one logical domain. Don't add mixer functions to `TransportBridge.cpp`.
- **`registerXxxBridge(options)`** — Each file implements one registration method called from `createWebViewOptions()`. The method receives `juce::WebBrowserComponent::Options&` and chains `.withNativeFunction()` calls.
- **Message thread** — All native function handlers execute on the JUCE message thread. For heavy work, dispatch to a background thread and use `callAsync` to return results.
- **Real-time path** — `setMixerParamRT` writes directly to the engine without persisting state (used during slider drags). State persists on slider release via the normal Zustand → `updateState` path.
- **Event emission** — Use `emitEventIfBrowserIsVisible()` to push data from C++ to JS. For high-frequency data (~30Hz), use `callAsync` to avoid blocking the timer thread.

## Adding a New Native Function

1. Choose the appropriate domain bridge file (or create a new one)
2. In the `register*Bridge()` method, add `.withNativeFunction("functionName", ...)` 
3. Parse arguments from `juce::var` parameters
4. Implement logic and return result via the completion callback
5. If creating a new domain bridge: add `registerNewDomainBridge()` to `SongbirdEditor.h` and call it from `WebViewBridge.cpp`
6. On the JS side, use `nativeFunction('functionName')` from `@/data/bridge` to call it
