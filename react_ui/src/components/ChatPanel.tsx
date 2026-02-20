import { useState, useEffect, useRef } from 'react';
import { cn } from '@/lib/utils';
import { useChatStore } from '@/data/store';
import { buildSystemPrompt } from '@/lib/ai/prompts';
import { GeminiService } from '@/lib/ai/gemini';
import { Juce } from '@/lib';

export function ChatPanel() {
  const { chatOpen, chatMessages, chatInput, apiKey, selectedModel, setChatInput, setApiKey, setSelectedModel, addMessage, updateLastMessage } = useChatStore();
  const [isTyping, setIsTyping] = useState(false);
  const [tempKey, setTempKey] = useState('');
  const [loadingKey, setLoadingKey] = useState(true);
  const messagesEndRef = useRef<HTMLDivElement>(null);

  const scrollToBottom = () => {
    messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  };

  useEffect(() => {
    scrollToBottom();
  }, [chatMessages, isTyping]);

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

  const handleSend = async () => {
    if (!chatInput.trim() || !apiKey) return;
    
    addMessage('user', chatInput);
    setChatInput('');
    setIsTyping(true);

    // Add empty assistant message that will be streamed into
    addMessage('assistant', '');

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
        // Pass history without the empty assistant message we just added
        useChatStore.getState().chatMessages.slice(0, -1),
        buildSystemPrompt(currentBird),
        (_delta, accumulated) => {
          updateLastMessage(accumulated);
        },
        selectedModel,
        // Tool call handler: Gemini calls update_bird_file → we save via C++
        async (call) => {
          if (call.name === 'update_bird_file') {
            const content = call.args.content as string;
            console.log('[Chat] Tool call: update_bird_file, saving...');
            await Juce.getNativeFunction('updateBird')(content);
            return { success: true };
          }
          return { error: 'Unknown tool' };
        }
      );
      
      if (result.error) {
        updateLastMessage(`⚠️ Error: ${result.error}`);
      }
    } catch (e) {
      console.error(e);
      updateLastMessage('⚠️ Failed to connect to Gemini.');
    } finally {
      setIsTyping(false);
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
          {chatMessages.length === 0 && (
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
                <pre className={messageBody}>{msg.content}</pre>
              </div>
            </div>
          ))}

          {isTyping && (
            <div className={assistantOuter}>
              <div className={typingContainer}>
                <div className={typingDot} />
                <div className={typingDot1} />
                <div className={typingDot2} />
              </div>
            </div>
          )}
          <div ref={messagesEndRef} />
        </div>

        {/* Input */}
        <div className={inputWrapper}>
          <div className={inputRow}>
            <input
              type="text"
              value={chatInput}
              onChange={(e) => setChatInput(e.target.value)}
              onKeyDown={(e) => e.key === 'Enter' && handleSend()}
              placeholder="Describe your music..."
              className={inputField}
              disabled={isTyping}
            />
            <button onClick={handleSend} className={sendBtn} disabled={isTyping}>
              Send
            </button>
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
const messageBody = `whitespace-pre-wrap font-sans`;

// --- Typing ---
const typingContainer = `
  bg-[hsl(var(--chat-assistant))] border border-[hsl(var(--chat-assistant-border))]
  rounded-lg px-3 py-2 inline-flex gap-1`;
const typingDotBase = `w-1.5 h-1.5 rounded-full bg-[hsl(var(--muted-foreground))] animate-bounce`;
const typingDot = typingDotBase;
const typingDot1 = `${typingDotBase} [animation-delay:150ms]`;
const typingDot2 = `${typingDotBase} [animation-delay:300ms]`;

// --- Input ---
const inputWrapper = `shrink-0 border-t border-[hsl(var(--border))] p-2`;
const inputRow = `flex gap-2`;
const inputField = `
  flex-1 h-8 bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  rounded-md px-3 text-xs text-[hsl(var(--foreground))]
  placeholder-[hsl(var(--muted-foreground))]
  focus:outline-none focus:border-[hsl(var(--ring))]
  disabled:opacity-50`;
const sendBtn = `
  h-8 px-3 rounded-md bg-[hsl(var(--progress))] hover:bg-[hsl(var(--progress))]/80
  text-xs text-[hsl(var(--primary-foreground))] font-medium transition-colors
  disabled:opacity-50 disabled:cursor-not-allowed`;
