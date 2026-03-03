import { useState, useEffect, useRef, useCallback } from 'react';
import { Juce, isPlugin } from '@/lib';

export function BirdFilePanel() {
  const [content, setContent] = useState<string>('');
  const didFetch = useRef<boolean | null>(null);

  const refresh = useCallback(() => {
    if (!isPlugin) return;
    const fn = Juce.getNativeFunction('readBird');
    fn().then((raw: unknown) => {
      setContent((raw as string) || '');
    }).catch(() => {});
  }, []);

  // Auto-fetch once on mount
  if (didFetch.current == null) {
    didFetch.current = true;
    setTimeout(refresh, 50);
  }

  // Auto-refresh when bird file changes (reuses historyChanged event)
  useEffect(() => {
    if (!isPlugin) return;
    const handler = () => setTimeout(refresh, 100);
    window.__JUCE__!.backend.addEventListener('historyChanged', handler);
    return () => window.__JUCE__!.backend.removeEventListener('historyChanged', handler);
  }, [refresh]);

  const lines = content.split('\n');

  return (
    <div className={panel}>
      <div className={panelInner}>
        <div className={header}>
          <span className="text-[10px]">🐦</span>
          <span className="text-xs font-medium text-[hsl(var(--foreground))]">Bird File</span>
          <button onClick={refresh} className={refreshBtn} title="Refresh">↻</button>
        </div>

        <div className={codeScroll}>
          {content.length === 0 ? (
            <div className={emptyLine}>No bird file loaded</div>
          ) : (
            <pre className={preBlock}>
              {lines.map((line, i) => (
                <div key={i} className={codeLine}>
                  <span className={lineNum}>{i + 1}</span>
                  <span>{line}</span>
                </div>
              ))}
            </pre>
          )}
        </div>
      </div>
    </div>
  );
}

const panel = `
  bg-[hsl(var(--background))] border-l border-[hsl(var(--border))]
  w-80 flex flex-col overflow-hidden`;

const panelInner = `w-80 h-full flex flex-col`;

const header = `
  h-10 shrink-0 border-b border-[hsl(var(--border))]
  flex items-center px-3 gap-1.5`;

const refreshBtn = `
  ml-auto text-sm text-[hsl(var(--muted-foreground))]
  hover:text-[hsl(var(--foreground))] transition-colors
  bg-transparent border-none cursor-pointer`;

const codeScroll = `flex-1 overflow-y-auto overflow-x-auto`;

const preBlock = `m-0 p-0 font-mono text-[11px] leading-snug`;

const codeLine = `
  flex
  px-1 py-[1px]
  text-[hsl(var(--foreground))] whitespace-pre
  hover:bg-[hsl(var(--accent))]/20`;

const lineNum = `
  w-8 shrink-0 text-right pr-2
  text-[hsl(var(--muted-foreground))] select-none`;

const emptyLine = `
  px-3 py-4 text-center text-[11px]
  text-[hsl(var(--muted-foreground))] italic`;
