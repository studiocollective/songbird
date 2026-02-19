import { useMeterStore } from '@/data/meters';

export function MasterChannel() {
  const { master } = useMeterStore();
  const avgLevel = Math.max(master.left, master.right);

  return (
    <div className={channel}>
      <span className={label}>Master</span>
      <div className={meterWrapper}>
        {/* Left channel */}
        <div className={meterTrack}>
          <div
            className={meterFill}
            style={{ '--meter-h': `${master.left}%` } as React.CSSProperties}
          />
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
          <div
            className={meterFill}
            style={{ '--meter-h': `${master.right}%` } as React.CSSProperties}
          />
          {Array.from({ length: 12 }, (_, i) => (
            <div
              key={i}
              className={segment}
              style={{ '--seg-bottom': `${(i / 12) * 100}%` } as React.CSSProperties}
            />
          ))}
        </div>
      </div>
      <div className={readout}>{avgLevel > 0 ? `-${(60 - avgLevel * 0.6).toFixed(1)}` : '-∞'}</div>
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
  h-[var(--meter-h)]
  bg-gradient-to-t from-emerald-500/80 via-emerald-400 to-amber-400`;
const segment = `absolute w-full h-px bg-[hsl(var(--mixer))] bottom-[var(--seg-bottom)]`;
const readout = `text-[10px] font-mono text-[hsl(var(--muted-foreground))] mt-1`;
