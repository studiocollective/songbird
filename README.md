# Songbird

Songbird is a DAW (Digital Audio Workstation) built as a native macOS desktop app. It combines a **C++ audio engine** (JUCE + Tracktion Engine) with a **React-based UI** rendered in a WebView, and an **AI copilot** powered by Gemini for music composition.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  React UI (WebView)                                         │
│  Vite + React 19 + Tailwind v4 + Zustand                    │
│  ├─ ArrangementView  — timeline, track lanes, playhead      │
│  ├─ MidiEditor       — piano roll, velocity editing         │
│  ├─ MixerPanel       — faders, meters, plugin slots         │
│  ├─ ChatPanel        — AI copilot (Gemini function calling) │
│  └─ SettingsPanel    — audio device, MIDI, theme config     │
└─────────────────┬───────────────────────────────────────────┘
                  │ JS ↔ C++ via JUCE WebBrowserComponent
┌─────────────────▼───────────────────────────────────────────┐
│  C++ Backend (SongbirdEditor)                               │
│  ├─ bridge/    — JS↔C++ native function dispatch            │
│  ├─ state/     — Zustand ↔ engine sync + git undo/redo      │
│  ├─ audio/     — real-time meters, recording, FFT analysis  │
│  ├─ loader/    — .bird file parser → Tracktion Edit         │
│  ├─ plugins/   — plugin windows, macro parameter mapping    │
│  ├─ midi/      — MIDI recording + .bird serialization       │
│  ├─ ai/        — Lyria AI music generation                  │
│  └─ export/    — stems, master, sheet music export          │
│                                                             │
│  Tracktion Engine                                           │
│  └─ te::Edit → Tracks → Plugins → Audio/MIDI Clips         │
└─────────────────────────────────────────────────────────────┘
```

## Key Concepts

- **`.bird` files** — A text-based music notation format ("Markdown for music"). Defines tracks, instruments, patterns, notes, and arrangement. See [`documentation/bird.md`](documentation/bird.md).
- **State management** — Three-layer system: React (Zustand) ↔ C++ (StateSync) ↔ Git (libgit2). Every meaningful change is committed to an in-process git repo. See [`documentation/state-management.md`](documentation/state-management.md).
- **WebView bridge** — All React↔C++ communication flows through JUCE native functions registered in [`app/bridge/`](app/bridge/).
- **Real-time metering** — 30Hz C++ data → ballistic smoothing → 60Hz GPU-composited DOM updates. Zero React re-renders for meters.

## Project Structure

```
songbird-chirp/
├── app/              — C++ backend (JUCE + Tracktion Engine)
├── react_ui/         — React frontend (Vite + TypeScript)
├── libraries/        — Git submodules (JUCE, Tracktion, magenta, etc.)
├── documentation/    — Architecture docs (bird notation, state management)
├── tools/            — Developer utilities (plugin param scanning)
├── utils/            — Build scripts and shell utilities
├── eval/             — LLM evaluation framework for AI copilot
├── files/            — Sample .bird projects, MIDI files, plugin configs
└── CMakeLists.txt    — CMake build configuration
```

## Build

```bash
# Build the C++ desktop app
cmake -B build -G Ninja
cmake --build build

# Build the React UI (served by JUCE WebView in production)
cd react_ui
npm install
npm run build
```

The build produces `SongbirdPlayer.app` in `build/SongbirdPlayer_artefacts/`.

See [`documentation/setup.md`](documentation/setup.md) for full setup instructions.
