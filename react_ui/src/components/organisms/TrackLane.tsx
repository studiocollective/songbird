import { TrackHeader } from './TrackHeader';
import { MidiClip } from '@/components/molecules';

interface Section {
  name: string;
  start: number;
  length: number;
  color: string;
}

interface TrackLaneProps {
  trackId: number;
  name: string;
  color: string;
  muted: boolean;
  solo: boolean;
  sections: Section[];
  totalBars: number;
}

export function TrackLane({ trackId, name, color, muted, solo, sections, totalBars }: TrackLaneProps) {
  return (
    <div className={row}>
      <TrackHeader trackId={trackId} name={name} color={color} muted={muted} solo={solo} />
      <div className={contentArea}>
        {sections.map((sec, i) => (
          <div
            key={i}
            className={`${sectionBg} ${sec.color}`}
            style={{
              '--sec-left': `${(sec.start / totalBars) * 100}%`,
              '--sec-width': `${(sec.length / totalBars) * 100}%`,
            } as React.CSSProperties}
          />
        ))}
        <MidiClip trackId={trackId} name={name} color={color} totalBars={totalBars} />
      </div>
    </div>
  );
}

const row = `flex h-16 border-b border-[hsl(var(--border))]/50 group`;
const contentArea = `flex-1 relative`;
const sectionBg = `
  absolute inset-y-0 border-r border-[hsl(var(--border))]/30
  left-[var(--sec-left)] w-[var(--sec-width)]`;
