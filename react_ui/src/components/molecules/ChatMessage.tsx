import { cn } from '@/lib/utils';

interface ChatMessageProps {
  role: 'user' | 'assistant';
  content: string;
}

export function ChatMessage({ role, content }: ChatMessageProps) {
  return (
    <div className={role === 'user' ? userOuter : assistantOuter}>
      <div className={cn(bubble, role === 'user' ? userBubble : assistantBubble)}>
        <pre className={messageText}>{content}</pre>
      </div>
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
