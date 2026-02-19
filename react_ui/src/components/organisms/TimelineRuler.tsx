interface Section {
  name: string;
  start: number;
  length: number;
  color: string;
}

interface TimelineRulerProps {
  sections: Section[];
  totalBars: number;
}

export function TimelineRuler({ sections, totalBars }: TimelineRulerProps) {
  return (
    <div className={wrapper}>
      <div className={spacer} />
      <div className={ruler}>
        {sections.map((sec, i) => (
          <div
            key={i}
            className={sectionLabel}
            style={{
              '--label-left': `${(sec.start / totalBars) * 100}%`,
              '--label-width': `${(sec.length / totalBars) * 100}%`,
            } as React.CSSProperties}
          >
            {sec.name}
          </div>
        ))}
        {Array.from({ length: totalBars }, (_, i) => (
          <div
            key={i}
            className={barNumber}
            style={{
              '--bar-left': `${(i / totalBars) * 100}%`,
              '--bar-width': `${(1 / totalBars) * 100}%`,
            } as React.CSSProperties}
          >
            {i + 1}
          </div>
        ))}
      </div>
    </div>
  );
}

const wrapper = `flex shrink-0`;
const spacer = `w-44 shrink-0 bg-[hsl(var(--background))] border-b border-r border-[hsl(var(--border))]`;
const ruler = `
  flex-1 h-10 bg-[hsl(var(--background))] border-b border-[hsl(var(--border))]
  flex items-end relative overflow-hidden`;

const sectionLabel = `
  absolute top-0 h-5 flex items-center justify-center
  text-[10px] font-medium text-[hsl(var(--muted-foreground))]
  border-x border-[hsl(var(--border))]/50
  left-[var(--label-left)] w-[var(--label-width)]`;

const barNumber = `
  absolute bottom-0 h-5 flex items-center justify-center
  text-[9px] font-mono text-[hsl(var(--muted-foreground))]
  border-r border-[hsl(var(--border))]/50
  left-[var(--bar-left)] w-[var(--bar-width)]`;
