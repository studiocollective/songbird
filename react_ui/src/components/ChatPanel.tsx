import { useState } from 'react';
import { useAppStore } from '@/data/store';

export function ChatPanel() {
  const { chatOpen, chatMessages, chatInput, setChatInput, addMessage } = useAppStore();
  const [isTyping, setIsTyping] = useState(false);

  const handleSend = () => {
    if (!chatInput.trim()) return;
    addMessage('user', chatInput);
    setChatInput('');

    // Simulate LLM response
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
    <div
      className={`bg-zinc-900 border-l border-zinc-700 transition-all duration-300 ease-in-out overflow-hidden flex flex-col ${
        chatOpen ? 'w-80' : 'w-0'
      }`}
    >
      <div className="w-80 h-full flex flex-col">
        {/* Header */}
        <div className="h-10 shrink-0 border-b border-zinc-800 flex items-center px-3">
          <div className="w-2 h-2 rounded-full bg-emerald-500 mr-2" />
          <span className="text-xs font-medium text-zinc-300">Songbird Copilot</span>
          <span className="text-[10px] text-zinc-600 ml-auto">bird notation</span>
        </div>

        {/* Messages */}
        <div className="flex-1 overflow-y-auto p-3 space-y-3">
          {/* Welcome message */}
          {chatMessages.length === 0 && (
            <div className="text-center py-8">
              <div className="text-2xl mb-2">🐦</div>
              <p className="text-xs text-zinc-500 leading-relaxed">
                Describe the music you want to create.
                <br />
                I'll generate bird notation for you.
              </p>
              <div className="mt-4 space-y-1.5">
                {[
                  'moody lo-fi jazz beat',
                  'driving techno bassline',
                  'ambient pad with reverb',
                ].map((suggestion) => (
                  <button
                    key={suggestion}
                    onClick={() => {
                      setChatInput(suggestion);
                    }}
                    className="block w-full text-left px-3 py-1.5 rounded bg-zinc-800/50 hover:bg-zinc-800 text-[11px] text-zinc-400 hover:text-zinc-300 transition-colors"
                  >
                    "{suggestion}"
                  </button>
                ))}
              </div>
            </div>
          )}

          {/* Chat messages */}
          {chatMessages.map((msg, i) => (
            <div
              key={i}
              className={`${
                msg.role === 'user' ? 'ml-6' : 'mr-2'
              }`}
            >
              <div
                className={`rounded-lg px-3 py-2 text-xs leading-relaxed ${
                  msg.role === 'user'
                    ? 'bg-emerald-900/40 text-emerald-200 border border-emerald-800/30'
                    : 'bg-zinc-800 text-zinc-300 border border-zinc-700/50'
                }`}
              >
                <pre className="whitespace-pre-wrap font-sans">{msg.content}</pre>
              </div>
            </div>
          ))}

          {/* Typing indicator */}
          {isTyping && (
            <div className="mr-2">
              <div className="bg-zinc-800 border border-zinc-700/50 rounded-lg px-3 py-2 inline-flex gap-1">
                <div className="w-1.5 h-1.5 rounded-full bg-zinc-500 animate-bounce" style={{ animationDelay: '0ms' }} />
                <div className="w-1.5 h-1.5 rounded-full bg-zinc-500 animate-bounce" style={{ animationDelay: '150ms' }} />
                <div className="w-1.5 h-1.5 rounded-full bg-zinc-500 animate-bounce" style={{ animationDelay: '300ms' }} />
              </div>
            </div>
          )}
        </div>

        {/* Input */}
        <div className="shrink-0 border-t border-zinc-800 p-2">
          <div className="flex gap-2">
            <input
              type="text"
              value={chatInput}
              onChange={(e) => setChatInput(e.target.value)}
              onKeyDown={(e) => e.key === 'Enter' && handleSend()}
              placeholder="Describe your music..."
              className="flex-1 h-8 bg-zinc-800 border border-zinc-700 rounded-md px-3 text-xs text-white placeholder-zinc-600 focus:outline-none focus:border-zinc-500"
            />
            <button
              onClick={handleSend}
              className="h-8 px-3 rounded-md bg-emerald-700 hover:bg-emerald-600 text-xs text-white font-medium transition-colors"
            >
              Send
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
