# State Management (`app/state/`)

Manages application state persistence, synchronization between UI and engine, and git-based undo/redo.

> For the comprehensive deep-dive into state management, see [`documentation/state-management.md`](../../documentation/state-management.md).

## Architecture

```
React Zustand stores          StateSync.cpp              ProjectState.cpp          Disk
─────────────────────         ─────────────              ────────────────          ────
useMixerStore persist    →    handleStateUpdate()   →    commitAndNotify()    →    daw.state.json
  songbird-mixer               applyMixerState()          stage + commit           daw.edit.json
  songbird-transport            applyTransportState()      move refs/heads/main     daw.bird
  songbird-chat                 saveStateCache()
  songbird-lyria                saveSessionState()
                                saveEditState()

                              describeMixerChange()       undo() / redo()     →    restore workdir
                                "drums vol 80→65"          move refs/heads/main     from commit tree
```

## Files

| File | Purpose |
|------|---------|
| `StateSync.cpp` | Bidirectional Zustand ↔ Tracktion Engine state sync. `handleStateUpdate()` dispatches incoming state, `applyMixerState()` writes to engine, `describeMixerChange()` generates human-readable commit messages. |
| `SessionState.cpp` | Session-specific state handling: transport, chat, and Lyria state (gitignored — not part of undo/redo). |
| `ProjectState.cpp/.h` | Git-based undo/redo using libgit2. `commit()`, `undo()`, `redo()`, `revertLastLLM()`, `getHistory()`. Operates on an in-process git repository — no fork, no exec. |
| `ValueTreeJSON.h` | Helper utilities for converting JUCE ValueTrees to/from JSON. |

## Git Undo/Redo

Uses **libgit2** for zero-overhead in-process git operations:

- **`refs/heads/main`** — current position (HEAD), moves forward on commit, backward on undo
- **`refs/redo-tip`** — created on first undo, points to the newest undone commit (deleted when a new commit is made, invalidating the redo chain)

Each commit is tagged with a source: `[auto]` (system), `[mixer]` (fader/knob), `[LLM]` (AI copilot), `[user]` (manual save).

## Echo Prevention

Seven mechanisms prevent feedback loops when state crosses the React ↔ C++ boundary:

1. **String comparison** — `handleStateUpdate()` skips if incoming JSON == cached JSON
2. **JSON normalization** — round-trip through JUCE parser after commit so echoes match
3. **`undoRedoInProgress` flag** — blocks mixer commits during undo/redo (cleared after 200ms)
4. **`isLoadFinished` gate** — blocks ALL mixer commits until initial project load completes
5. **`hasUncommittedChanges()` guard** — skip commit if git working tree is clean
6. **`suppressMixerEcho` flag** — blocks `TrackStateWatcher` during `applyMixerState()`
7. **Integer rounding** — volume/pan rounded to integers to avoid float precision diffs

## Design Principles

- **Git-tracked vs session** — Only `daw.bird`, `daw.state.json`, and `daw.edit.json` participate in undo/redo. Session state (`daw.session.json`) is ephemeral and gitignored.
- **Commit gating** — Never commit before `isLoadFinished` is true. The loading sequence must complete (plugins settle, React hydrates) before commits are enabled.
- **Debounced session saves** — Non-mixer stores trigger a 500ms debounce timer before writing to disk (transport/chat changes don't need instant persistence).
- **Descriptive commits** — `describeMixerChange()` generates readable messages like `'drums' vol 80→65` by diffing old vs new JSON. This powers the HistoryPanel UI.
