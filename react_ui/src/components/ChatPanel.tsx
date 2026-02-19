import { useState } from 'react';
import { cn } from '@/lib/utils';
import { useChatStore } from '@/data/store';

export function ChatPanel() {
  const { chatOpen, chatMessages, chatInput, setChatInput, addMessage } = useChatStore();
  const [isTyping, setIsTyping] = useState(false);

  const handleSend = () => {
    if (!chatInput.trim()) return;
    addMessage('user', chatInput);
    setChatInput('');

    setIsTyping(true);
    setTimeout(() => {
      addMessage(
        'assistant',
        `Here's a bird notation for that:\n\n\`\`\`\nb 4\n\nch 1 keys\n  p q q q q\n    sw < ~\n      v 80 60 90 70\n        n @Cm7 @Fm7 @Ab @G7\n  mix volume 85\n  mix comp 40\n\`\`\``
      );
      setIsTyping(false);
    }, 1500);
  };

  return (
    <div className={cn(panel, chatOpen ? 'w-80' : 'w-0')}>
      <div className={panelInner}>
        {/* Header */}
        <div className={header}>
          <div className={statusDot} />
          <span className={headerTitle}>Songbird Copilot</span>
          <span className={headerSubtitle}>bird notation</span>
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
            />
            <button onClick={handleSend} className={sendBtn}>
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
const headerSubtitle = `text-[10px] text-[hsl(var(--muted-foreground))] ml-auto`;

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
  focus:outline-none focus:border-[hsl(var(--ring))]`;
const sendBtn = `
  h-8 px-3 rounded-md bg-[hsl(var(--progress))] hover:bg-[hsl(var(--progress))]/80
  text-xs text-[hsl(var(--primary-foreground))] font-medium transition-colors`;
