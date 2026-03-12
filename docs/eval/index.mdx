# Eval (`eval/`)

LLM evaluation framework for testing the Songbird AI copilot's ability to generate and edit `.bird` files.

## Overview

Runs a suite of 50 music composition prompts through the Gemini API, simulates tool calls (no live C++ backend needed), validates the generated `.bird` files, and uses LLM-as-judge scoring for quality assessment.

## Files

| File | Purpose |
|------|---------|
| `run_eval.js` | Main evaluation runner — sends prompts to Gemini, handles multi-turn tool simulation, validates output, scores with LLM-as-judge. |
| `prompts.json` | 50 evaluation prompts covering music composition tasks (create beats, add tracks, change arrangements, etc.). |
| `baseline.bird` | Baseline `.bird` project file used as the starting state for each evaluation prompt. |
| `report.md` | Generated evaluation report with pass/fail rates and scoring. |
| `results/` | Per-prompt result files (JSON) containing generated `.bird` files, tool calls, scores, and timing. |

## Usage

```bash
# Run all 50 prompts
GEMINI_API_KEY=xxx node eval/run_eval.js

# Run specific prompts
GEMINI_API_KEY=xxx node eval/run_eval.js --ids 1,2,3

# Generate report from existing results
node eval/run_eval.js --report
```

## Architecture

```
Prompt → Gemini API (with tool declarations)
  → Model returns tool calls (update_bird_file, validate_bird_file, etc.)
  → Eval runner simulates tool responses (mock plugin params, validation)
  → Model may continue with more tool calls (up to 5 rounds)
  → Final text response + generated .bird content
  → validateBird() checks structural correctness
  → LLM-as-judge scores quality (1-10) using a separate Gemini call
  → Results saved to results/<promptId>.json
```

## Scoring

Each prompt is scored on:
- **Structural validity** — Does the `.bird` file parse correctly?
- **Tool use correctness** — Were the right tools called with valid arguments?
- **Musical quality** — LLM-as-judge evaluates musicality, complexity, and adherence to the prompt
