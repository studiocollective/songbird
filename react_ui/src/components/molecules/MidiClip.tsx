interface MidiClipProps {
  trackId: number;
  name: string;
  color: string;
  totalBars: number;
}

export function MidiClip({ trackId, name, color, totalBars }: MidiClipProps) {
  const leftPct = ((trackId % 3) * 2 / totalBars) * 100;
  const widthPct = ((8 - (trackId % 3)) / totalBars) * 100;

  return (
    <div
      className={clip}
      style={{
        '--clip-left': `${leftPct}%`,
        '--clip-width': `${widthPct}%`,
        '--clip-color': color,
      } as React.CSSProperties}
    >
      <div className={clipLabel}>{name}</div>
      <div className={notesContainer}>
        {Array.from({ length: 6 + (trackId % 4) }, (_, j) => (
          <div
            key={j}
            className={noteLine}
            style={{
              '--note-w': `${4 + (j % 3) * 3}px`,
              '--note-mt': `${(j * 2) % 5}px`,
            } as React.CSSProperties}
          />
        ))}
      </div>
    </div>
  );
}

const clip = `
  absolute top-1.5 bottom-1.5 rounded-sm opacity-60
  left-[var(--clip-left)] w-[var(--clip-width)]
  bg-[var(--clip-color)]`;

const clipLabel = `px-1.5 py-0.5 text-[9px] font-medium text-white/80 truncate`;

const notesContainer = `px-1.5 flex gap-px flex-wrap`;

const noteLine = `
  h-0.5 rounded-full bg-white/40
  w-[var(--note-w)] mt-[var(--note-mt)]`;
