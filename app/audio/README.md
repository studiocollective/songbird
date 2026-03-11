# Audio Processing (`app/audio/`)

Real-time audio analysis, recording, metering, and plugin state tracking.

## Files

| File | Purpose |
|------|---------|
| `PlaybackInfo.cpp/.h` | 30Hz timer + background analysis thread for transport, FFT spectrum, stereo analysis, and metering. Emits batched `rtFrame` JSON to the WebView. |
| `AudioRecorder.cpp/.h` | Real-time audio recording to Tracktion clips. |
| `AudioQuantizer.cpp/.h` | Audio clip quantization to beat grid. |
| `MasterAnalyzerPlugin.cpp/.h` | Tracktion plugin inserted on master track for raw audio tap (feeds FFT/stereo analysis). |
| `LevelMeterBridge.cpp/.h` | Legacy per-track level metering (disabled — superseded by `PlaybackInfo` `rtFrame` batching). |
| `PluginStateTracker.cpp` | Reactive plugin parameter change tracking. Listens to `audioProcessorParameterChanged`, marks dirty plugins, debounces via timer, then flushes changes to `daw.edit.json` and commits to git. |
| `DropoutDetector.h` | Monitors audio callback timing. Detects buffer underruns (xruns) and timing gaps, emits `dropoutDetected` events to the WebView for display in the debug panel. |

## Real-Time Metering Architecture

```
Audio Thread          Background Thread        Message Thread (30Hz)       JS (display rate)
─────────────        ──────────────────       ──────────────────────      ──────────────────
                                               timerCallback():
                                                 read track levels         
                                                 read transport position   
applyToBuffer() ──►  AnalysisThread:            snapshot → LevelSnapshot  
  ring buffer write    pull from ring buffer     ▲                        
  RMS accumulators     compute FFT+stereo       atomic swap               
                       read LevelSnapshot  ──►  read pre-built JSON  ──►  rtFrame event
                       read CPU atomics          callAsync(emit)            ▼
                       snprintf → char[8192]                              rtBuffer write
                       atomic pointer swap                                rAF ballistic
                                                                           smoothing
                                                                            ▼
                                                                          display @ 60Hz
```

### Data Flow

1. **Audio thread** (`MasterAnalyzerPlugin::applyToBuffer`): Writes raw samples into a lock-free ring buffer and accumulates RMS values. Zero allocations, no locks.

2. **Background thread** (`AnalysisThread`, ~30Hz): Pulls from ring buffer, runs FFT + windowing → 16-band spectrum. Computes stereo width, phase correlation, and balance from RMS accumulators. Reads the `LevelSnapshot` (populated by the timer) and builds the complete JSON payload using `snprintf` into a double-buffered `char[8192]`. Zero heap allocations.

3. **Timer callback** (message thread, 30Hz): Snapshots per-track levels (`getAndClearAudioLevel`) and transport position into a `LevelSnapshot` struct. Reads the pre-built JSON pointer from the background thread. Posts the WebView emit via `callAsync` (non-blocking — the timer returns immediately).

4. **JS side**: Writes to a mutable `rtBuffer`. A `requestAnimationFrame` loop applies ballistic smoothing (instant attack, ~300ms exponential decay) and notifies DOM subscribers at display refresh rate (~60Hz).

### Key Design Decisions

- **30Hz C++ → 60Hz visual**: C++ only sends data 30×/sec to avoid competing with the audio thread. JS-side ballistic smoothing at display refresh rate makes meters appear 60Hz+.
- **Zero allocations in steady state**: Timer uses pre-allocated `juce::String` (`preallocateBytes`). Background thread uses `snprintf` into stack buffers. No `juce::String` concatenation.
- **`callAsync` for emit**: The WebView IPC (`emitEventIfBrowserIsVisible`) is posted to the next message loop iteration, so the timer callback returns immediately and the audio thread can be scheduled sooner.
- **Double-buffered handoffs**: `LevelSnapshot` (timer→background) and JSON `char[]` (background→timer) use atomic pointer swaps — no locks, no contention.

## Design Principles

- **Audio thread safety** — Never allocate memory, lock mutexes, or call non-real-time-safe functions on the audio thread. `applyToBuffer()` uses only atomic operations and ring buffer writes.
- **Three-thread pipeline** — Audio → Background → Message thread separation keeps each thread's work minimal and predictable. Don't collapse threads.
- **Gain ramps** — Audio start/stop uses 10ms gain ramps to prevent pops. See `SongbirdEditor` transport handlers for ramp orchestration.
- **Plugin state tracking** — `PluginStateTracker` uses `audioProcessorParameterChanged` callbacks (audio thread) → `callAsync` → dirty set accumulation → timer-based debounced flush. This avoids committing on every tiny parameter tweak.
