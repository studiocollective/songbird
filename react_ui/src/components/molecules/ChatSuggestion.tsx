interface ChatSuggestionProps {
  text: string;
  onClick: () => void;
}

export function ChatSuggestion({ text, onClick }: ChatSuggestionProps) {
  return (
    <button onClick={onClick} className={button}>
      "{text}"
    </button>
  );
}

const button = `
  block w-full text-left px-3 py-1.5 rounded
  bg-[hsl(var(--card))]/50 hover:bg-[hsl(var(--card))]
  text-[11px] text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  transition-colors`;
