import { useState, useEffect, useRef, useCallback } from 'react';
import { Juce, isPlugin } from '@/lib';

interface HistoryEntry {
  hash: string;
  message: string;
  author?: 'user' | 'ai' | 'system';
}

export function HistoryPanel() {
  const [entries, setEntries] = useState<HistoryEntry[]>([]);
  const [headIndex, setHeadIndex] = useState(0);
  const [redoTipHash, setRedoTipHash] = useState('');
  const didFetch = useRef<boolean | null>(null);

  const refresh = useCallback(() => {

    console.log('refresh  history');
    if (!isPlugin) return;
    const fn = Juce.getNativeFunction('getHistory');
    fn().then((raw: unknown) => {
      setTimeout(() => {
        try {
          const data = JSON.parse(raw as string);
          console.log(data)
          if (Array.isArray(data)) {
            setEntries(data);
          } else {
            setEntries(data.commits ?? []);
            setHeadIndex(data.headIndex ?? 0);
            setRedoTipHash(data.redoTipHash ?? '');
          }
        } catch (e) { console.warn('[History]', e); }
      }, 0);
    }).catch(() => {});
  }, []);

  // Auto-fetch once on mount
  if (didFetch.current == null) {
    didFetch.current = true;
    setTimeout(refresh, 50);
  }

  // Auto-refresh on commits and undo/redo
  useEffect(() => {
    if (!isPlugin) return;
    console.log('refresh  history');
    const handler = () => setTimeout(refresh, 0);
    window.__JUCE__!.backend.addEventListener('historyChanged', handler);
    return () => window.__JUCE__!.backend.removeEventListener('historyChanged', handler);
  }, [refresh]);

  // Find first boundary — divider goes BELOW it
  const boundaryIndex = entries.findIndex(
    e => e.message.includes('Project loaded') || e.message.includes('Initial project state')
  );

  return (
    <div className={panel}>
      <div className={panelInner}>
        <div className={header}>
          <span className="text-[10px]">📜</span>
          <span className="text-xs font-medium text-[hsl(var(--foreground))]">History</span>
          <button onClick={refresh} className={refreshBtn} title="Refresh">↻</button>
        </div>

        <div className={logScroll}>
          {entries.length === 0 ? (
            <div className={emptyLine}>No commits yet</div>
          ) : (
            entries.map((entry, i) => {
              const isHead = i === headIndex;
              const isRedoTip = redoTipHash && (entry.hash.startsWith(redoTipHash) || redoTipHash.startsWith(entry.hash));
              const isRedo = headIndex >= 0 && i < headIndex;
              const showDivider = i === boundaryIndex;
              return (
                <div key={entry.hash + i}>
                  <div className={`${logLine} ${isRedo ? 'opacity-35' : ''} ${isHead ? headLine : ''}`}>
                    <span className={markerCol}>
                      {isHead ? '▶' : isRedoTip ? '⤴' : ''}
                    </span>
                    {entry.author === 'ai' && <span className={aiBadge}>🤖</span>}
                    <span className={hashStyle}>{entry.hash}</span>
                    <span>{entry.message}</span>
                  </div>
                  {showDivider && (
                    <div className={divider}>
                      <span className="text-[9px] text-[hsl(var(--muted-foreground))] px-2">session boundary</span>
                    </div>
                  )}
                </div>
              );
            })
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

const logScroll = `flex-1 overflow-y-auto`;

const logLine = `
  flex items-center
  px-3 py-0.5 font-mono text-[11px] leading-snug
  text-[hsl(var(--foreground))] whitespace-nowrap
  hover:bg-[hsl(var(--accent))]/20`;

const headLine = `bg-[hsl(var(--accent))]/15`;

const markerCol = `w-3 shrink-0 text-[8px] text-[hsl(var(--progress))]`;

const divider = `
  flex items-center justify-center
  border-t border-dashed border-[hsl(var(--border))]
  my-1 mx-3`;

const emptyLine = `
  px-3 py-4 text-center text-[11px]
  text-[hsl(var(--muted-foreground))] italic`;

const hashStyle = `text-[hsl(var(--progress))] mr-1.5 shrink-0`;

const aiBadge = `text-[9px] mr-1 shrink-0 opacity-70`;
