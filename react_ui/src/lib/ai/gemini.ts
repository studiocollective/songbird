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

const MODELS = [
  'gemini-3-flash-preview',
  'gemini-3.1-pro-preview',
];

const BASE = 'https://generativelanguage.googleapis.com/v1beta/models';

export class GeminiService {
  /**
   * Send a message to Gemini with function calling support.
   *
   * Flow:
   * 1. Non-streaming request with tool declarations
   * 2. If model returns functionCall → execute via onToolCall, send result back
   * 3. Stream the final text response
   */
  static async streamMessage(
    apiKey: string,
    history: ChatMessage[],
    systemPrompt: string,
    onChunk: (delta: string, accumulated: string) => void,
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

      const result = await this.tryWithTools(model, apiKey, systemPrompt, contents, onChunk, onToolCall, signal);

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
    onChunk: (delta: string, accumulated: string) => void,
    onToolCall?: (call: ToolCall) => Promise<Record<string, unknown>>,
    signal?: AbortSignal,
  ): Promise<GeminiResponse> {
    const generationConfig = {
      temperature: 0.7,
      topK: 40,
      topP: 0.95,
      maxOutputTokens: 4096,
    };

    // Step 1: Streaming call with tool declarations
    const initialResponse = await this.executeStreamRequest(
      model, apiKey, systemPrompt, contents, onToolCall ? [BIRD_TOOL, VALIDATE_TOOL] : undefined, onChunk, generationConfig, signal
    );

    if (initialResponse.error) {
      return { content: initialResponse.content, error: initialResponse.error };
    }

    // Step 2: Handle function call if present
    if (initialResponse.functionCall && onToolCall) {
      const fc = initialResponse.functionCall;
      console.log(`[Gemini] Function call: ${fc.name}`, fc.args);

      const toolResult = await onToolCall({ name: fc.name, args: fc.args });

      if (signal?.aborted) {
        return { content: initialResponse.content };
      }

      // Step 3: Stream follow-up after tool call
      const followUpContents = [
        ...contents,
        {
          role: 'model',
          parts: initialResponse.modelParts && initialResponse.modelParts.length > 0 ? initialResponse.modelParts : [{ functionCall: fc }]
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

      // Pass a wrapped onChunk to append to whatever text might have already been generated
      let followUpAccumulated = initialResponse.content;
      const followUpResponse = await this.executeStreamRequest(
        model, apiKey, systemPrompt, followUpContents, undefined,
        (delta) => {
          followUpAccumulated += delta;
          onChunk(delta, followUpAccumulated);
        },
        generationConfig, signal
      );

      return { content: followUpAccumulated, error: followUpResponse.error };
    }

    return { content: initialResponse.content };
  }

  private static async executeStreamRequest(
    model: string,
    apiKey: string,
    systemPrompt: string,
    contents: unknown[],
    tools: unknown[] | undefined,
    onChunk: (delta: string, accumulated: string) => void,
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

      const yieldToBrowser = () => new Promise<void>(resolve => {
        requestAnimationFrame(() => setTimeout(resolve, 0));
      });

      const decoder = new TextDecoder();
      let accumulated = '';
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

        let hadText = false;
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
                if ('text' in part) {
                  accumulated += part.text as string;
                  onChunk(part.text as string, accumulated);
                  hadText = true;

                  const lastPart = modelParts[modelParts.length - 1];
                  if (lastPart && 'text' in lastPart) {
                    lastPart.text = (lastPart.text as string) + part.text;
                    // Merge any other keys that might have appeared with text
                    for (const k of Object.keys(part)) {
                      if (k !== 'text') lastPart[k] = part[k as keyof typeof part];
                    }
                  } else {
                    modelParts.push({ ...part });
                  }
                } else if ('functionCall' in part) {
                  functionCall = part.functionCall as { name: string; args: Record<string, unknown> };
                  const existing = modelParts.find(p => 'functionCall' in p && (p.functionCall as { name: string }).name === functionCall?.name);
                  
                  if (existing) {
                    Object.assign(existing, part);
                  } else {
                    modelParts.push({ ...part });
                  }
                } else if ('thought' in part) {
                  const lastPart = modelParts[modelParts.length - 1];
                  if (lastPart && 'thought' in lastPart) {
                    lastPart.thought = (lastPart.thought as string) + part.thought;
                  } else {
                    modelParts.push({ ...part });
                  }
                } else {
                  modelParts.push({ ...part });
                }
              }
            }
          } catch {
            // skip malformed
          }
        }

        if (hadText) await yieldToBrowser();
      }

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
}
