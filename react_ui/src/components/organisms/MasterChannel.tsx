import { useRef, useEffect } from 'react';
import { subscribeRtBuffer } from '@/data/meters';

interface MasterChannelProps {
  returnsOpen?: boolean;
  onToggleReturns?: () => void;
  recordStripOpen?: boolean;
  onToggleRecordStrip?: () => void;
}

/**
 * MasterChannel — direct DOM manipulation for zero-overhead metering.
 *
 * Renders once, then imperatively updates meter fills and readout
 * via a subscribeRtBuffer subscription + DOM refs. No React re-renders for level changes.
 */
export function MasterChannel({ returnsOpen, onToggleReturns, recordStripOpen, onToggleRecordStrip }: MasterChannelProps) {
  const leftRef = useRef<HTMLDivElement>(null);
  const rightRef = useRef<HTMLDivElement>(null);
  const readoutRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const unsub = subscribeRtBuffer((buf) => {
      const { master } = buf;
      if (leftRef.current) leftRef.current.style.transform = `scaleY(${master.left / 100})`;
      if (rightRef.current) rightRef.current.style.transform = `scaleY(${master.right / 100})`;
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
      <div className="flex gap-1 mt-1">
        {onToggleRecordStrip && (
          <button
            onClick={onToggleRecordStrip}
            className={[
              "w-6 h-6 rounded flex items-center justify-center text-[10px] font-bold transition-colors",
              recordStripOpen
                ? "bg-white text-black"
                : "bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--muted))]/80"
            ].join(' ')}
            title="Toggle Input / Record Strip"
          >
            I
          </button>
        )}
        {onToggleReturns && (
          <button
            onClick={onToggleReturns}
            className={[
              "w-6 h-6 rounded flex items-center justify-center text-[10px] font-bold transition-colors",
              returnsOpen
                ? "bg-white text-black"
                : "bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--muted))]/80"
            ].join(' ')}
            title="Toggle Return Tracks & Sends"
          >
            R
          </button>
        )}
      </div>
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
  absolute bottom-0 w-full h-full rounded-full origin-bottom will-change-transform
  bg-gradient-to-t from-emerald-500/80 via-emerald-400 to-red-500`;
const segment = `absolute w-full h-px bg-[hsl(var(--mixer))] bottom-[var(--seg-bottom)]`;
const readout = `text-[10px] font-mono text-[hsl(var(--muted-foreground))] mt-1`;
