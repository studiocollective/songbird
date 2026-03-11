# Lib (`react_ui/src/lib/`)

Non-React utility modules — JUCE bridge, AI integration, theme system, and shared helpers.

## Structure

```
lib/
├── index.ts          — Re-exports (Juce, isPlugin, cn, etc.)
├── utils.ts          — Shared utilities: cn() for class merging, isPlugin detection
├── juce/             — JUCE WebView bridge layer
├── ai/               — Gemini AI integration
└── theme/            — Dark/light theme CSS and system detection
```

## Modules

### `juce/` — JUCE WebView Bridge

The low-level interop layer between the React app and the C++ backend.

| File | Purpose |
|------|---------|
| `juce.js` | JUCE-provided JS bridge library (v7.0.7). `getNativeFunction()`, `SliderState`, `ToggleState`, `ComboBoxState`. Promise-based async call/response via `__juce__complete` events. |
| `juce.d.ts` | TypeScript declarations for `Juce` exports. |
| `window.d.ts` | TypeScript declarations for `window.__JUCE__` global — backend event system, native function dispatch. |
| `check_native_interop.js` | Validates that `window.__JUCE__` exists at import time. Warns in dev mode when running outside the JUCE WebView. |

**Key exports:** `Juce.getNativeFunction(name)` — returns a function that calls a registered C++ handler and returns a Promise with the result.

### `ai/` — Gemini AI Integration

Client-side AI copilot using Google's Gemini API with function calling.

| File | Purpose |
|------|---------|
| `gemini.ts` | `GeminiService` class — streaming chat with multi-turn tool use. Handles `update_bird_file`, `validate_bird_file`, `get_plugin_params`, `set_plugin_param`, `set_track_mixer`, `set_bpm`, `set_lyria_track_config`, `set_lyria_track_prompts`, `set_lyria_quantize` function calls. Max 5 tool rounds per message. |
| `prompts.ts` | System prompt construction — injects bird notation reference, current project state, and available tools into the Gemini session. |
| `validator.ts` | Client-side `.bird` file validation — checks structure, token validity, and pattern alignment before sending to C++. |

**Design:** The AI copilot uses Gemini's function calling to edit `.bird` files and tweak parameters. Each tool call is routed to a C++ native function or applied directly to the Zustand store. The `ChatPanel` orchestrates the conversation loop.

### `theme/` — Theme System

| File | Purpose |
|------|---------|
| `index.ts` | Theme detection and application. Supports `auto` (follows system), `dark`, and `light` modes. Injects CSS variables. |
| `dark.css` | Dark theme CSS custom properties (colors, shadows, surfaces). |
| `light.css` | Light theme CSS custom properties. |

## Conventions

- **`isPlugin`** — Boolean flag (`true` when running inside JUCE WebView, `false` in browser dev mode). Guards all native function calls.
- **`cn()`** — Class name merging utility (wraps `clsx` + `tailwind-merge`). Use this instead of raw template literals for conditional Tailwind classes.
- **No React in `lib/`** — This layer should remain framework-agnostic. No hooks, no JSX. Components that need these utilities import from `@/lib`.
