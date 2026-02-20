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
  ): Promise<GeminiResponse> {
    const contents = history.map(msg => ({
      role: msg.role === 'user' ? 'user' : 'model',
      parts: [{ text: msg.content }]
    }));

    const models = [preferredModel, ...MODELS.filter(m => m !== preferredModel)];

    for (const model of models) {
      const result = await this.tryWithTools(model, apiKey, systemPrompt, contents, onChunk, onToolCall);

      if (result.error && (result.error.includes('high demand') || result.error.includes('429') || result.error.includes('503'))) {
        console.log(`[Gemini] ${model} unavailable, trying fallback...`);
        continue;
      }

      return { ...result, model };
    }

    return { content: '', error: 'All models are currently overloaded. Please try again later.' };
  }

  /**
   * Step 1: Non-streaming call with tools.
   * If the model returns a functionCall, execute it and make a follow-up call.
   * Otherwise, or for the final response, stream the text.
   */
  private static async tryWithTools(
    model: string,
    apiKey: string,
    systemPrompt: string,
    contents: Array<{ role: string; parts: Array<Record<string, unknown>> }>,
    onChunk: (delta: string, accumulated: string) => void,
    onToolCall?: (call: ToolCall) => Promise<Record<string, unknown>>,
  ): Promise<GeminiResponse> {
    const generationConfig = {
      temperature: 0.7,
      topK: 40,
      topP: 0.95,
      maxOutputTokens: 4096,
    };

    try {
      // Step 1: Non-streaming call with tool declarations
      const url = `${BASE}/${model}:generateContent?key=${apiKey}`;
      const body = JSON.stringify({
        systemInstruction: { parts: [{ text: systemPrompt }] },
        contents,
        tools: onToolCall ? [BIRD_TOOL] : undefined,
        generationConfig,
      });

      const response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body,
      });

      console.log(`[Gemini] ${model} (tool call): ${response.status}`);

      if (!response.ok) {
        const errorText = await response.text().catch(() => '');
        try {
          const errorData = JSON.parse(errorText);
          return { content: '', error: errorData.error?.message || `API Error: ${response.status}` };
        } catch {
          return { content: '', error: `API Error: ${response.status} - ${errorText.slice(0, 200)}` };
        }
      }

      const data = await response.json();
      const candidate = data.candidates?.[0];
      if (!candidate) return { content: '', error: 'No response candidate' };

      // Check if the model wants to call a function
      const functionCallPart = candidate.content?.parts?.find(
        (p: Record<string, unknown>) => p.functionCall
      );

      if (functionCallPart && onToolCall) {
        const fc = functionCallPart.functionCall as { name: string; args: Record<string, unknown> };
        console.log(`[Gemini] Function call: ${fc.name}`, fc.args);

        // Execute the tool
        const toolResult = await onToolCall({ name: fc.name, args: fc.args });

        // Step 2: Send function result back and stream the final response
        const followUpContents = [
          ...contents,
          candidate.content,  // model's functionCall turn
          {
            role: 'user',
            parts: [{
              functionResponse: {
                name: fc.name,
                response: { result: toolResult },
              }
            }]
          }
        ];

        return await this.streamResponse(model, apiKey, systemPrompt, followUpContents, onChunk, generationConfig);
      }

      // No function call — just extract text directly
      const textParts = candidate.content?.parts
        ?.filter((p: Record<string, unknown>) => p.text)
        ?.map((p: { text: string }) => p.text)
        .join('') || '';

      if (textParts) {
        onChunk(textParts, textParts);
      }

      return { content: textParts };
    } catch (e) {
      console.error(`[Gemini] ${model} error:`, e);
      return { content: '', error: e instanceof Error ? e.message : 'Unknown network error' };
    }
  }

  /**
   * Stream a response using SSE (used for the final text response after function calling).
   */
  private static async streamResponse(
    model: string,
    apiKey: string,
    systemPrompt: string,
    contents: unknown[],
    onChunk: (delta: string, accumulated: string) => void,
    generationConfig: Record<string, unknown>,
  ): Promise<GeminiResponse> {
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
          generationConfig,
        }),
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

      while (true) {
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
            const text = parsed.candidates?.[0]?.content?.parts?.[0]?.text;
            if (text) {
              accumulated += text;
              onChunk(text, accumulated);
              hadText = true;
            }
          } catch {
            // skip malformed
          }
        }

        if (hadText) await yieldToBrowser();
      }

      return { content: accumulated };
    } catch (e) {
      console.error(`[Gemini] ${model} stream error:`, e);
      return { content: '', error: e instanceof Error ? e.message : 'Unknown network error' };
    }
  }
}
