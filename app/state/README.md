# State Management (`app/state/`)

Manages application state persistence and synchronization between UI and engine.

## Files

| File | Purpose |
|------|---------|
| `ProjectState.cpp/.h` | Git-based undo/redo using libgit2. Commit/restore working directory snapshots. |
| `StateSync.cpp` | Bidirectional Zustand ↔ Tracktion Engine state sync (transport, mixer). |
| `ValueTreeJSON.h` | Helper utilities for converting JUCE ValueTrees to/from JSON. |
