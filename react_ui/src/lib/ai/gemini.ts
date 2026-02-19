import type { ChatMessage } from '@/data/slices';

export interface GeminiResponse {
  content: string;
  error?: string;
  model?: string;
}

const MODELS = [
  'gemini-3-flash-preview',
  'gemini-3-pro-preview',
];

const BASE = 'https://generativelanguage.googleapis.com/v1beta/models';

export class GeminiService {
  /**
   * Stream a response from Gemini using fetch + ReadableStream.
   * Uses preferredModel first, falls back to the other on 429/503.
   */
  static async streamMessage(
    apiKey: string,
    history: ChatMessage[],
    systemPrompt: string,
    onChunk: (delta: string, accumulated: string) => void,
    preferredModel: string = 'gemini-3-flash-preview'
  ): Promise<GeminiResponse> {
    const contents = history.map(msg => ({
      role: msg.role === 'user' ? 'user' : 'model',
      parts: [{ text: msg.content }]
    }));

    // Order models: preferred first, then fallbacks
    const models = [preferredModel, ...MODELS.filter(m => m !== preferredModel)];
    const body = JSON.stringify({
      systemInstruction: { parts: [{ text: systemPrompt }] },
      contents,
      generationConfig: {
        temperature: 0.7,
        topK: 40,
        topP: 0.95,
        maxOutputTokens: 2048,
      },
    });

    for (const model of models) {
      const result = await this.tryStream(model, apiKey, body, onChunk);

      // If rate-limited or overloaded, try next model
      if (result.error && (result.error.includes('high demand') || result.error.includes('429') || result.error.includes('503'))) {
        console.log(`[Gemini] ${model} unavailable, trying fallback...`);
        continue;
      }

      return { ...result, model };
    }

    return { content: '', error: 'All models are currently overloaded. Please try again later.' };
  }

  private static async tryStream(
    model: string,
    apiKey: string,
    body: string,
    onChunk: (delta: string, accumulated: string) => void
  ): Promise<GeminiResponse> {
    try {
      const url = `${BASE}/${model}:streamGenerateContent?alt=sse&key=${apiKey}`;
      const response = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Accept': 'text/event-stream',
        },
        body,
      });

      console.log(`[Gemini] ${model}: ${response.status}`);

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
      console.error(`[Gemini] ${model} error:`, e);
      return { content: '', error: e instanceof Error ? e.message : 'Unknown network error' };
    }
  }
}
