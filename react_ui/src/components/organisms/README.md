# Organisms (`components/organisms/`)

Self-contained UI blocks that compose atoms and molecules into functional sections of the DAW interface.

## Components

| Component | Description |
|-----------|-------------|
| `MixerChannel.tsx` | Full channel strip — volume fader, pan, mute/solo, send knobs, plugin slots, level meter, record strip. One instance per audio track. |
| `MasterChannel.tsx` | Master bus channel strip — master fader, stereo meters, spectrum analyzer, stereo width/phase/balance displays. |
| `TrackHeader.tsx` | Track lane header (left side of arrangement view) — track name, color dot, mute/solo. |
| `TrackLane.tsx` | Track lane content area (right side of arrangement view) — renders MIDI clips, sections, and playhead. |
| `TimelineRuler.tsx` | Bar/beat ruler at the top of the arrangement view. Shows bar numbers and beat grid. |
| `ChatInputBar.tsx` | Chat input textarea with send button and model selector. |
| `ChatMessageList.tsx` | Scrollable list of chat messages with auto-scroll-to-bottom. |
| `LoadingScreen.tsx` | Full-screen loading overlay shown during initial plugin scanning and project load. |
| `ExportProgressModal.tsx` | Modal dialog showing stem/master export progress with per-track status updates. |

## Design Conventions

- **Composition** — Organisms compose molecules/atoms, they don't reimplement their logic. `MixerChannel` renders `VolumeFader` + `PanControl` + `MuteSoloButtons` etc.
- **Self-contained state** — Each organism reads directly from Zustand stores via selectors. No prop drilling from parent panels.
- **Track-scoped** — `MixerChannel` and `TrackLane` receive a `trackIndex` and select all their data from `useMixerStore`.
