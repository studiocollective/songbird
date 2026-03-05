import type { ChatMessage } from '@/data/slices';

export interface ToolCall {
  name: string;
  args: Record<string, unknown>;
}

export interface GeminiResponse {
  content: string;
  error?: string;
  model?: string;
  toolCalls?: ToolCall[];
}

// Tool declaration for editing the bird file
const BIRD_TOOL = {
  functionDeclarations: [{
    name: 'update_bird_file',
    description: 'Updates the .bird project file with new content. Use this to make changes to the music composition. Always output the complete file contents.',
    parameters: {
      type: 'OBJECT',
      properties: {
        content: {
          type: 'STRING',
          description: 'The complete updated .bird file content',
        },
      },
      required: ['content'],
    },
  }],
};

// Tool declaration for validating bird syntax
const VALIDATE_TOOL = {
  functionDeclarations: [{
    name: 'validate_bird_file',
    description: 'Validates the structural syntax of a .bird file before saving. Use this to check your work for errors such as unaligned columns or missing global declarations. Returns an object { isValid: boolean, error?: string }.',
    parameters: {
      type: 'OBJECT',
      properties: {
        content: {
          type: 'STRING',
          description: 'The .bird file content to validate',
        },
      },
      required: ['content'],
    },
  }],
};

// Tool declaration for reading live plugin parameter list
const GET_PLUGIN_PARAMS_TOOL = {
  functionDeclarations: [{
    name: 'get_plugin_params',
    description: 'Returns the full list of automatable parameters for all plugins loaded on a track. Call this before set_plugin_param to discover exact parameter names and their current values. Returns an array of { plugin, params: [{id, name, value (0-1), displayValue}] }.',
    parameters: {
      type: 'OBJECT',
      properties: {
        trackId: {
          type: 'NUMBER',
          description: 'Zero-based track index (same as the track order in the bird file, starting from 0)',
        },
      },
      required: ['trackId'],
    },
  }],
};

// Tool declaration for setting a plugin parameter directly
const SET_PLUGIN_PARAM_TOOL = {
  functionDeclarations: [{
    name: 'set_plugin_param',
    description: 'Immediately sets a plugin parameter to a specific value. The value must be normalized between 0.0 and 1.0. Use get_plugin_params first to discover the exact parameter name. Returns { success, plugin, param, value } or { success: false, error }.',
    parameters: {
      type: 'OBJECT',
      properties: {
        trackId: {
          type: 'NUMBER',
          description: 'Zero-based track index',
        },
        paramName: {
          type: 'STRING',
          description: 'The parameter name or ID (case-insensitive, substring match). Use the exact name from get_plugin_params for reliability.',
        },
        value: {
          type: 'NUMBER',
          description: 'Normalized value between 0.0 (minimum) and 1.0 (maximum)',
        },
      },
      required: ['trackId', 'paramName', 'value'],
    },
  }],
};

// Tool: set mixer state for a single track
const SET_TRACK_MIXER_TOOL = {
  functionDeclarations: [{
    name: 'set_track_mixer',
    description: 'Sets the volume, pan, mute, and solo state for a single track. Use this when the user asks to adjust the mix (e.g. "lower the kick", "pan the synth left", "mute the drums"). Track index is the same as position in the bird file (0-based).',
    parameters: {
      type: 'OBJECT',
      properties: {
        trackId:  { type: 'NUMBER', description: 'Zero-based track index' },
        volumeDb: { type: 'NUMBER', description: 'Volume in dB. 0 = unity gain, -6 = half, -inf = silent. Typical range: -60 to +6.' },
        pan:      { type: 'NUMBER', description: 'Pan position: -1.0 = full left, 0 = centre, 1.0 = full right' },
        mute:     { type: 'BOOLEAN', description: 'Whether the track should be muted' },
        solo:     { type: 'BOOLEAN', description: 'Whether the track should be soloed' },
      },
      required: ['trackId', 'volumeDb', 'pan', 'mute', 'solo'],
    },
  }],
};

// Tool: set project BPM
const SET_BPM_TOOL = {
  functionDeclarations: [{
    name: 'set_bpm',
    description: 'Changes the project tempo. Use when the user asks to change speed, BPM, or tempo.',
    parameters: {
      type: 'OBJECT',
      properties: {
        bpm: { type: 'NUMBER', description: 'Beats per minute, between 20 and 300' },
      },
      required: ['bpm'],
    },
  }],
};

// Tool: set Lyria generated track config (temperature, density, brightness, BPM, etc.)
const SET_LYRIA_TRACK_CONFIG_TOOL = {
  functionDeclarations: [{
    name: 'set_lyria_track_config',
    description: 'Sets the generation config for a Lyria AI music track. Use this when the user asks to change the mood, energy, density, or style of a generated track. Does not affect MIDI tracks.',
    parameters: {
      type: 'OBJECT',
      properties: {
        trackId: { type: 'NUMBER', description: 'Zero-based track index of the Lyria generated track' },
        config: {
          type: 'OBJECT',
          description: 'Lyria config fields to set. All fields are optional.',
          properties: {
            temperature:   { type: 'NUMBER', description: 'Creativity / randomness (0.0–2.0, default 1.0)' },
            guidance:      { type: 'NUMBER', description: 'Prompt adherence (1.0–6.0, default 3.0)' },
            topK:          { type: 'NUMBER', description: 'Top-K sampling (default 250)' },
            density:       { type: 'NUMBER', description: 'Note density 0.0 (sparse) to 1.0 (dense)' },
            useDensity:    { type: 'BOOLEAN', description: 'Whether to use the density parameter' },
            brightness:    { type: 'NUMBER', description: 'Brightness / timbre 0.0 (dark) to 1.0 (bright)' },
            useBrightness: { type: 'BOOLEAN', description: 'Whether to use the brightness parameter' },
            muteBass:      { type: 'BOOLEAN', description: 'Mute the bass stem from Lyria output' },
            muteDrums:     { type: 'BOOLEAN', description: 'Mute the drums stem from Lyria output' },
            muteOther:     { type: 'BOOLEAN', description: 'Mute the other stem from Lyria output' },
            bpm:           { type: 'NUMBER', description: 'Override BPM for this track' },
            useBpm:        { type: 'BOOLEAN', description: 'Whether to enforce the BPM override' },
          },
        },
      },
      required: ['trackId', 'config'],
    },
  }],
};

// Tool: set Lyria track prompts
const SET_LYRIA_TRACK_PROMPTS_TOOL = {
  functionDeclarations: [{
    name: 'set_lyria_track_prompts',
    description: 'Sets the text prompts for a Lyria AI music track. Each prompt is a short descriptive phrase with an associated weight. Use this to steer the musical style of a generated track.',
    parameters: {
      type: 'OBJECT',
      properties: {
        trackId: { type: 'NUMBER', description: 'Zero-based track index of the Lyria generated track' },
        prompts: {
          type: 'ARRAY',
          description: 'Array of prompts — each has a text description and a weight between 0.0 and 1.0',
          items: {
            type: 'OBJECT',
            properties: {
              text:   { type: 'STRING', description: 'Short descriptive phrase, e.g. "jazz piano", "uplifting melodic house"' },
              weight: { type: 'NUMBER', description: 'Relative influence of this prompt (default 1.0)' },
            },
            required: ['text'],
          },
        },
      },
      required: ['trackId', 'prompts'],
    },
  }],
};

// Tool: quantize Lyria track to bar grid
const SET_LYRIA_QUANTIZE_TOOL = {
  functionDeclarations: [{
    name: 'set_lyria_quantize',
    description: 'Sets bar-boundary quantization for a Lyria track. When set, the Lyria stream is restarted only on bar boundaries so it stays in sync with the grid. Use bars=0 to disable.',
    parameters: {
      type: 'OBJECT',
      properties: {
        trackId: { type: 'NUMBER', description: 'Zero-based track index of the Lyria generated track' },
        bars:    { type: 'NUMBER', description: 'Number of bars between allowed restart points. 0 = off, 1 = every bar, 2 = every 2 bars, etc.' },
      },
      required: ['trackId', 'bars'],
    },
  }],
};

const MODELS = [
  'gemini-3.1-pro-preview',
  'gemini-3-flash-preview',
  'gemini-3.1-flash-lite-preview',
];

const BASE = 'https://generativelanguage.googleapis.com/v1beta/models';

/** Max tool-call round-trips before we force a text-only response */
const MAX_TOOL_ROUNDS = 5;

export interface StreamCallbacks {
  onChunk: (delta: string, accumulated: string) => void;
  onThought?: (delta: string, accumulated: string) => void;
  onFunctionCall?: (name: string, args: Record<string, unknown>) => void;
}

export class GeminiService {
  /**
   * Send a message to Gemini with function calling support.
   *
   * Flow:
   * 1. Streaming request with tool declarations
   * 2. If model returns functionCall → execute via onToolCall, send result back
   * 3. Repeat up to MAX_TOOL_ROUNDS times
   * 4. Stream the final text response
   */
  static async streamMessage(
    apiKey: string,
    history: ChatMessage[],
    systemPrompt: string,
    callbacks: StreamCallbacks,
    preferredModel: string = 'gemini-3-flash-preview',
    onToolCall?: (call: ToolCall) => Promise<Record<string, unknown>>,
    signal?: AbortSignal,
  ): Promise<GeminiResponse> {
    const contents = history.map(msg => ({
      role: msg.role === 'user' ? 'user' : 'model',
      parts: [{ text: msg.content }]
    }));

    const models = [preferredModel, ...MODELS.filter(m => m !== preferredModel)];

    for (const model of models) {
      // Check if already aborted
      if (signal?.aborted) {
        return { content: '' };
      }

      const result = await this.tryWithTools(model, apiKey, systemPrompt, contents, callbacks, onToolCall, signal);

      if (result.error && (result.error.includes('high demand') || result.error.includes('429') || result.error.includes('503'))) {
        console.log(`[Gemini] ${model} unavailable, trying fallback...`);
        continue;
      }

      return { ...result, model };
    }

    return { content: '', error: 'All models are currently overloaded. Please try again later.' };
  }

  private static async tryWithTools(
    model: string,
    apiKey: string,
    systemPrompt: string,
    contents: Array<{ role: string; parts: Array<Record<string, unknown>> }>,
    callbacks: StreamCallbacks,
    onToolCall?: (call: ToolCall) => Promise<Record<string, unknown>>,
    signal?: AbortSignal,
  ): Promise<GeminiResponse> {
    const generationConfig = {
      temperature: 0.7,
      topK: 40,
      topP: 0.95,
      maxOutputTokens: 16384,
      thinkingConfig: { includeThoughts: true },
    };

    const tools = onToolCall
      ? [BIRD_TOOL, VALIDATE_TOOL, GET_PLUGIN_PARAMS_TOOL, SET_PLUGIN_PARAM_TOOL,
         SET_TRACK_MIXER_TOOL, SET_BPM_TOOL,
         SET_LYRIA_TRACK_CONFIG_TOOL, SET_LYRIA_TRACK_PROMPTS_TOOL, SET_LYRIA_QUANTIZE_TOOL]
      : undefined;

    let currentContents = [...contents];
    let accumulatedText = '';

    // Multi-turn tool call loop
    for (let round = 0; round < MAX_TOOL_ROUNDS; round++) {
      if (signal?.aborted) return { content: accumulatedText };

      // Use tool declarations on all rounds (model may want to call multiple tools)
      const textBefore = accumulatedText;
      const response = await this.executeStreamRequest(
        model, apiKey, systemPrompt, currentContents,
        tools,
        (delta, accumulated) => {
          accumulatedText = textBefore + accumulated;
          callbacks.onChunk(delta, accumulatedText);
        },
        callbacks.onThought,
        generationConfig, signal
      );

      console.log(`[Gemini] Round ${round} result:`, {
        contentLength: response.content?.length ?? 0,
        hasFunctionCall: !!response.functionCall,
        functionCallName: response.functionCall?.name,
        modelPartsCount: response.modelParts?.length ?? 0,
        error: response.error,
        accumulatedText: accumulatedText.slice(0, 100),
      });

      if (response.error) {
        return { content: accumulatedText, error: response.error };
      }

      // Merge any text from this response
      accumulatedText = response.content || accumulatedText;

      // No function call → we're done
      if (!response.functionCall || !onToolCall) {
        return { content: accumulatedText };
      }

      // Execute function call
      const fc = response.functionCall;
      console.log(`[Gemini] Function call (round ${round + 1}): ${fc.name}`, fc.args);

      // Notify UI about the function call
      callbacks.onFunctionCall?.(fc.name, fc.args);

      // Yield to browser before executing tool
      await this.yieldToBrowser();

      const toolResult = await onToolCall({ name: fc.name, args: fc.args });

      if (signal?.aborted) return { content: accumulatedText };

      // Yield after tool execution
      await this.yieldToBrowser();

      // Build follow-up contents with tool result
      currentContents = [
        ...currentContents,
        {
          role: 'model',
          parts: response.modelParts && response.modelParts.length > 0
            ? response.modelParts
            : [{ functionCall: fc }]
        },
        {
          role: 'user',
          parts: [{
            functionResponse: {
              name: fc.name,
              response: { result: toolResult || {} },
            }
          }]
        }
      ];

      // Reset accumulated text for follow-up — text from previous rounds is already streamed
      // The onChunk wrapper will continue from the current accumulated position
      const prevAccumulated = accumulatedText;
      const wrappedCallbacks: StreamCallbacks = {
        onChunk: (delta, _accumulated) => {
          accumulatedText = prevAccumulated + (_accumulated);
          callbacks.onChunk(delta, accumulatedText);
        },
        onThought: callbacks.onThought,
        onFunctionCall: callbacks.onFunctionCall,
      };

      // For the last round, if we still get a function call, we ignore it
      if (round === MAX_TOOL_ROUNDS - 1) {
        // Final round — stream response without tools to force text
        const finalResponse = await this.executeStreamRequest(
          model, apiKey, systemPrompt, currentContents, undefined,
          wrappedCallbacks.onChunk,
          wrappedCallbacks.onThought,
          generationConfig, signal
        );
        accumulatedText = prevAccumulated + (finalResponse.content || '');
        return { content: accumulatedText, error: finalResponse.error };
      }

      // Next iteration will send the follow-up and check for more tool calls
      // Update the onChunk wrapper for the next streaming call
      // We need to rebuild contents for the next executeStreamRequest call
      // The loop will handle this on the next iteration
    }

    return { content: accumulatedText };
  }

  private static async executeStreamRequest(
    model: string,
    apiKey: string,
    systemPrompt: string,
    contents: unknown[],
    tools: unknown[] | undefined,
    onChunk: (delta: string, accumulated: string) => void,
    onThought: ((delta: string, accumulated: string) => void) | undefined,
    generationConfig: Record<string, unknown>,
    signal?: AbortSignal,
  ): Promise<{ content: string; functionCall?: { name: string; args: Record<string, unknown> }, modelParts?: Record<string, unknown>[], error?: string }> {
    try {
      const url = `${BASE}/${model}:streamGenerateContent?alt=sse&key=${apiKey}`;
      const response = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Accept': 'text/event-stream',
        },
        body: JSON.stringify({
          systemInstruction: { parts: [{ text: systemPrompt }] },
          contents,
          tools,
          generationConfig,
        }),
        signal,
      });

      console.log(`[Gemini] ${model} (stream): ${response.status}`);

      if (!response.ok) {
        const errorText = await response.text().catch(() => '');
        try {
          const errorData = JSON.parse(errorText);
          return { content: '', error: errorData.error?.message || `API Error: ${response.status}` };
        } catch {
          return { content: '', error: `API Error: ${response.status} - ${errorText.slice(0, 200)}` };
        }
      }

      const reader = response.body?.getReader();
      if (!reader) return { content: '', error: 'No response body' };

      const decoder = new TextDecoder();
      let accumulated = '';
      let thoughtAccumulated = '';
      let buffer = '';
      let functionCall: { name: string; args: Record<string, unknown> } | undefined;
      const modelParts: Record<string, unknown>[] = [];

      while (true) {
        if (signal?.aborted) {
          reader.cancel();
          return { content: accumulated, functionCall, modelParts };
        }

        const { done, value } = await reader.read();
        if (done) break;

        buffer += decoder.decode(value, { stream: true });
        const lines = buffer.split('\n');
        buffer = lines.pop() || '';

        for (const line of lines) {
          if (!line.startsWith('data: ')) continue;
          const jsonStr = line.slice(6).trim();
          if (!jsonStr || jsonStr === '[DONE]') continue;

          try {
            const parsed = JSON.parse(jsonStr);
            const candidate = parsed.candidates?.[0];
            const parts = candidate?.content?.parts;
            if (parts) {
              for (const part of parts) {
                // Always preserve every part as-is for thought signatures.
                // Gemini 3 docs: "Don't concatenate parts with signatures together."
                modelParts.push({ ...part });

                // Gemini returns thought summaries as { text: "...", thought: true }
                // Check for thought flag FIRST — route to onThought, not onChunk
                if (part.thought === true && 'text' in part && (part.text as string).length > 0) {
                  const thoughtText = part.text as string;
                  thoughtAccumulated += thoughtText;
                  onThought?.(thoughtText, thoughtAccumulated);
                } else if ('text' in part && (part.text as string).length > 0) {
                  accumulated += part.text as string;
                  onChunk(part.text as string, accumulated);
                } else if ('functionCall' in part) {
                  functionCall = part.functionCall as { name: string; args: Record<string, unknown> };
                }
              }
            }
          } catch {
            // skip malformed
          }
        }

        // Yield to browser on every chunk to keep UI responsive
        await this.yieldToBrowser();
      }

      console.log(`[Gemini] ${model} stream complete:`, {
        textLength: accumulated.length,
        thoughtLength: thoughtAccumulated.length,
        hasFunctionCall: !!functionCall,
        functionCallName: functionCall?.name,
        partsCount: modelParts.length,
        partTypes: modelParts.map(p => Object.keys(p).join(',')),
      });

      return { content: accumulated, functionCall, modelParts };
    } catch (e) {
      if (e instanceof DOMException && e.name === 'AbortError') {
        console.log(`[Gemini] ${model} stream aborted by user`);
        return { content: '' };
      }
      console.error(`[Gemini] ${model} stream error:`, e);
      return { content: '', error: e instanceof Error ? e.message : 'Unknown network error' };
    }
  }

  private static yieldToBrowser(): Promise<void> {
    return new Promise<void>(resolve => {
      requestAnimationFrame(() => setTimeout(resolve, 0));
    });
  }
}
