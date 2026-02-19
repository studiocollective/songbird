import { cn } from '@/lib/utils';

interface LevelMeterProps {
  level: number;    // 0–100, real audio level from C++
  volume: number;   // 0–127, fader setting (shown as reference line)
  color: string;
  height?: string;
  muted?: boolean;
}

export function LevelMeter({ level, volume, color, height = 'h-24', muted = false }: LevelMeterProps) {
  const displayLevel = muted ? 0 : level;
  const volumePct = (volume / 127) * 100;

  return (
    <div className={cn(track, height)}>
      {/* Static volume reference fill (subtle) */}
      <div
        className={volumeFill}
        style={{
          '--vol-height': `${volumePct}%`,
          '--meter-color': color,
        } as React.CSSProperties}
      />
      {/* Real-time audio level (brighter overlay) */}
      <div
        className={levelFill}
        style={{
          '--meter-height': `${displayLevel}%`,
          '--meter-color': color,
        } as React.CSSProperties}
      />
    </div>
  );
}

const track = `relative w-1.5 bg-[hsl(var(--card))] rounded-full overflow-hidden`;
const volumeFill = `
  absolute bottom-0 w-full rounded-full
  h-[var(--vol-height)]
  bg-[var(--meter-color)]/10`;
const levelFill = `
  absolute bottom-0 w-full rounded-full
  h-[var(--meter-height)]
  bg-gradient-to-t from-[var(--meter-color)]/50 to-[var(--meter-color)]/90`;
