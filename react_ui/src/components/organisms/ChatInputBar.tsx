interface ChatInputBarProps {
  value: string;
  onChange: (value: string) => void;
  onSend: () => void;
}

export function ChatInputBar({ value, onChange, onSend }: ChatInputBarProps) {
  return (
    <div className={wrapper}>
      <div className={row}>
        <input
          type="text"
          value={value}
          onChange={(e) => onChange(e.target.value)}
          onKeyDown={(e) => e.key === 'Enter' && onSend()}
          placeholder="Describe your music..."
          className={input}
        />
        <button onClick={onSend} className={sendBtn}>
          Send
        </button>
      </div>
    </div>
  );
}

const wrapper = `shrink-0 border-t border-[hsl(var(--border))] p-2`;
const row = `flex gap-2`;
const input = `
  flex-1 h-8 bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  rounded-md px-3 text-xs text-[hsl(var(--foreground))]
  placeholder-[hsl(var(--muted-foreground))]
  focus:outline-none focus:border-[hsl(var(--ring))]`;
const sendBtn = `
  h-8 px-3 rounded-md bg-[hsl(var(--progress))] hover:bg-[hsl(var(--progress))]/80
  text-xs text-[hsl(var(--primary-foreground))] font-medium transition-colors`;
