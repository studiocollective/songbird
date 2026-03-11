# Documentation (`documentation/`)

Architecture and reference documentation for Songbird.

## Files

| Document | Description |
|----------|-------------|
| [`bird.md`](bird.md) | Complete reference for the `.bird` music notation format — tokens, patterns, velocity, notes, chords, swing, automation, plugin keywords, and macro mappings. |
| [`state-management.md`](state-management.md) | Deep dive into the three-layer state system (React ↔ C++ ↔ Git), including Zustand stores, `StateSync`, `ProjectState` undo/redo, echo prevention, and the full loading sequence. |

## For AI Agents

If you are an AI agent working on Songbird, read these docs in order:

1. **`bird.md`** — understand the file format before editing `.bird` files or the parser
2. **`state-management.md`** — understand how state flows before touching stores, sync, or undo/redo
3. **[`app/README.md`](../app/README.md)** — C++ backend architecture overview
4. **[`react_ui/src/README.md`](../react_ui/src/README.md)** — React frontend architecture overview
