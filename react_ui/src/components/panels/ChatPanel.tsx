import { useState, useEffect, useRef } from 'react';
import { cn } from '@/lib/utils';
import { useChatStore } from '@/data/store';
import { buildSystemPrompt } from '@/lib/ai/prompts';
import { GeminiService } from '@/lib/ai/gemini';
import { Juce } from '@/lib';
import { MarkdownRenderer } from '../molecules/MarkdownRenderer';
import { validateBirdSyntax } from '@/lib/ai/validator';
import songbirdIcon from '@/assets/songbird.svg';

export function ChatPanel() {
  const {
    chatMessages, chatInput, apiKey, selectedModel,
    isThinking, isStreaming, thinkingText, toolUseLabel,
    activeThreadId, threads, threadMenuOpen,
    setChatInput, setApiKey, setSelectedModel,
    addMessage, updateLastMessage, setLastMessageThinking,
    setThinking, setStreaming, setThinkingText, setToolUseLabel,
    newThread, switchThread, deleteThread, toggleThreadMenu,
    persistCurrentThread, getRecentSummaries, setThreadSummary,
    setThreadTitle,
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

  const handleNewThread = async () => {
    // Auto-summarize the old thread before creating new one
    const oldThread = threads.find(t => t.id === activeThreadId);
    if (oldThread && oldThread.messages.length > 0 && !oldThread.summary && apiKey) {
      // Fire and forget — don't block the UI
      const msgs = oldThread.messages.map(m => `${m.role}: ${m.content}`).join('\n').slice(0, 2000);
      const threadId = oldThread.id;
      GeminiService.streamMessage(
        apiKey,
        [{ role: 'user', content: `Summarize this conversation in 1-2 sentences (max 100 words). Focus on what was accomplished:\n\n${msgs}` }],
        'You are a summarizer. Output only the summary, nothing else.',
        { onChunk: () => {} },
        selectedModel,
      ).then(result => {
        if (result.content) {
          setThreadSummary(threadId, result.content.trim());
        }
      }).catch(() => {});
    }
    newThread();
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

      // Human-readable labels for tool calls
      const TOOL_LABELS: Record<string, string> = {
        update_bird_file: 'Updating bird file…',
        validate_bird_file: 'Validating grammar…',
        get_plugin_params: 'Reading plugin params…',
        set_plugin_param: 'Setting plugin param…',
        set_track_mixer: 'Adjusting mix…',
        set_bpm: 'Setting BPM…',
        set_lyria_track_config: 'Configuring Lyria…',
        set_lyria_track_prompts: 'Setting Lyria prompts…',
        set_lyria_quantize: 'Quantizing Lyria…',
      };

      const result = await GeminiService.streamMessage(
        apiKey,
        useChatStore.getState().chatMessages,
        buildSystemPrompt(currentBird, getRecentSummaries()),
        {
          onChunk: (_delta, accumulated) => {
            // Clear thinking text once real text arrives
            if (!hasReceivedText) {
              hasReceivedText = true;
              setThinking(false);
              setStreaming(true);
              addMessage('assistant', accumulated);
            } else {
              updateLastMessage(accumulated);
            }
          },
          onThought: (_delta, accumulated) => {
            console.log('[Chat] Thinking:', accumulated.slice(-80));
            setThinkingText(accumulated);
          },
          onFunctionCall: (name) => {
            console.log(`[Chat] Tool call started: ${name}`);
            setToolUseLabel(TOOL_LABELS[name] || `Running ${name}…`);
          },
        },
        selectedModel,
        // Tool call handler
        async (call) => {
          if (call.name === 'update_bird_file') {
            const content = call.args.content as string;
            const validation = validateBirdSyntax(content);
            if (!validation.isValid) {
              console.warn('[Chat] update_bird_file blocked: invalid syntax —', validation.error);
              setToolUseLabel(null);
              return { success: false, error: `Bird syntax error: ${validation.error}. Please fix and try again.` };
            }
            console.log('[Chat] Tool call: update_bird_file, saving...');
            await Juce.getNativeFunction('updateBird')(content);
            return { success: true };
          } else if (call.name === 'validate_bird_file') {
            const content = call.args.content as string;
            const result = validateBirdSyntax(content);
            console.log(`[Chat] Tool call: validate_bird_file -> ${result.isValid ? 'OK' : result.error}`);
            return result;
          } else if (call.name === 'get_plugin_params') {
            const trackId = call.args.trackId as number;
            console.log(`[Chat] Tool call: get_plugin_params(trackId=${trackId})`);
            const raw = await Juce.getNativeFunction('getPluginParams')(trackId);
            try {
              return JSON.parse(raw as string);
            } catch {
              return { error: 'Failed to parse plugin params' };
            }
          } else if (call.name === 'set_plugin_param') {
            const { trackId, paramName, value } = call.args as { trackId: number; paramName: string; value: number };
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
            console.log(`[Chat] Tool call: set_track_mixer(track=${trackId}, vol=${volumeDb}dB, pan=${pan}, mute=${mute}, solo=${solo})`);
            const raw = await Juce.getNativeFunction('setTrackMixer')(trackId, volumeDb, pan, mute, solo);
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          } else if (call.name === 'set_bpm') {
            const { bpm } = call.args as { bpm: number };
            console.log(`[Chat] Tool call: set_bpm(${bpm})`);
            const raw = await Juce.getNativeFunction('setBpm')(bpm);
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          } else if (call.name === 'set_lyria_track_config') {
            const { trackId, config } = call.args as { trackId: number; config: object };
            console.log(`[Chat] Tool call: set_lyria_track_config(track=${trackId})`);
            const raw = await Juce.getNativeFunction('setLyriaTrackConfig')(trackId, JSON.stringify(config));
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          } else if (call.name === 'set_lyria_track_prompts') {
            const { trackId, prompts } = call.args as { trackId: number; prompts: object[] };
            console.log(`[Chat] Tool call: set_lyria_track_prompts(track=${trackId}, count=${prompts.length})`);
            const raw = await Juce.getNativeFunction('setLyriaTrackPrompts')(trackId, JSON.stringify(prompts));
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          } else if (call.name === 'set_lyria_quantize') {
            const { trackId, bars } = call.args as { trackId: number; bars: number };
            console.log(`[Chat] Tool call: set_lyria_quantize(track=${trackId}, bars=${bars})`);
            const raw = await Juce.getNativeFunction('setLyriaQuantize')(trackId, bars);
            try { return JSON.parse(raw as string); } catch { return { success: false }; }
          }
          return { error: 'Unknown tool' };
        },
        controller.signal,
      );

      console.log('[Chat] streamMessage finished:', {
        hasReceivedText,
        contentLength: result.content?.length ?? 0,
        error: result.error,
        aborted: controller.signal.aborted,
      });

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

      // Handle tool-only response (model used tools but produced no text)
      if (!hasReceivedText && !result.error && !controller.signal.aborted) {
        addMessage('assistant', '✅ Done.');
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
      // Save thinking text to the last assistant message before clearing
      const finalThinking = useChatStore.getState().thinkingText;
      if (finalThinking) {
        setLastMessageThinking(finalThinking);
      }

      setThinking(false);
      setStreaming(false);
      setThinkingText('');
      setToolUseLabel(null);
      abortRef.current = null;
      persistCurrentThread();

      // Auto-title the thread after the first exchange
      const currentThread = useChatStore.getState().threads.find(
        t => t.id === useChatStore.getState().activeThreadId
      );
      if (currentThread && currentThread.title === 'New Chat') {
        const firstUserMsg = useChatStore.getState().chatMessages.find(m => m.role === 'user');
        if (firstUserMsg) {
          setThreadTitle(firstUserMsg.content.slice(0, 40) + (firstUserMsg.content.length > 40 ? '…' : ''));
        }
      }

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
      <div className={cn(panel, 'w-80')}>
        <div style={{ padding: '2rem', textAlign: 'center', color: 'var(--text-secondary)' }}>
          Loading...
        </div>
      </div>
    );
  }

  if (!apiKey) {
    return (
      <div className={cn(panel, 'w-80')}>
        <div className={panelInner}>
          <div className={header}>
             <div className={statusDot} />
             <span className="text-xs font-medium text-[hsl(var(--foreground))]">Songbird Copilot</span>
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
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className={cn(panel, 'w-80')}>
      <div className={panelInner}>
        {/* Header */}
        <div className={header}>
          <div className={statusDot} />
          <button
            onClick={toggleThreadMenu}
            className={headerTitleBtn}
            title="Thread history"
          >
            {threads.find(t => t.id === activeThreadId)?.title || 'New Chat'}
            <span className="text-[8px] ml-1 opacity-50">▼</span>
          </button>
          <button
            onClick={handleNewThread}
            className={newThreadBtn}
            title="New thread"
          >
            ✚
          </button>
          <select
            value={selectedModel}
            onChange={(e) => setSelectedModel(e.target.value)}
            className={modelSelect}
          >
            <option value="gemini-3.1-pro-preview">Pro</option>
            <option value="gemini-3-flash-preview">Flash</option>
            <option value="gemini-3.1-flash-lite-preview">Flash Lite</option>
          </select>
        </div>

        {/* Thread dropdown */}
        {threadMenuOpen && (
          <div className={threadDropdown}>
            {threads.map(t => (
              <div
                key={t.id}
                className={cn(threadItem, t.id === activeThreadId && threadItemActive)}
              >
                <button
                  className={threadItemBtn}
                  onClick={() => switchThread(t.id)}
                >
                  <span className="truncate">{t.title}</span>
                  <span className={threadDate}>
                    {new Date(t.createdAt).toLocaleDateString(undefined, { month: 'short', day: 'numeric' })}
                  </span>
                </button>
                {threads.length > 1 && (
                  <button
                    className={threadDeleteBtn}
                    onClick={(e) => { e.stopPropagation(); deleteThread(t.id); }}
                    title="Delete thread"
                  >
                    ×
                  </button>
                )}
              </div>
            ))}
          </div>
        )}

        {/* Messages */}
        <div className={messagesScroll}>
          {chatMessages.length === 0 && !isThinking && (
            <div className={welcomeWrapper}>
              <div className={welcomeEmoji}><img src={songbirdIcon} alt="Songbird" width={32} height={32} /></div>
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
              {msg.role === 'user' ? (
                <div className={userBubble}>
                  <MarkdownRenderer content={msg.content} />
                </div>
              ) : (
                <>
                  {msg.thinking && (
                    <details className={persistedThinkingDetails}>
                      <summary className={persistedThinkingSummary}>
                        <span className={thinkingCaret}>▶</span>
                        <span>Thought process</span>
                      </summary>
                      <div className={persistedThinkingContent}>
                        <MarkdownRenderer content={msg.thinking} />
                      </div>
                    </details>
                  )}
                  <MarkdownRenderer content={msg.content} />
                </>
              )}
            </div>
          ))}

          {/* Thinking indicator — collapsed by default */}
          {isThinking && (
            <div className={assistantOuter}>
              {thinkingText ? (
                <details className={thinkingDetails}>
                  <summary className={thinkingSummary}>
                    <span className={thinkingIcon}>✨</span>
                    <span className={thinkingLabel}>Thinking…</span>
                  </summary>
                  <div className={thinkingContent}>{thinkingText}</div>
                </details>
              ) : (
                <div className={thinkingContainer}>
                  <span className={thinkingIcon}>✨</span>
                  <span className={thinkingLabel}>Thinking</span>
                  <span className={thinkingDots}>
                    <span className={dot1}>.</span>
                    <span className={dot2}>.</span>
                    <span className={dot3}>.</span>
                  </span>
                </div>
              )}
            </div>
          )}

          {/* Tool use indicator — shown outside thinking */}
          {toolUseLabel && (
            <div className={assistantOuter}>
              <div className={toolIndicator}>
                <span className={toolIcon}>🔧</span>
                <span>{toolUseLabel}</span>
              </div>
            </div>
          )}

          {/* Generating spinner — shows while streaming */}
          {isStreaming && !isThinking && (
            <div className={assistantOuter}>
              <div className={generatingIndicator}>
                <span className={spinner} />
                <span>Generating…</span>
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
const header = `h-10 shrink-0 border-b border-[hsl(var(--border))] flex items-center px-3 gap-1`;
const statusDot = `w-2 h-2 rounded-full bg-[hsl(var(--progress))] shrink-0`;
const headerTitleBtn = `
  text-xs font-medium text-[hsl(var(--foreground))]
  hover:text-[hsl(var(--muted-foreground))] transition-colors
  truncate max-w-[120px] cursor-pointer bg-transparent border-none p-0`;
const newThreadBtn = `
  text-sm text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  transition-colors cursor-pointer bg-transparent border-none p-0 ml-1 shrink-0`;
const modelSelect = `
  ml-auto text-[10px] bg-transparent text-[hsl(var(--muted-foreground))]
  border border-[hsl(var(--border))] rounded px-1.5 py-0.5
  cursor-pointer outline-none
  hover:text-[hsl(var(--foreground))] hover:border-[hsl(var(--muted-foreground))]
  transition-colors`;
// --- Thread dropdown ---
const threadDropdown = `
  border-b border-[hsl(var(--border))] bg-[hsl(var(--card))]
  max-h-48 overflow-y-auto`;
const threadItem = `
  flex items-center gap-1 hover:bg-[hsl(var(--accent))] transition-colors`;
const threadItemActive = `bg-[hsl(var(--accent))]/50`;
const threadItemBtn = `
  flex-1 flex items-center justify-between gap-2 px-3 py-1.5
  text-[11px] text-[hsl(var(--foreground))] truncate
  bg-transparent border-none cursor-pointer text-left`;
const threadDate = `
  text-[9px] text-[hsl(var(--muted-foreground))] shrink-0`;
const threadDeleteBtn = `
  px-2 py-1 text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--destructive,0_84%_60%))]
  bg-transparent border-none cursor-pointer text-sm shrink-0`;

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

const userOuter = `ml-4`;
const assistantOuter = `mr-2 text-xs leading-relaxed text-[hsl(var(--foreground))] overflow-hidden min-w-0`;
const userBubble = `
  rounded-lg px-3 py-2 text-xs leading-relaxed
  bg-[hsl(var(--chat-user))] text-[hsl(var(--foreground))]
  border border-[hsl(var(--chat-user-border))]`;

// --- Thinking indicator ---
const thinkingContainer = `
  inline-flex items-center gap-1.5 px-2 py-1.5
  text-[11px] text-[hsl(var(--muted-foreground))]`;
const thinkingDetails = `
  text-[11px] text-[hsl(var(--muted-foreground))]
  rounded overflow-hidden min-w-0`;
const thinkingSummary = `
  flex items-center gap-1.5 px-2 py-1 cursor-pointer
  select-none list-none [&::-webkit-details-marker]:hidden`;
const thinkingIcon = `text-sm animate-pulse shrink-0`;
const thinkingLabel = `font-medium`;
const thinkingContent = `
  px-2 pb-1.5 text-[10px] opacity-70
  whitespace-pre-wrap break-words overflow-hidden`;
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

// --- Persisted thinking on completed messages ---
const persistedThinkingDetails = `
  text-[11px] text-[hsl(var(--muted-foreground))]
  rounded overflow-hidden mb-1`;
const persistedThinkingSummary = `
  flex items-center gap-1 px-1 py-0.5 cursor-pointer
  select-none list-none [&::-webkit-details-marker]:hidden
  hover:text-[hsl(var(--foreground))] transition-colors`;
const thinkingCaret = `
  text-[8px] transition-transform duration-200
  [details[open]>&]:rotate-90`;
const persistedThinkingContent = `
  px-1 pb-1 text-[10px] opacity-70
  border-l-2 border-[hsl(var(--border))] ml-1 pl-2
  overflow-hidden`;

// --- Generating indicator ---
const generatingIndicator = `
  inline-flex items-center gap-1.5 px-3 py-1.5
  text-[11px] text-[hsl(var(--muted-foreground))] italic`;
const spinner = `
  w-3 h-3 border-2 border-[hsl(var(--muted-foreground))]/30
  border-t-[hsl(var(--muted-foreground))] rounded-full animate-spin`;

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
