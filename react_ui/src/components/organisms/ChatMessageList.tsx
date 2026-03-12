import { ChatMessage, TypingIndicator, ChatSuggestion } from '@/components/molecules';
import songbirdIcon from '@/assets/songbird.svg';

interface ChatMessageListProps {
  messages: { role: 'user' | 'assistant'; content: string }[];
  isTyping: boolean;
  onSuggestionClick: (text: string) => void;
}

const SUGGESTIONS = [
  'moody lo-fi jazz beat',
  'driving techno bassline',
  'ambient pad with reverb',
];

export function ChatMessageList({ messages, isTyping, onSuggestionClick }: ChatMessageListProps) {
  return (
    <div className={container}>
      {messages.length === 0 && (
        <div className={welcomeWrapper}>
          <div className={emoji}><img src={songbirdIcon} alt="Songbird" width={32} height={32} /></div>
          <p className={welcomeText}>
            Describe the music you want to create.
            <br />
            I'll generate bird notation for you.
          </p>
          <div className={suggestionsWrapper}>
            {SUGGESTIONS.map((suggestion) => (
              <ChatSuggestion
                key={suggestion}
                text={suggestion}
                onClick={() => onSuggestionClick(suggestion)}
              />
            ))}
          </div>
        </div>
      )}

      {messages.map((msg, i) => (
        <ChatMessage key={i} role={msg.role} content={msg.content} />
      ))}

      {isTyping && <TypingIndicator />}
    </div>
  );
}

const container = `flex-1 overflow-y-auto p-3 space-y-3`;
const welcomeWrapper = `text-center py-8`;
const emoji = `text-2xl mb-2`;
const welcomeText = `text-xs text-[hsl(var(--muted-foreground))] leading-relaxed`;
const suggestionsWrapper = `mt-4 space-y-1.5`;
