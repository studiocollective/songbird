# Songbird LLM Eval Report

**Overall Score: 216/250 (86%)**

Run: 2026-02-21T01:19:36.635Z

## By Category

### note_length — 51/60 (avg 8.5/10)

**Failures (score < 6):**
- [3/10] "Convert the melody from 16th notes to dotted 8th notes for a swung feel."
  - Relevance: The AI correctly explained the approach but failed to make any tool calls to actually apply the rhythmic changes to the melody.
  - Quality: The explanation of the dotted eighth and sixteenth note rhythm (x _x _x x) is musically accurate for a hard swung feel.

### chord_rhythm — 84/90 (avg 9.3/10)

### humanization — 41/50 (avg 8.2/10)

### vibe — 40/50 (avg 8.0/10)

**Failures (score < 6):**
- [0/10] "This sounds too stiff and quantized. Add some rhythmic looseness — some notes sl"
  - Relevance: The AI hallucinated a 't' command for timing instead of acknowledging the language's grid-locked limitation and suggesting velocity variations as a workaround.
  - Quality: The resulting Bird file is invalid because it uses a non-existent 't' parameter for note timing.

## Common Failure Patterns

2 prompts scored below 6/10:

- **[55] Convert the melody from 16th notes to dotted 8th notes for a swung fee**: The AI correctly explained the approach but failed to make any tool calls to actually apply the rhythmic changes to the melody.
- **[71] This sounds too stiff and quantized. Add some rhythmic looseness — som**: The AI hallucinated a 't' command for timing instead of acknowledging the language's grid-locked limitation and suggesting velocity variations as a workaround.

## Recommendations

*(Generated from eval results — see results/ directory for full per-prompt JSON)*
