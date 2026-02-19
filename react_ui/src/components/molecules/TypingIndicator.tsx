export function TypingIndicator() {
  return (
    <div className={wrapper}>
      <div className={container}>
        <div className={dotClass} />
        <div className={dotDelay1} />
        <div className={dotDelay2} />
      </div>
    </div>
  );
}

const wrapper = `mr-2`;
const container = `
  bg-[hsl(var(--chat-assistant))] border border-[hsl(var(--chat-assistant-border))]
  rounded-lg px-3 py-2 inline-flex gap-1`;
const dotBase = `w-1.5 h-1.5 rounded-full bg-[hsl(var(--muted-foreground))] animate-bounce`;
const dotClass = dotBase;
const dotDelay1 = `${dotBase} [animation-delay:150ms]`;
const dotDelay2 = `${dotBase} [animation-delay:300ms]`;
