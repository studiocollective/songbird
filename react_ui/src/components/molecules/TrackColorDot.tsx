import { cn } from '@/lib/utils';

interface TrackColorDotProps {
  color: string;
  size?: 'sm' | 'md';
}

export function TrackColorDot({ color, size = 'sm' }: TrackColorDotProps) {
  return (
    <div
      className={cn(dot, size === 'sm' ? dotSm : dotMd)}
      style={{ '--dot-color': color } as React.CSSProperties}
    />
  );
}

const dot = `rounded-full shrink-0 bg-[var(--dot-color)]`;
const dotSm = `w-2.5 h-2.5`;
const dotMd = `w-2 h-2`;
