import { useState } from 'react';
import { cn } from '@/lib/utils';
import { Juce, isPlugin } from '@/lib';

const revertLLM = isPlugin ? Juce.getNativeFunction('revertLLM') : null;

interface ChatMessageProps {
  role: 'user' | 'assistant';
  content: string;
}

export function ChatMessage({ role, content }: ChatMessageProps) {
  const [hovering, setHovering] = useState(false);

  const handleRevert = (e: React.MouseEvent) => {
    e.stopPropagation();
    revertLLM?.();
  };

  return (
    <div
      className={cn(role === 'user' ? userOuter : assistantOuter, 'group relative')}
      onMouseEnter={() => setHovering(true)}
      onMouseLeave={() => setHovering(false)}
    >
      <div className={cn(bubble, role === 'user' ? userBubble : assistantBubble)}>
        <pre className={messageText}>{content}</pre>
      </div>
      {role === 'assistant' && hovering && (
        <button
          onClick={handleRevert}
          className={revertBtn}
          title="Revert to before this LLM change"
        >
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
            <polyline points="1 4 1 10 7 10" />
            <path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10" />
          </svg>
        </button>
      )}
    </div>
  );
}

const userOuter = `ml-6`;
const assistantOuter = `mr-2`;
const bubble = `rounded-lg px-3 py-2 text-xs leading-relaxed`;
const userBubble = `
  bg-[hsl(var(--chat-user))] text-[hsl(var(--foreground))]
  border border-[hsl(var(--chat-user-border))]`;
const assistantBubble = `
  bg-[hsl(var(--chat-assistant))] text-[hsl(var(--foreground))]
  border border-[hsl(var(--chat-assistant-border))]`;
const messageText = `whitespace-pre-wrap font-sans`;
const revertBtn = `
  absolute -left-1 top-1
  w-5 h-5 flex items-center justify-center
  rounded-full
  bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))]
  hover:bg-[hsl(var(--destructive))] hover:text-white
  transition-colors duration-150 cursor-pointer
  opacity-70 hover:opacity-100
  shadow-sm
`;

