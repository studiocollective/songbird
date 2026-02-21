import { useState, useEffect, useRef } from 'react';
import { cn } from '@/lib/utils';
import { useChatStore } from '@/data/store';
import { buildSystemPrompt } from '@/lib/ai/prompts';
import { GeminiService } from '@/lib/ai/gemini';
import { Juce } from '@/lib';
import { MarkdownRenderer } from './molecules/MarkdownRenderer';
import { validateBirdSyntax } from '@/lib/ai/validator';

export function ChatPanel() {
  const {
    chatOpen, chatMessages, chatInput, apiKey, selectedModel,
    isThinking, isStreaming, toolUseLabel,
    setChatInput, setApiKey, setSelectedModel,
    addMessage, updateLastMessage,
    setThinking, setStreaming, setToolUseLabel,
  } = useChatStore();
  const [tempKey, setTempKey] = useState('');
  const [loadingKey, setLoadingKey] = useState(true);
  const [messageQueue, setMessageQueue] = useState<string[]>([]);
  const messagesEndRef = useRef<HTMLDivElement>(null);
  const abortRef = useRef<AbortController | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  const isActive = isThinking || isStreaming;

  const scrollToBottom = () => {
    messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  };

  useEffect(() => {
    scrollToBottom();
  }, [chatMessages, isThinking, isStreaming]);

  // Load API key from C++ ApplicationProperties on mount
  useEffect(() => {
    const loadKey = async () => {
      try {
        console.log('[Chat] Calling getApiKey()...');
        const key = await Juce.getNativeFunction('getApiKey')();
        console.log('[Chat] getApiKey returned:', key ? `String(length=${key.length})` : 'empty/null');
        
        if (key && typeof key === 'string' && key.length > 0) {
          setApiKey(key);
          console.log('[Chat] API key loaded from application settings');
        } else {
          console.log('[Chat] No API key found, should prompt user.');
        }
      } catch (e) {
        console.warn('[Chat] Failed to load API key:', e);
      } finally {
        setLoadingKey(false);
      }
    };
    loadKey();
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const handleSetKey = async () => {
    if (tempKey.trim()) {
      const key = tempKey.trim();
      setApiKey(key);
      // Persist to C++ ApplicationProperties
      try {
        await Juce.getNativeFunction('setApiKey')(key);
        console.log('[Chat] API key saved to application settings');
      } catch (e) {
        console.warn('[Chat] Failed to save API key:', e);
      }
    }
  };

  const handleStop = () => {
    abortRef.current?.abort();
  };

  const handleSend = async (messageOverride?: string) => {
    const text = (messageOverride ?? chatInput).trim();
    if (!text || !apiKey) return;

    if (isActive) {
      // Queue message for after the current response finishes
      setMessageQueue(q => [...q, text]);
      setChatInput('');
      return;
    }
    
    addMessage('user', text);
    setChatInput('');
    setThinking(true);
    setToolUseLabel(null);

    // Create abort controller for this request
    const controller = new AbortController();
    abortRef.current = controller;

    let hasReceivedText = false;

    try {
      // Read the current bird file so Gemini can see/edit it
      let currentBird = '';
      try {
        currentBird = await Juce.getNativeFunction('readBird')();
      } catch (e) {
        console.warn('[Chat] Failed to read bird file:', e);
      }

      const result = await GeminiService.streamMessage(
        apiKey,
        // Pass current history (without any not-yet-added assistant message)
        useChatStore.getState().chatMessages,
        buildSystemPrompt(currentBird),
        (_delta, accumulated) => {
          if (!hasReceivedText) {
            // First chunk arrives — switch from thinking to streaming, add assistant message
            hasReceivedText = true;
            setThinking(false);
            setStreaming(true);
            addMessage('assistant', accumulated);
          } else {
            updateLastMessage(accumulated);
          }
        },
        selectedModel,
        // Tool call handler: Gemini calls update_bird_file → we save via C++
        async (call) => {
          if (call.name === 'update_bird_file') {
            const content = call.args.content as string;
            // Validate before writing — invalid content must never trigger a C++ reload
            const validation = validateBirdSyntax(content);
            if (!validation.isValid) {
              console.warn('[Chat] update_bird_file blocked: invalid syntax —', validation.error);
              setToolUseLabel(null);
              return { success: false, error: `Bird syntax error: ${validation.error}. Please fix and try again.` };
            }
            setToolUseLabel('Updating bird file…');
            console.log('[Chat] Tool call: update_bird_file, saving...');
            await Juce.getNativeFunction('updateBird')(content);
            return { success: true };
          } else if (call.name === 'validate_bird_file') {
            setToolUseLabel('Validating grammar…');
            const content = call.args.content as string;
            const result = validateBirdSyntax(content);
            console.log(`[Chat] Tool call: validate_bird_file -> ${result.isValid ? 'OK' : result.error}`);
            return result;
          } else if (call.name === 'get_plugin_params') {
            const trackId = call.args.trackId as number;
            setToolUseLabel(`Reading plugin params…`);
            console.log(`[Chat] Tool call: get_plugin_params(trackId=${trackId})`);
            const raw = await Juce.getNativeFunction('getPluginParams')(trackId);
            try {
              return JSON.parse(raw as string);
            } catch {
              return { error: 'Failed to parse plugin params' };
            }
          } else if (call.name === 'set_plugin_param') {
            const { trackId, paramName, value } = call.args as { trackId: number; paramName: string; value: number };
            setToolUseLabel(`Setting ${paramName}…`);
            console.log(`[Chat] Tool call: set_plugin_param(track=${trackId}, param="${paramName}", value=${value})`);
            const raw = await Juce.getNativeFunction('setPluginParam')(trackId, paramName, value);
            try {
              return JSON.parse(raw as string);
            } catch {
              return { success: false, error: 'Failed to parse response' };
            }
          } else if (call.name === 'set_track_mixer') {
            const { trackId, volumeDb, pan, mute, solo } = call.args as {
              trackId: number; volumeDb: number; pan: number; mute: boolean; solo: boolean;
            };
            setToolUseLabel(`Adjusting mix…`);
            console.log(`[Chat] Tool call: set_track_mixer(track=${trackId}, vol=${volumeDb}dB, pan=${pan}, mute=${mute}, solo=${solo})`);
            const raw = await Juce.getNativeFunction('setTrackMixer')(trackId, volumeDb, pan, mute, solo);
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          } else if (call.name === 'set_bpm') {
            const { bpm } = call.args as { bpm: number };
            setToolUseLabel(`Setting BPM to ${bpm}…`);
            console.log(`[Chat] Tool call: set_bpm(${bpm})`);
            const raw = await Juce.getNativeFunction('setBpm')(bpm);
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          } else if (call.name === 'set_lyria_track_config') {
            const { trackId, config } = call.args as { trackId: number; config: object };
            setToolUseLabel(`Configuring Lyria track ${trackId}…`);
            console.log(`[Chat] Tool call: set_lyria_track_config(track=${trackId})`);
            const raw = await Juce.getNativeFunction('setLyriaTrackConfig')(trackId, JSON.stringify(config));
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          } else if (call.name === 'set_lyria_track_prompts') {
            const { trackId, prompts } = call.args as { trackId: number; prompts: object[] };
            setToolUseLabel(`Setting Lyria prompts for track ${trackId}…`);
            console.log(`[Chat] Tool call: set_lyria_track_prompts(track=${trackId}, count=${prompts.length})`);
            const raw = await Juce.getNativeFunction('setLyriaTrackPrompts')(trackId, JSON.stringify(prompts));
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          } else if (call.name === 'set_lyria_quantize') {
            const { trackId, bars } = call.args as { trackId: number; bars: number };
            setToolUseLabel(`Setting Lyria quantization for track ${trackId} to ${bars} bar(s)…`);
            console.log(`[Chat] Tool call: set_lyria_quantize(track=${trackId}, bars=${bars})`);
            const raw = await Juce.getNativeFunction('setLyriaQuantize')(trackId, bars);
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          }
          return { error: 'Unknown tool' };
        },
        controller.signal,
      );

      // Handle error response
      if (result.error) {
        if (!hasReceivedText) {
          addMessage('assistant', `⚠️ Error: ${result.error}`);
        } else {
          const current = useChatStore.getState().chatMessages;
          const lastContent = current[current.length - 1]?.content || '';
          updateLastMessage(lastContent + `\n\n⚠️ Error: ${result.error}`);
        }
      }

      // Handle case where no text was received and no error (e.g. abort with no content)
      if (!hasReceivedText && !result.error && controller.signal.aborted) {
        // User stopped before any text — nothing to show
      }
    } catch (e) {
      console.error(e);
      if (!hasReceivedText) {
        addMessage('assistant', '⚠️ Failed to connect to Gemini.');
      } else {
        updateLastMessage(
          (useChatStore.getState().chatMessages.at(-1)?.content || '') +
          '\n\n⚠️ Failed to connect to Gemini.'
        );
      }
    } finally {
      setThinking(false);
      setStreaming(false);
      setToolUseLabel(null);
      abortRef.current = null;

      // Drain the queue — send the next message if one was queued
      setMessageQueue(q => {
        if (q.length > 0) {
          const [next, ...rest] = q;
          setTimeout(() => handleSend(next), 0);
          return rest;
        }
        return q;
      });
    }
  };

  if (loadingKey) {
    return (
      <div className={cn(panel, chatOpen ? 'w-80' : 'w-0')}>
        <div style={{ padding: '2rem', textAlign: 'center', color: 'var(--text-secondary)' }}>
          Loading...
        </div>
      </div>
    );
  }

  if (!apiKey) {
    return (
      <div className={cn(panel, chatOpen ? 'w-80' : 'w-0')}>
        <div className={panelInner}>
          <div className={header}>
             <div className={statusDot} />
             <span className={headerTitle}>Songbird Copilot</span>
          </div>
          <div className="flex-1 flex flex-col items-center justify-center p-6 text-center space-y-4">
             <div className="text-4xl">🔑</div>
             <h3 className="text-sm font-medium text-[hsl(var(--foreground))]">Enter Gemini API Key</h3>
             <p className="text-xs text-[hsl(var(--muted-foreground))]">
               To use the AI features, please provide a valid Google Gemini API key.
             </p>
             <input 
               type="password" 
               className={inputField + " w-full"} 
               placeholder="AIzaSy..." 
               value={tempKey}
               onChange={(e) => setTempKey(e.target.value)}
             />
             <button onClick={handleSetKey} className={sendBtn + " w-full"}>
               Save Key
             </button>
             <p className="text-[10px] text-[hsl(var(--muted-foreground))] opacity-50">
               Stored locally in your browser.
             </p>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className={cn(panel, chatOpen ? 'w-80' : 'w-0')}>
      <div className={panelInner}>
        {/* Header */}
        <div className={header}>
          <div className={statusDot} />
          <span className={headerTitle}>Songbird Copilot</span>
          <select
            value={selectedModel}
            onChange={(e) => setSelectedModel(e.target.value)}
            className={modelSelect}
          >
            <option value="gemini-3-flash-preview">Flash</option>
            <option value="gemini-3.1-pro-preview">Pro</option>
          </select>
        </div>

        {/* Messages */}
        <div className={messagesScroll}>
          {chatMessages.length === 0 && !isThinking && (
            <div className={welcomeWrapper}>
              <div className={welcomeEmoji}>🐦</div>
              <p className={welcomeText}>
                Describe the music you want to create.
                <br />
                I'll generate bird notation for you.
              </p>
              <div className={suggestionsWrapper}>
                {SUGGESTIONS.map((suggestion) => (
                  <button
                    key={suggestion}
                    onClick={() => setChatInput(suggestion)}
                    className={suggestionBtn}
                  >
                    "{suggestion}"
                  </button>
                ))}
              </div>
            </div>
          )}

          {chatMessages.map((msg, i) => (
            <div key={i} className={msg.role === 'user' ? userOuter : assistantOuter}>
              <div className={cn(bubble, msg.role === 'user' ? userBubble : assistantBubble)}>
                <MarkdownRenderer content={msg.content} />
              </div>
            </div>
          ))}

          {/* Tool use indicator */}
          {toolUseLabel && (
            <div className={assistantOuter}>
              <div className={toolIndicator}>
                <span className={toolIcon}>🔧</span>
                <span>{toolUseLabel}</span>
              </div>
            </div>
          )}

          {/* Thinking indicator — only while waiting for first text */}
          {isThinking && (
            <div className={assistantOuter}>
              <div className={thinkingContainer}>
                <span className={thinkingIcon}>✨</span>
                <span className={thinkingText}>Thinking</span>
                <span className={thinkingDots}>
                  <span className={dot1}>.</span>
                  <span className={dot2}>.</span>
                  <span className={dot3}>.</span>
                </span>
              </div>
            </div>
          )}

          <div ref={messagesEndRef} />
        </div>

        {/* Input */}
        <div className={inputWrapper}>
          {messageQueue.length > 0 && (
            <div className={queueBadge}>
              <span>⏳</span>
              <span>{messageQueue.length} message{messageQueue.length > 1 ? 's' : ''} queued</span>
            </div>
          )}
          <div className={inputRow}>
            <textarea
              ref={textareaRef}
              value={chatInput}
              onChange={(e) => setChatInput(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter' && !e.shiftKey) {
                  e.preventDefault();
                  handleSend();
                }
              }}
              placeholder={isActive ? 'Type to queue next message…' : 'Describe your music…'}
              className={inputField}
              rows={1}
            />
            {isActive ? (
              <button onClick={handleStop} className={stopBtn} title="Stop generation">
                ■
              </button>
            ) : (
              <button onClick={() => handleSend()} className={sendBtn} disabled={!chatInput.trim()}>
                Send
              </button>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

const SUGGESTIONS = [
  'moody lo-fi jazz beat',
  'driving techno bassline',
  'ambient pad with reverb',
];

// --- Panel ---
const panel = `
  bg-[hsl(var(--background))] border-l border-[hsl(var(--border))]
  transition-all duration-300 ease-in-out overflow-hidden flex flex-col`;
const panelInner = `w-80 h-full flex flex-col`;

// --- Header ---
const header = `h-10 shrink-0 border-b border-[hsl(var(--border))] flex items-center px-3`;
const statusDot = `w-2 h-2 rounded-full bg-[hsl(var(--progress))] mr-2`;
const headerTitle = `text-xs font-medium text-[hsl(var(--foreground))]`;
const modelSelect = `
  ml-auto text-[10px] bg-transparent text-[hsl(var(--muted-foreground))]
  border border-[hsl(var(--border))] rounded px-1.5 py-0.5
  cursor-pointer outline-none
  hover:text-[hsl(var(--foreground))] hover:border-[hsl(var(--muted-foreground))]
  transition-colors`;

// --- Messages ---
const messagesScroll = `flex-1 overflow-y-auto p-3 space-y-3`;

const welcomeWrapper = `text-center py-8`;
const welcomeEmoji = `text-2xl mb-2`;
const welcomeText = `text-xs text-[hsl(var(--muted-foreground))] leading-relaxed`;
const suggestionsWrapper = `mt-4 space-y-1.5`;
const suggestionBtn = `
  block w-full text-left px-3 py-1.5 rounded
  bg-[hsl(var(--card))]/50 hover:bg-[hsl(var(--card))]
  text-[11px] text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  transition-colors`;

const userOuter = `ml-6`;
const assistantOuter = `mr-2`;
const bubble = `rounded-lg px-3 py-2 text-xs leading-relaxed`;
const userBubble = `
  bg-[hsl(var(--chat-user))] text-[hsl(var(--foreground))]
  border border-[hsl(var(--chat-user-border))]`;
const assistantBubble = `
  bg-[hsl(var(--chat-assistant))] text-[hsl(var(--foreground))]
  border border-[hsl(var(--chat-assistant-border))]`;

// --- Thinking indicator ---
const thinkingContainer = `
  inline-flex items-center gap-1.5 px-3 py-1.5
  text-[11px] text-[hsl(var(--muted-foreground))]
  bg-[hsl(var(--chat-assistant))] border border-[hsl(var(--chat-assistant-border))]
  rounded-lg`;
const thinkingIcon = `text-sm animate-pulse`;
const thinkingText = `font-medium`;
const thinkingDots = `inline-flex`;
const dot1 = `animate-bounce [animation-delay:0ms]`;
const dot2 = `animate-bounce [animation-delay:150ms]`;
const dot3 = `animate-bounce [animation-delay:300ms]`;

// --- Tool use indicator ---
const toolIndicator = `
  inline-flex items-center gap-1.5 px-3 py-1.5
  text-[11px] text-[hsl(var(--muted-foreground))]
  italic`;
const toolIcon = `text-sm`;

// --- Input ---
const inputWrapper = `shrink-0 border-t border-[hsl(var(--border))] p-2 space-y-1.5`;
const queueBadge = `
  flex items-center gap-1.5 px-2 py-1 rounded
  bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  text-[10px] text-[hsl(var(--muted-foreground))] italic`;
const inputRow = `flex gap-2 items-end`;
const inputField = `
  flex-1 min-h-8 max-h-32 bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  rounded-md px-3 py-1.5 text-xs text-[hsl(var(--foreground))]
  placeholder-[hsl(var(--muted-foreground))]
  focus:outline-none focus:border-[hsl(var(--ring))]
  resize-none overflow-y-auto leading-relaxed`;
const sendBtn = `
  h-8 px-3 rounded-md bg-[hsl(var(--progress))] hover:bg-[hsl(var(--progress))]/80
  text-xs text-[hsl(var(--primary-foreground))] font-medium transition-colors
  disabled:opacity-50 disabled:cursor-not-allowed shrink-0`;
const stopBtn = `
  h-8 w-8 rounded-md bg-[hsl(var(--destructive,0_84%_60%))]
  hover:bg-[hsl(var(--destructive,0_84%_60%))]/80
  text-xs text-white font-bold transition-colors
  flex items-center justify-center shrink-0`;
