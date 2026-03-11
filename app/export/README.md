# Export (`app/export/`)

Audio export functionality — stems, master bounce, and sheet music.

## Files

| File | Purpose |
|------|---------|
| `ExportManager.cpp` | All export methods: `exportStems()`, `exportMaster()`, `exportSheetMusic()`. These are `SongbirdEditor` member functions defined in a separate file for organization. |

## Export Operations

### Stems (`exportStems`)
Renders each track individually as a WAV file. Uses Tracktion's offline render with track solo/mute automation:
1. Creates a background `RenderJob` thread
2. Iterates tracks, solos one at a time with return FX optionally included
3. Renders to `exports/<projectName>_stems/` with progress updates to the WebView

### Master (`exportMaster`)
Bounces the full mix to a single stereo WAV:
1. Creates a `MasterRenderJob` thread
2. Renders all tracks through the master bus
3. Outputs to `exports/<projectName>_master.wav`

### Sheet Music (`exportSheetMusic`)
Converts the current `.bird` file parse result to sheet music JSON and sends it to the React UI for rendering via the `SheetMusicView` panel.

## Design Principles

- **Background threads** — All audio renders run on dedicated `juce::Thread` instances to avoid blocking the message thread. Progress is pushed to React via `callAsync` + `emitEventIfBrowserIsVisible`.
- **Tracktion render API** — Uses `tracktion::Renderer` for bit-perfect offline bouncing at the project's sample rate.
- **Track state preservation** — Solo/mute state is saved and restored after stem export to avoid side effects.
