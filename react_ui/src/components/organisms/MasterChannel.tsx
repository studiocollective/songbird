export function MasterChannel() {
  return (
    <div className={channel}>
      <span className={label}>Master</span>
      <div className={meterWrapper}>
        <div className={meterTrack}>
          <div className={meterFill} />
          {Array.from({ length: 12 }, (_, i) => (
            <div
              key={i}
              className={segment}
              style={{ '--seg-bottom': `${(i / 12) * 100}%` } as React.CSSProperties}
            />
          ))}
        </div>
      </div>
      <div className={readout}>0.0</div>
    </div>
  );
}

const channel = `
  w-20 shrink-0 border-r border-[hsl(var(--border))]
  flex flex-col items-center py-2 bg-[hsl(var(--mixer))]`;
const label = `text-[9px] text-[hsl(var(--muted-foreground))] uppercase tracking-widest mb-2`;
const meterWrapper = `flex-1 flex items-center justify-center`;
const meterTrack = `relative w-2 h-28 bg-[hsl(var(--card))] rounded-full`;
const meterFill = `absolute bottom-0 w-full h-3/4 rounded-full bg-gradient-to-t from-[hsl(var(--progress))] to-[hsl(var(--progress))]/80`;
const segment = `absolute w-full h-px bg-[hsl(var(--mixer))] bottom-[var(--seg-bottom)]`;
const readout = `text-[10px] font-mono text-[hsl(var(--muted-foreground))] mt-1`;
