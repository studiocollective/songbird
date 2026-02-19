import { cn } from '@/lib/utils';

interface LevelMeterProps {
  value: number;
  color: string;
  height?: string;
  muted?: boolean;
}

export function LevelMeter({ value, color, height = 'h-24', muted = false }: LevelMeterProps) {
  return (
    <div className={cn(track, height)}>
      <div
        className={fill}
        style={{
          '--meter-height': `${muted ? 0 : value}%`,
          '--meter-color': color,
        } as React.CSSProperties}
      />
    </div>
  );
}

const track = `relative w-1.5 bg-[hsl(var(--card))] rounded-full overflow-hidden`;
const fill = `
  absolute bottom-0 w-full rounded-full transition-all
  h-[var(--meter-height)]
  bg-gradient-to-t from-[var(--meter-color)]/50 to-[var(--meter-color)]`;
