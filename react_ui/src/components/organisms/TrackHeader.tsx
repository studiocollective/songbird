import { TrackColorDot, MuteSoloButtons } from '@/components/molecules';

interface TrackHeaderProps {
  trackId: number;
  name: string;
  color: string;
  muted: boolean;
  solo: boolean;
}

export function TrackHeader({ trackId, name, color, muted, solo }: TrackHeaderProps) {
  return (
    <div className={container}>
      <TrackColorDot color={color} size="sm" />
      <span className={nameLabel}>{name}</span>
      <div className={buttonsWrapper}>
        <MuteSoloButtons trackId={trackId} muted={muted} solo={solo} size="sm" />
      </div>
    </div>
  );
}

const container = `
  w-44 shrink-0 bg-[hsl(var(--background))]/80 border-r border-[hsl(var(--border))]
  flex items-center px-3 gap-2`;
const nameLabel = `text-xs text-[hsl(var(--foreground))] font-medium truncate flex-1`;
const buttonsWrapper = `opacity-0 group-hover:opacity-100 transition-opacity`;
