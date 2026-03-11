# Panels (`components/panels/`)

Full panel/page-level components — each represents a major section of the DAW UI.

## Components

| Panel | Size | Description |
|-------|------|-------------|
| `ArrangementView.tsx` | ~37KB | Main timeline view — track lanes, playhead, section markers, scroll/zoom, section focus, click-to-seek. Handles horizontal scroll sync between ruler and lanes. |
| `MidiEditor.tsx` | ~45KB | Piano roll editor — note editing (draw, move, resize, delete), velocity lane, section-scoped display, playhead, grid snapping. |
| `SampleEditor.tsx` | ~8KB | Audio waveform display — shows waveform for audio/Lyria tracks, time-aligned with the arrangement. |
| `MixerPanel.tsx` | ~8KB | Mixer view — renders all `MixerChannel` organisms + `MasterChannel`. Toggleable visibility. |
| `ChatPanel.tsx` | ~27KB | AI assistant sidebar — Gemini-powered copilot with streaming responses, tool call execution, and `.bird` file editing. Right panel layout. |
| `Transport.tsx` | ~12KB | Top transport bar — play/pause/stop, BPM input, key/scale selectors, position display, CPU meter. |
| `SettingsPanel.tsx` | ~15KB | Settings modal — audio device config (input/output/buffer/sample rate), MIDI device list, UI theme toggle, latency display. |
| `HistoryPanel.tsx` | ~5KB | Git commit history panel — displays undo/redo history from `ProjectState`. Expandable toggle at bottom. |
| `BirdFilePanel.tsx` | ~6KB | `.bird` file text editor — live editing of the bird notation source. Changes are debounced and sent to C++ for re-parsing. |
| `SheetMusicView.tsx` | ~14KB | Sheet music rendering — displays exported sheet music from the bird parser. |
| `AutomationOverlay.tsx` | ~3KB | Automation curve overlay for the arrangement view. |
| `DebugPanel.tsx` | ~11KB | Developer tools overlay — bridge call log, real-time data inspector, state dump. |

## Design Conventions

- **Panels own layout** — Each panel manages its own scrolling, sizing, and responsiveness. `App.tsx` only controls panel visibility and positioning.
- **Canvas rendering** — `ArrangementView` and `MidiEditor` use `<canvas>` for performance-critical rendering (playhead, note grid). The canvas uses `requestAnimationFrame` with data-driven updates from `subscribeRtBuffer()`.
- **Keyboard shortcuts** — Panels register their own keyboard event handlers. Transport handles space (play/pause), MidiEditor handles note editing shortcuts.
- **Panel state** — Panel visibility (mixer open, chat open, etc.) is stored in `useMixerStore` and persisted via Zustand → C++ → git.
