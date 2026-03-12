# MIDI (`app/midi/`)

MIDI recording, editing, and serialization between the piano roll UI and `.bird` file format.

## Architecture

```
Piano Roll (React)              C++ Backend                     Bird File
──────────────────              ──────────────                  ─────────
MidiEditor.tsx                  MidiEditBridge.cpp              .bird text
  note drag/add/delete     →     MidiEditHelpers.cpp      →     MidiToBird.cpp
  → nativeFunction()              update Tracktion clip           → serialize notes
                                   → writeBirdFromClip()           back to .bird format
                                   → commitAndNotify()

MIDI Keyboard/Controller        MidiRecorder.cpp
  hardware input           →     real-time capture   →     Tracktion MIDI clip
  listMidiInputs()                quantize to grid          → writeBirdFromClip()
  setMidiInput()
```

## Files

| File | Purpose |
|------|---------|
| `MidiRecorder.cpp/.h` | Real-time MIDI input recording. Captures incoming MIDI events and writes them to Tracktion clips with optional grid quantization. |
| `MidiToBird.cpp/.h` | MIDI clip → `.bird` file notation serialization. Converts Tracktion MIDI clip data (pitches, velocities, timings) back into `.bird` pattern/note/velocity tokens. |
| `MidiEditHelpers.cpp` | Piano roll editing operations — note manipulation helpers called from `MidiEditBridge.cpp`. |
| `TrackStateWatcher.h` | Reactive ValueTree listeners for per-track state changes. Watches `VolumeAndPanPlugin` properties and pushes updates to React when the engine's mixer state changes. |

## Design Principles

- **Round-trip fidelity** — MIDI from the piano roll must serialize back to `.bird` notation without losing information. The `MidiToBird` serializer must produce valid `.bird` tokens that re-parse to the same MIDI output.
- **Grid quantization** — Recorded MIDI is quantized to the pattern grid (sixteenth note default). The quantization happens at recording time, not playback.
- **TrackStateWatcher debouncing** — Value changes from the engine are debounced (100ms) and filtered by delta thresholds (0.5% volume, 2% pan) to prevent flooding React with noise from float precision jitter.
- **`suppressMixerEcho`** — When `applyMixerState()` writes to the engine, `TrackStateWatcher` must not echo the change back to React. The `suppressMixerEcho` atomic flag in `SongbirdEditor` gates this.
- **Commit after edit** — Piano roll edits trigger `scheduleMidiCommit()` which debounces → writes the clip back to `.bird` → commits to git via `commitAndNotify()`.

## Extending

To support a new MIDI editing operation from the piano roll:
1. Add the native function in `MidiEditBridge.cpp`
2. Implement the logic in `MidiEditHelpers.cpp` (operates on Tracktion's `te::MidiClip`)
3. Call `scheduleMidiCommit()` to persist the change to `.bird` format
