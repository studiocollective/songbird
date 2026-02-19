import { cn } from '@/lib/utils';
import { TrackColorDot, MuteSoloButtons, LevelMeter, VolumeFader, PanControl, PluginSlots } from '@/components/molecules';
import { useMeterStore } from '@/data/meters';
import type { PluginSlot } from '@/data/slices/mixer';

interface MixerChannelProps {
  trackId: number;
  trackIndex: number;
  name: string;
  trackType: 'midi' | 'audio';
  color: string;
  muted: boolean;
  solo: boolean;
  volume: number;
  pan: number;
  instrument: PluginSlot;
  channelStrip: PluginSlot;
}

export function MixerChannel({
  trackId, trackIndex, name, trackType, color, muted, solo, volume, pan, instrument, channelStrip,
}: MixerChannelProps) {
  const level = useMeterStore((s) => {
    const ch = s.levels[trackIndex];
    return ch ? Math.max(ch.left, ch.right) : 0;
  });

  return (
    <div className={channel}>
      <div className={header}>
        <TrackColorDot color={color} size="md" />
        <span className={trackName}>{name}</span>
        <span className={cn(badge, trackType === 'midi' ? badgeMidi : badgeAudio)}>
          {trackType}
        </span>
      </div>

      <PluginSlots trackId={trackId} trackType={trackType} instrument={instrument} channelStrip={channelStrip} />

      <div className={msWrapper}>
        <MuteSoloButtons trackId={trackId} muted={muted} solo={solo} size="md" />
      </div>

      <div className={faderArea}>
        <LevelMeter level={level} volume={volume} color={color} height="h-20" muted={muted} />
        <VolumeFader trackId={trackId} value={volume} color={color} height="h-20" />
      </div>

      <div className={panWrapper}>
        <PanControl trackId={trackId} value={pan} color={color} />
      </div>

      <div className={volumeReadout}>{volume}</div>
    </div>
  );
}

const channel = `
  w-28 shrink-0 border-r border-[hsl(var(--border))]/50
  flex flex-col items-center py-2
  hover:bg-[hsl(var(--mixer-channel-hover))] transition-colors`;

const header = `flex items-center gap-1 mb-1`;
const trackName = `text-[10px] text-[hsl(var(--muted-foreground))] font-medium truncate max-w-[50px]`;
const badge = `text-[7px] uppercase tracking-wider px-1 rounded`;
const badgeMidi = `bg-[hsl(var(--badge-midi-bg))]/20 text-[hsl(var(--badge-midi-bg))]`;
const badgeAudio = `bg-[hsl(var(--badge-audio-bg))]/20 text-[hsl(var(--badge-audio-bg))]`;

const msWrapper = `mb-1`;
const faderArea = `flex-1 flex items-center gap-1.5`;
const panWrapper = `mt-1`;
const volumeReadout = `text-[9px] font-mono text-[hsl(var(--muted-foreground))] mt-0.5`;
