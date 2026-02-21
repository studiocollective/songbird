#!/usr/bin/env node
/**
 * Songbird LLM Eval Runner
 * 
 * Usage:
 *   GEMINI_API_KEY=xxx node run_eval.js          # Run all 50 prompts
 *   GEMINI_API_KEY=xxx node run_eval.js --ids 1,2,3  # Run specific prompts
 *   node run_eval.js --report                    # Print summary from existing results
 * 
 * Scoring rubric (max 10 per prompt):
 *   validity   (0-3):  Did the bird file parse without errors? (auto)
 *   relevance  (0-4):  Did the response match the request? (LLM judge)
 *   quality    (0-3):  Was the musical content sensible? (LLM judge)
 */

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

// ─── Config ────────────────────────────────────────────────────────────────
const API_KEY = process.env.GEMINI_API_KEY;
const MODEL = 'gemini-3-flash-preview';
const JUDGE_MODEL = 'gemini-3.1-pro-preview';
const RESULTS_DIR = path.join(__dirname, 'results');
const BASELINE = fs.readFileSync(path.join(__dirname, 'baseline.bird'), 'utf8');
const PROMPTS = require('./prompts.json').prompts;

const args = process.argv.slice(2);
const reportOnly = args.includes('--report');
const specificIds = args.includes('--ids')
  ? args[args.indexOf('--ids') + 1].split(',').map(Number)
  : null;

if (!reportOnly && !API_KEY) {
  console.error('Error: Set GEMINI_API_KEY env var');
  process.exit(1);
}

fs.mkdirSync(RESULTS_DIR, { recursive: true });

// ─── Bird Validator (ported from validator.ts) ──────────────────────────────
const MACROS = [
  'brightness', 'resonance', 'attack', 'release', 'sub_level', 'decay',
  'space', 'echo', 'feedback', 'drive', 'input_gain', 'low_cut',
  'eq_mid_gain', 'comp_thresh', 'pitch', 'volume', 'width', 'amount',
  'filter_attack', 'filter_decay', 'lfo_rate', 'chorus'
];

function validateBird(content) {
  const lines = content.split('\n');
  let inGlobal = true;
  let currentChannel = '';
  const definedChannels = new Set();
  let currentPatternLength = 0;

  for (let i = 0; i < lines.length; i++) {
    const trimmed = lines[i].trim();
    if (!trimmed || trimmed.startsWith('#')) continue;
    const tokens = trimmed.split(/\s+/);
    const cmd = tokens[0];

    if (cmd === 'ch') {
      if (tokens.length < 3) return { isValid: false, error: `Line ${i+1}: 'ch' needs number and name` };
      const chName = `${tokens[1]} ${tokens[2]}`;
      if (inGlobal) definedChannels.add(chName);
      currentChannel = chName;
      currentPatternLength = 0;
    } else if (cmd === 'sec') {
      inGlobal = false;
      if (tokens.length < 2) return { isValid: false, error: `Line ${i+1}: 'sec' needs a name` };
      currentChannel = '';
    } else if (cmd === 'arr') {
      inGlobal = false;
    } else if (['plugin', 'fx', 'strip'].includes(cmd)) {
      if (!inGlobal) return { isValid: false, error: `Line ${i+1}: '${cmd}' must be in global block` };
    } else if (cmd === 'p' || cmd === 'v' || cmd === 'n' || cmd === 'sw' || cmd === 't' || MACROS.includes(cmd)) {
      if (inGlobal) {
        if (MACROS.includes(cmd) && tokens[1] === 'ramp') continue;
        if (cmd === 'sw' || cmd === 't') continue; // sw/t can appear in global ch block
        return { isValid: false, error: `Line ${i+1}: Cannot define patterns in global space` };
      }
      if (!currentChannel) return { isValid: false, error: `Line ${i+1}: Pattern without preceding 'ch'` };
      if (!definedChannels.has(currentChannel)) return { isValid: false, error: `Line ${i+1}: Channel '${currentChannel}' not defined globally` };
      const count = tokens.length - 1;
      if (cmd === 'p') {
        currentPatternLength = count;
      } else if ((cmd === 'v' || MACROS.includes(cmd)) && currentPatternLength > 0 && count > 1 && count !== currentPatternLength) {
        return { isValid: false, error: `Line ${i+1}: Alignment error — '${cmd}' has ${count} values but pattern has ${currentPatternLength}` };
      }
    } else if (cmd.startsWith('```')) {
      return { isValid: false, error: `Line ${i+1}: Markdown fence found — remove the \`\`\`bird wrapper` };
    }
  }
  return { isValid: true };
}

// ─── Gemini API call ─────────────────────────────────────────────────────────
function buildSystemPrompt() {
  return `You are an AI copilot for Songbird, a music sequencer that uses Bird notation.

You have tools to edit the project's .bird file and to directly control plugin parameters in real-time.

**Before taking any action, think carefully:**
- If the request is ambiguous or vague (e.g. "make it better", "fix it", "add some effects"), ask a specific clarifying question. Do NOT guess and edit.
- A bad edit is worse than no edit.
- Do NOT invent Bird syntax that doesn't exist — only use the tokens documented below. There is no sidechain keyword in Bird.
- Use validate_bird_file before saving if you're not confident.

## Bird Syntax
Global block: ch + plugin/fx/strip definitions. Then arr (arrangement). Then sec blocks.
| Token | Meaning | Example |
|-------|---------|---------| 
| ch  | Channel | ch 1 kick |
| plugin | Instrument | plugin kick / mini / jup8 |
| arr | Section order + bars | arr intro 4 verse 8 |
| sec | Section | sec intro |
| cont | Continue pattern | cont |
| p   | Pattern (w=whole q=quarter x=16th _=rest) — each value is one note slot | p q q q q (4 beats) |
| v   | Velocity 0-127 | v 80 60 |
| n   | MIDI notes or offsets | n 36 +7 +12 |
| sw  | Swing (50=straight, 67=triplet, ~N=humanize jitter) | sw 60 ~5 |
| t   | Per-note timing offset in ticks (cycles like v) | t 0 +5 -3 0 |

Pattern timing rule: all p values in a channel must sum to the same total bar length. 4/4 = 4 quarters or 16 sixteenths. Mismatched lengths cause desync!

Rules: plugins ONLY in global ch block, NEVER in sec. Always include full file in update_bird_file. Never invent Bird syntax tokens.
For humanization: use sw for groove/swing, t for per-note micro-timing, v variation for dynamic feel.

## Mix Control  
set_track_mixer(trackId, volumeDb, pan, mute, solo) — volumeDb: -60 to +6.
set_bpm(bpm)
For relative changes (e.g. "lower by 6dB"): ask the current level if you don't know it instead of assuming 0dB.

## Plugin Parameter Control
1. Call get_plugin_params(trackId) to discover parameter names and current values
2. IMMEDIATELY follow up with set_plugin_param(trackId, paramName, value 0-1) — do NOT stop after step 1`;
}

const TOOL_DECLARATIONS = [
  {
    name: 'update_bird_file',
    description: 'Save the complete .bird file',
    parameters: { type: 'OBJECT', properties: { content: { type: 'STRING' } }, required: ['content'] }
  },
  {
    name: 'validate_bird_file',
    description: 'Validate bird file syntax',
    parameters: { type: 'OBJECT', properties: { content: { type: 'STRING' } }, required: ['content'] }
  },
  {
    name: 'get_plugin_params',
    description: 'Get plugin parameters for a track',
    parameters: { type: 'OBJECT', properties: { trackId: { type: 'NUMBER' } }, required: ['trackId'] }
  },
  {
    name: 'set_plugin_param',
    description: 'Set a plugin parameter (0-1)',
    parameters: { type: 'OBJECT', properties: { trackId: { type: 'NUMBER' }, paramName: { type: 'STRING' }, value: { type: 'NUMBER' } }, required: ['trackId', 'paramName', 'value'] }
  },
  {
    name: 'set_track_mixer',
    description: 'Set track volume/pan/mute/solo',
    parameters: { type: 'OBJECT', properties: { trackId: { type: 'NUMBER' }, volumeDb: { type: 'NUMBER' }, pan: { type: 'NUMBER' }, mute: { type: 'BOOLEAN' }, solo: { type: 'BOOLEAN' } }, required: ['trackId', 'volumeDb', 'pan', 'mute', 'solo'] }
  },
  {
    name: 'set_bpm',
    description: 'Set project BPM',
    parameters: { type: 'OBJECT', properties: { bpm: { type: 'NUMBER' } }, required: ['bpm'] }
  }
];

// ─── Mock plugin parameters (from MacroMapper.cpp verified scans) ────────────
const MOCK_PLUGIN_PARAMS = {
  // Track 0: Melody — Mini V3 + Console 1
  0: [
    { id: 'p0', name: 'Cutoff Frequency', value: 0.65, displayValue: '8.2 kHz', min: 0, max: 1 },
    { id: 'p1', name: 'Resonance', value: 0.2, displayValue: '20%', min: 0, max: 1 },
    { id: 'p2', name: 'Attack Time', value: 0.05, displayValue: '5 ms', min: 0, max: 1 },
    { id: 'p3', name: 'Decay Time', value: 0.4, displayValue: '400 ms', min: 0, max: 1 },
    { id: 'p4', name: 'Sustain Level', value: 0.7, displayValue: '70%', min: 0, max: 1 },
    { id: 'p5', name: 'Release Time', value: 0.3, displayValue: '300 ms', min: 0, max: 1 },
    { id: 'p6', name: 'Filter Env Depth', value: 0.5, displayValue: '50%', min: 0, max: 1 },
    { id: 'p7', name: 'LFO Rate', value: 0.3, displayValue: '2.4 Hz', min: 0, max: 1 },
    { id: 'p8', name: 'LFO Amount', value: 0.0, displayValue: '0%', min: 0, max: 1 },
    { id: 'p9', name: 'Drive', value: 0.0, displayValue: '0%', min: 0, max: 1 },
    { id: 'p10', name: 'Volume', value: 0.8, displayValue: '-2 dB', min: 0, max: 1 },
    { id: 'p11', name: 'Input Gain', value: 0.5, displayValue: '0 dB', min: 0, max: 1 },
    { id: 'p12', name: 'Compression', value: 0.3, displayValue: '-8 dB', min: 0, max: 1 },
  ],
  // Track 1: Bass — SubLabXL + Console 1
  1: [
    { id: 'p0', name: 'Sub:Filter Cutoff', value: 0.4, displayValue: '1.2 kHz', min: 0, max: 1 },
    { id: 'p1', name: 'Sub:Filter Resonance', value: 0.15, displayValue: '15%', min: 0, max: 1 },
    { id: 'p2', name: 'Sub:Distortion Amount', value: 0.1, displayValue: '10%', min: 0, max: 1 },
    { id: 'p3', name: 'Sub:Level', value: 0.8, displayValue: '-2 dB', min: 0, max: 1 },
    { id: 'p4', name: 'Sub:Amp Attack', value: 0.02, displayValue: '2 ms', min: 0, max: 1 },
    { id: 'p5', name: 'Sub:Amp Decay', value: 0.5, displayValue: '500 ms', min: 0, max: 1 },
    { id: 'p6', name: 'Sub:Amp Sustain', value: 0.8, displayValue: '80%', min: 0, max: 1 },
    { id: 'p7', name: 'Sub:Amp Release', value: 0.2, displayValue: '200 ms', min: 0, max: 1 },
    { id: 'p8', name: 'Sub:Pitch Env Depth', value: 0.1, displayValue: '10%', min: 0, max: 1 },
    { id: 'p9', name: 'Input Gain', value: 0.5, displayValue: '0 dB', min: 0, max: 1 },
    { id: 'p10', name: 'Compression', value: 0.4, displayValue: '-6 dB', min: 0, max: 1 },
  ],
  // Track 2: Chords — Jup-8 V4 + Console 1
  2: [
    { id: 'p0', name: 'Filter Cutoff', value: 0.55, displayValue: '4.5 kHz', min: 0, max: 1 },
    { id: 'p1', name: 'Filter Resonance', value: 0.25, displayValue: '25%', min: 0, max: 1 },
    { id: 'p2', name: 'Amp Attack', value: 0.15, displayValue: '50 ms', min: 0, max: 1 },
    { id: 'p3', name: 'Amp Decay', value: 0.6, displayValue: '600 ms', min: 0, max: 1 },
    { id: 'p4', name: 'Amp Sustain', value: 0.75, displayValue: '75%', min: 0, max: 1 },
    { id: 'p5', name: 'Amp Release', value: 0.4, displayValue: '400 ms', min: 0, max: 1 },
    { id: 'p6', name: 'Filter Attack', value: 0.1, displayValue: '30 ms', min: 0, max: 1 },
    { id: 'p7', name: 'Filter Decay', value: 0.5, displayValue: '500 ms', min: 0, max: 1 },
    { id: 'p8', name: 'LFO Rate', value: 0.2, displayValue: '1.5 Hz', min: 0, max: 1 },
    { id: 'p9', name: 'Volume', value: 0.75, displayValue: '-3 dB', min: 0, max: 1 },
    { id: 'p10', name: 'Input Gain', value: 0.5, displayValue: '0 dB', min: 0, max: 1 },
  ],
  // Track 3: Kick — Kick 3
  3: [
    { id: 'p0', name: 'synth: Main | Pitch', value: 0.45, displayValue: '52 Hz', min: 0, max: 1 },
    { id: 'p1', name: 'synth: Main | Drive', value: 0.2, displayValue: '20%', min: 0, max: 1 },
    { id: 'p2', name: 'synth: Main | Level', value: 0.85, displayValue: '-1 dB', min: 0, max: 1 },
    { id: 'p3', name: 'synth: ADSR-1 | Attack', value: 0.01, displayValue: '1 ms', min: 0, max: 1 },
    { id: 'p4', name: 'synth: ADSR-1 | Decay', value: 0.35, displayValue: '350 ms', min: 0, max: 1 },
    { id: 'p5', name: 'synth: ADSR-1 | Sustain', value: 0.0, displayValue: '0%', min: 0, max: 1 },
    { id: 'p6', name: 'synth: ADSR-1 | Release', value: 0.15, displayValue: '150 ms', min: 0, max: 1 },
  ],
  // Track 4: Snare — Heartbeat
  4: [
    { id: 'p0', name: 'SN1 Pitch', value: 0.5, displayValue: '200 Hz', min: 0, max: 1 },
    { id: 'p1', name: 'SN1 Decay', value: 0.4, displayValue: '180 ms', min: 0, max: 1 },
    { id: 'p2', name: 'SN1 Level', value: 0.8, displayValue: '-2 dB', min: 0, max: 1 },
    { id: 'p3', name: 'BD1 Pitch', value: 0.45, displayValue: '55 Hz', min: 0, max: 1 },
    { id: 'p4', name: 'BD1 Decay', value: 0.35, displayValue: '300 ms', min: 0, max: 1 },
    { id: 'p5', name: 'BD1 Level', value: 0.7, displayValue: '-3 dB', min: 0, max: 1 },
    { id: 'p6', name: 'HH Pitch', value: 0.6, displayValue: '8 kHz', min: 0, max: 1 },
    { id: 'p7', name: 'HH Decay', value: 0.15, displayValue: '50 ms', min: 0, max: 1 },
    { id: 'p8', name: 'HH Level', value: 0.65, displayValue: '-4 dB', min: 0, max: 1 },
    { id: 'p9', name: 'Stereo Width', value: 0.5, displayValue: '50%', min: 0, max: 1 },
    { id: 'p10', name: 'Master Saturation', value: 0.1, displayValue: '10%', min: 0, max: 1 },
  ],
  // Track 5: HiHat — Heartbeat (same plugin)
  5: [
    { id: 'p0', name: 'HH Pitch', value: 0.6, displayValue: '8 kHz', min: 0, max: 1 },
    { id: 'p1', name: 'HH Decay', value: 0.15, displayValue: '50 ms', min: 0, max: 1 },
    { id: 'p2', name: 'HH Level', value: 0.65, displayValue: '-4 dB', min: 0, max: 1 },
    { id: 'p3', name: 'SN1 Pitch', value: 0.5, displayValue: '200 Hz', min: 0, max: 1 },
    { id: 'p4', name: 'SN1 Decay', value: 0.4, displayValue: '180 ms', min: 0, max: 1 },
    { id: 'p5', name: 'BD1 Pitch', value: 0.45, displayValue: '55 Hz', min: 0, max: 1 },
    { id: 'p6', name: 'BD1 Decay', value: 0.35, displayValue: '300 ms', min: 0, max: 1 },
    { id: 'p7', name: 'Stereo Width', value: 0.5, displayValue: '50%', min: 0, max: 1 },
  ],
};

// ─── Gemini API (multi-turn capable) ─────────────────────────────────────────
function callGemini(model, systemPrompt, contents, tools) {
  const payload = {
    ...(systemPrompt ? { system_instruction: { parts: [{ text: systemPrompt }] } } : {}),
    contents,
    ...(tools ? { tools: [{ functionDeclarations: tools }] } : {}),
    generationConfig: { temperature: 0.7, maxOutputTokens: 4096 }
  };
  const tmp = `/tmp/eval_payload_${Date.now()}.json`;
  fs.writeFileSync(tmp, JSON.stringify(payload));
  try {
    const out = execSync(
      `curl -s --max-time 90 -X POST \
"https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${API_KEY}" \
-H "Content-Type: application/json" -d @${tmp}`,
      { maxBuffer: 10 * 1024 * 1024 }
    ).toString();
    fs.unlinkSync(tmp);
    return JSON.parse(out);
  } catch(e) {
    try { fs.unlinkSync(tmp); } catch {}
    throw new Error('curl: ' + e.message.slice(0, 200));
  }
}

// ─── Extract tool calls and text from Gemini response ───────────────────────
function parseResponse(resp) {
  const rawParts = resp?.candidates?.[0]?.content?.parts ?? [];
  const text = rawParts.filter(p => p.text).map(p => p.text).join('');
  const toolCalls = rawParts.filter(p => p.functionCall).map(p => ({
    name: p.functionCall.name,
    args: p.functionCall.args
  }));
  return { text, toolCalls, rawParts };
}

// ─── Simulate a tool call, returning a mock response ────────────────────────
function simulateToolCall(tc) {
  if (tc.name === 'get_plugin_params') {
    const tid = tc.args.trackId;
    const params = MOCK_PLUGIN_PARAMS[tid];
    if (params) return { success: true, trackId: tid, params };
    return { success: false, error: 'Track not found' };
  }
  if (tc.name === 'set_plugin_param') {
    return { success: true, plugin: 'MockPlugin', param: tc.args.paramName, value: tc.args.value };
  }
  if (tc.name === 'validate_bird_file') {
    if (tc.args.content) return validateBird(tc.args.content);
    return { isValid: false, error: 'No content' };
  }
  if (tc.name === 'set_track_mixer') {
    return { success: true };
  }
  if (tc.name === 'set_bpm') {
    return { success: true, bpm: tc.args.bpm };
  }
  if (tc.name === 'update_bird_file') {
    return { success: true };
  }
  return { error: 'Unknown tool' };
}

// ─── LLM-as-judge scoring ─────────────────────────────────────────────────────
function judgeResponse(prompt, allToolCalls, finalText, birdContent) {
  const judgePrompt = `You are evaluating an AI music assistant's response to a user request.

USER REQUEST: "${prompt.prompt}"
CATEGORY: ${prompt.category}
EXPECTED BEHAVIOR: ${prompt.notes}

AI RESPONSE TEXT:
${finalText || '(no text \u2014 tool calls only)'}

TOOL CALLS MADE:
${JSON.stringify(allToolCalls, null, 2)}

${birdContent ? `RESULTING BIRD FILE:\n\`\`\`\n${birdContent}\n\`\`\`` : ''}

Score the response on two dimensions (JSON response only):
{
  "relevance": <0-4>,   // 0=wrong tool/approach, 2=correct approach but imprecise, 4=exactly right
  "quality": <0-3>,     // 0=musically wrong/nonsensical, 1=basic but ok, 2=good, 3=musically excellent
  "relevance_reason": "<one sentence>",
  "quality_reason": "<one sentence>"
}

For clarification_required prompts: relevance=4 if LLM asked instead of guessing, relevance=0 if it made edits.`;

  try {
    const resp = callGemini(JUDGE_MODEL, '', [{ role: 'user', parts: [{ text: judgePrompt }] }], null);
    const { text } = parseResponse(resp);
    const match = text.match(/\{[\s\S]*\}/);
    if (match) return JSON.parse(match[0]);
  } catch (e) {
    console.error('Judge error:', e.message);
  }
  return { relevance: 0, quality: 0, relevance_reason: 'Judge failed', quality_reason: 'Judge failed' };
}

// ─── Run a single prompt (multi-turn with tool simulation) ──────────────────
function runPrompt(prompt) {
  const resultFile = path.join(RESULTS_DIR, `p${prompt.id}.json`);
  const contextualMessage = `Current project:\n\`\`\`bird\n${BASELINE}\n\`\`\`\n\n${prompt.prompt}`;
  const totalPrompts = PROMPTS.length;
  console.log(`\n[${prompt.id}/${totalPrompts}] ${prompt.category}: "${prompt.prompt.slice(0, 60)}..."`);

  let birdContent = null, validationResult = null;
  const allToolCalls = [];
  let finalText = '';

  try {
    // Build conversation contents for multi-turn
    const contents = [{ role: 'user', parts: [{ text: contextualMessage }] }];
    const MAX_TURNS = 3;

    for (let turn = 0; turn < MAX_TURNS; turn++) {
      const raw = callGemini(MODEL, buildSystemPrompt(), contents, TOOL_DECLARATIONS);
      const response = parseResponse(raw);

      if (response.text) finalText += response.text;

      // No tool calls — done
      if (!response.toolCalls.length) {
        if (response.text) {
          console.log(`  \u2192 Text: "${response.text.slice(0, 80)}..."`);
        } else if (turn === 0) {
          console.log(`  \u26a0 Empty response`);
        }
        break;
      }

      // Process tool calls
      const toolResponseParts = [];

      for (const tc of response.toolCalls) {
        allToolCalls.push(tc);

        // Log
        if (tc.name === 'update_bird_file') {
          birdContent = tc.args.content;
          validationResult = validateBird(birdContent);
          console.log(`  \u2192 update_bird_file: ${validationResult.isValid ? '\u2713 valid' : '\u2717 ' + validationResult.error}`);
        } else if (tc.name === 'get_plugin_params') {
          console.log(`  \u2192 get_plugin_params(track=${tc.args.trackId})`);
        } else if (tc.name === 'set_plugin_param') {
          console.log(`  \u2192 set_plugin_param(track=${tc.args.trackId}, param="${tc.args.paramName}", val=${tc.args.value})`);
        } else if (tc.name === 'set_track_mixer') {
          console.log(`  \u2192 set_track_mixer(track=${tc.args.trackId}, vol=${tc.args.volumeDb}dB)`);
        } else if (tc.name === 'set_bpm') {
          console.log(`  \u2192 set_bpm(${tc.args.bpm})`);
        } else if (tc.name === 'validate_bird_file') {
          console.log(`  \u2192 validate_bird_file`);
        }

        // Build tool response (simplified format for Gemini API)
        const mockResult = simulateToolCall(tc);
        toolResponseParts.push({
          functionResponse: { name: tc.name, response: mockResult }
        });
      }

      // Only continue if there are tool calls that need follow-up (get_plugin_params)
      const needsFollowUp = response.toolCalls.some(tc =>
        tc.name === 'get_plugin_params' || tc.name === 'validate_bird_file'
      );

      if (!needsFollowUp) break; // Terminal tool calls like set_plugin_param, update_bird_file

      // Append RAW model response (includes thoughtSignature required by Gemini 3)
      // and tool results for next turn
      contents.push({ role: 'model', parts: response.rawParts });
      contents.push({ role: 'user', parts: toolResponseParts });
      console.log(`  \u21bb Multi-turn: sending tool results back (turn ${turn + 1})...`);
    }
  } catch (e) {
    console.error(`  \u2717 API error: ${e.message}`);
    const result = { id: prompt.id, category: prompt.category, prompt: prompt.prompt,
      error: e.message, scores: { validity: 0, relevance: 0, quality: 0, total: 0 } };
    fs.writeFileSync(resultFile, JSON.stringify(result, null, 2));
    return result;
  }

  // Validity score
  let validity = 0;
  if (!birdContent) validity = 1; // no bird file needed (tool call or clarification)
  else if (validationResult?.isValid) validity = 3;
  else validity = 0;

  // LLM Judge score
  const judgment = judgeResponse(prompt, allToolCalls, finalText, birdContent);
  const scores = {
    validity,
    relevance: judgment.relevance,
    quality: judgment.quality,
    total: validity + judgment.relevance + judgment.quality
  };

  console.log(`  Score: validity=${validity}/3 relevance=${judgment.relevance}/4 quality=${judgment.quality}/3 \u2192 ${scores.total}/10`);

  const result = {
    id: prompt.id, category: prompt.category, prompt: prompt.prompt,
    response: { text: finalText?.slice(0, 500), toolCalls: allToolCalls },
    birdContent, validationResult, judgment, scores
  };
  fs.writeFileSync(resultFile, JSON.stringify(result, null, 2));
  return result;
}

// ─── Report ──────────────────────────────────────────────────────────────────
function generateReport(results) {
  const byCategory = {};
  let totalScore = 0, totalMax = 0;

  for (const r of results) {
    if (r.error) continue;
    const cat = r.category;
    if (!byCategory[cat]) byCategory[cat] = { scores: [], prompts: [] };
    byCategory[cat].scores.push(r.scores.total);
    byCategory[cat].prompts.push(r);
    totalScore += r.scores.total;
    totalMax += 10;
  }

  let report = `# Songbird LLM Eval Report\n\n`;
  report += `**Overall Score: ${totalScore}/${totalMax} (${Math.round(totalScore/totalMax*100)}%)**\n\n`;
  report += `Run: ${new Date().toISOString()}\n\n`;
  report += `## By Category\n\n`;

  for (const [cat, data] of Object.entries(byCategory)) {
    const avg = data.scores.reduce((a,b) => a+b, 0) / data.scores.length;
    const max = data.scores.length * 10;
    const total = data.scores.reduce((a,b) => a+b, 0);
    report += `### ${cat} — ${total}/${max} (avg ${avg.toFixed(1)}/10)\n\n`;

    // Show failures
    const failures = data.prompts.filter(p => p.scores.total < 6);
    if (failures.length) {
      report += `**Failures (score < 6):**\n`;
      for (const f of failures) {
        report += `- [${f.scores.total}/10] "${f.prompt.slice(0, 80)}"\n`;
        report += `  - Relevance: ${f.judgment?.relevance_reason}\n`;
        report += `  - Quality: ${f.judgment?.quality_reason}\n`;
      }
      report += '\n';
    }
  }

  // Failure patterns
  report += `## Common Failure Patterns\n\n`;
  const allFailures = results.filter(r => !r.error && r.scores.total < 6);
  if (allFailures.length === 0) {
    report += `No significant failures (all scored ≥ 6/10). 🎉\n`;
  } else {
    report += `${allFailures.length} prompts scored below 6/10:\n\n`;
    for (const f of allFailures) {
      report += `- **[${f.id}] ${f.prompt.slice(0,70)}**: ${f.judgment?.relevance_reason}\n`;
    }
  }

  report += `\n## Recommendations\n\n`;
  report += `*(Generated from eval results — see results/ directory for full per-prompt JSON)*\n`;

  return report;
}

// ─── Main ─────────────────────────────────────────────────────────────────────
function main() {
  if (reportOnly) {
    const files = fs.readdirSync(RESULTS_DIR).filter(f => f.endsWith('.json'));
    const results = files.map(f => JSON.parse(fs.readFileSync(path.join(RESULTS_DIR, f))));
    const report = generateReport(results);
    const reportPath = path.join(__dirname, 'report.md');
    fs.writeFileSync(reportPath, report);
    console.log(report);
    console.log(`\nWritten to ${reportPath}`);
    return;
  }

  const promptsToRun = specificIds
    ? PROMPTS.filter(p => specificIds.includes(p.id))
    : PROMPTS;

  console.log(`Running ${promptsToRun.length} eval prompts against ${MODEL}...\n`);

  const results = [];
  for (const prompt of promptsToRun) {
    const result = runPrompt(prompt);
    results.push(result);
    execSync('sleep 0.3');
  }

  const report = generateReport(results);
  const reportPath = path.join(__dirname, 'report.md');
  fs.writeFileSync(reportPath, report);
  console.log('\n' + '='.repeat(60));
  console.log(report);
  console.log(`\nFull results in: ${RESULTS_DIR}`);
  console.log(`Report: ${reportPath}`);
}

main();
