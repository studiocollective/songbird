import { useState, useEffect, useCallback } from 'react';
import { Juce, isPlugin } from '@/lib';
import { addStateListener } from '@/data/bridge';

interface HistoryEntry {
  hash: string;
  message: string;
}

const getHistory = isPlugin ? Juce.getNativeFunction('getHistory') : null;

export function HistoryPanel() {
  const [open, setOpen] = useState(false);
  const [entries, setEntries] = useState<HistoryEntry[]>([]);

  const fetchHistory = useCallback(async () => {
    try {
      const raw = await getHistory?.();
      if (raw) {
        const data = JSON.parse(raw);
        setEntries(data.entries || []);
      }
    } catch (e) {
      console.warn('[History] Failed to fetch:', e);
    }
  }, []);

  useEffect(() => {
    const unsub = addStateListener('historyChanged', () => fetchHistory());
    return () => unsub();
  }, [fetchHistory]);

  return (
    <div className={wrapper}>
      <button
        onClick={() => { setOpen(!open); if (!open) fetchHistory(); }}
        className={toggle}
      >
        <span>History</span>
        <span className="ml-auto opacity-50">{open ? '▼' : '▶'}</span>
      </button>

      {open && (
        <div className={listContainer}>
          {entries.length === 0 ? (
            <div className={line}>No commits yet</div>
          ) : (
            entries.map((entry, i) => (
              <div key={entry.hash + i} className={line}>
                <span className={hash}>{entry.hash}</span>{' '}
                <span className={msg}>{entry.message}</span>
              </div>
            ))
          )}
        </div>
      )}
    </div>
  );
}

const wrapper = `border-t border-[hsl(var(--border))] bg-[hsl(var(--background))]`;

const toggle = `
  w-full flex items-center gap-1.5 px-3 py-1
  text-[11px] font-mono text-[hsl(var(--muted-foreground))]
  hover:text-[hsl(var(--foreground))] transition-colors
  bg-transparent border-none cursor-pointer`;

const listContainer = `max-h-48 overflow-y-auto border-t border-[hsl(var(--border))]/50`;

const line = `
  px-3 py-0.5 font-mono text-[11px] leading-snug
  text-[hsl(var(--foreground))] whitespace-nowrap`;

const hash = `text-[hsl(var(--progress))]`;
const msg = ``;
