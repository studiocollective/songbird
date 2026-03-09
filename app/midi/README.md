# MIDI (`app/midi/`)

MIDI recording, editing, and serialization.

## Files

| File | Purpose |
|------|---------|
| `MidiRecorder.cpp/.h` | Real-time MIDI input recording to Tracktion clips. |
| `MidiToBird.cpp/.h` | MIDI clip → `.bird` file notation serialization. |
| `TrackStateWatcher.h` | Reactive ValueTree listeners for per-track state changes (C++ → React push). |
