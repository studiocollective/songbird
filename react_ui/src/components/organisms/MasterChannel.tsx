import { useRef, useEffect } from 'react';
import { useMeterStore } from '@/data/meters';

/**
 * MasterChannel — direct DOM manipulation for zero-overhead metering.
 *
 * Renders once, then imperatively updates meter fills and readout
 * via a store subscription + DOM refs. No React re-renders for level changes.
 */
export function MasterChannel() {
  const leftRef = useRef<HTMLDivElement>(null);
  const rightRef = useRef<HTMLDivElement>(null);
  const readoutRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const unsub = useMeterStore.subscribe((state) => {
      const { master } = state;
      if (leftRef.current) leftRef.current.style.height = `${master.left}%`;
      if (rightRef.current) rightRef.current.style.height = `${master.right}%`;
      if (readoutRef.current) {
        const avg = Math.max(master.left, master.right);
        readoutRef.current.textContent = avg > 0
          ? `-${(60 - avg * 0.6).toFixed(1)}`
          : '-∞';
      }
    });
    return unsub;
  }, []);

  return (
    <div className={channel}>
      <span className={label}>Master</span>
      <div className={meterWrapper}>
        {/* Left channel */}
        <div className={meterTrack}>
          <div ref={leftRef} className={meterFill} />
          {Array.from({ length: 12 }, (_, i) => (
            <div
              key={i}
              className={segment}
              style={{ '--seg-bottom': `${(i / 12) * 100}%` } as React.CSSProperties}
            />
          ))}
        </div>
        {/* Right channel */}
        <div className={meterTrack}>
          <div ref={rightRef} className={meterFill} />
          {Array.from({ length: 12 }, (_, i) => (
            <div
              key={i}
              className={segment}
              style={{ '--seg-bottom': `${(i / 12) * 100}%` } as React.CSSProperties}
            />
          ))}
        </div>
      </div>
      <div ref={readoutRef} className={readout}>-∞</div>
    </div>
  );
}

const channel = `
  w-16 shrink-0 border-r border-[hsl(var(--border))]
  flex flex-col items-center py-2 bg-[hsl(var(--mixer))]`;
const label = `text-[9px] text-[hsl(var(--muted-foreground))] uppercase tracking-widest mb-2`;
const meterWrapper = `flex-1 flex items-center justify-center gap-0.5`;
const meterTrack = `relative w-1.5 h-28 bg-[hsl(var(--card))] rounded-full overflow-hidden`;
const meterFill = `
  absolute bottom-0 w-full rounded-full
  bg-gradient-to-t from-emerald-500/80 via-emerald-400 to-red-500`;
const segment = `absolute w-full h-px bg-[hsl(var(--mixer))] bottom-[var(--seg-bottom)]`;
const readout = `text-[10px] font-mono text-[hsl(var(--muted-foreground))] mt-1`;
