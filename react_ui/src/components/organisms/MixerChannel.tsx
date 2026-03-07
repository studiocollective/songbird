import { cn } from '@/lib/utils';
import { TrackColorDot, MuteSoloButtons, LevelMeter, VolumeFader, PanControl, PluginSlots } from '@/components/molecules';
import { useMeterStore } from '@/data/meters';
import type { PluginSlot, Track, AudioSource } from '@/data/slices/mixer';

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
  fx: PluginSlot;
  channelStrip: PluginSlot;
  isReturn?: boolean;
  isMaster?: boolean;
  sidechainTrackId?: number | null;
  trackList?: Track[];
  sidechainSensitivity?: number;
  audioSource?: AudioSource | null;
  recordArmed?: boolean;
}

export function MixerChannel({
  trackId, trackIndex, name, trackType, color, muted, solo, volume, pan, instrument, fx, channelStrip, isReturn = false, isMaster = false, sidechainTrackId, trackList, sidechainSensitivity, recordArmed
}: MixerChannelProps) {
  const level = useMeterStore((s) => {
    if (isMaster) return Math.max(s.master.left, s.master.right);
    const ch = s.levels[trackIndex];
    return ch ? Math.max(ch.left, ch.right) : 0;
  });

  return (
    <div className={cn(channel, isReturn && channelReturn)}>

      <div className={header}>
        <TrackColorDot color={color} size="md" />
        <span className={trackName}>{name}</span>
        <span className={cn(badge, (isReturn || trackType === 'audio') ? badgeAudio : badgeMidi, isMaster && 'bg-emerald-500/20 text-emerald-300')}>
          {isMaster ? 'master' : (isReturn ? 'return' : trackType)}
        </span>
        {recordArmed && (
          <span className="text-[7px] text-red-500 animate-pulse ml-0.5">●</span>
        )}
      </div>

      <PluginSlots trackId={trackId} trackType={(isReturn || isMaster) ? 'audio' : trackType} instrument={(isReturn || isMaster) ? { pluginId: '', pluginName: '', bypassed: false } : instrument} fx={fx} channelStrip={channelStrip} isMaster={isMaster} sidechainTrackId={sidechainTrackId} sidechainSensitivity={sidechainSensitivity} trackList={trackList} />

      <div className={msWrapper}>
        {!isMaster && <MuteSoloButtons trackId={trackId} muted={muted} solo={solo} size="md" />}
      </div>

      <div className={faderArea}>
        <LevelMeter level={level} volume={volume} color={color} height="h-20" muted={muted} />
        <VolumeFader trackId={trackId} value={volume} color={color} height="h-20" />
      </div>

      {!isMaster && (
        <div className={panWrapper}>
          <PanControl trackId={trackId} value={pan} color={color} />
        </div>
      )}

      <div className={volumeReadout}>{volume}</div>
    </div>
  );
}

const channel = `
  relative w-28 shrink-0 border-r border-[hsl(var(--border))]/50
  flex flex-col items-center py-2
  hover:bg-[hsl(var(--mixer-channel-hover))] transition-colors`;

const channelReturn = `w-20`;

const header = `flex items-center gap-1 mb-1`;
const trackName = `text-[10px] text-[hsl(var(--muted-foreground))] font-medium truncate max-w-[50px]`;
const badge = `text-[7px] uppercase tracking-wider px-1 rounded`;
const badgeMidi = `bg-[hsl(var(--badge-midi-bg))]/20 text-[hsl(var(--badge-midi-bg))]`;
const badgeAudio = `bg-[hsl(var(--badge-audio-bg))]/20 text-[hsl(var(--badge-audio-bg))]`;

const msWrapper = `mb-1`;
const faderArea = `flex-1 flex items-center gap-1.5`;
const panWrapper = `mt-1`;
const volumeReadout = `text-[9px] font-mono text-[hsl(var(--muted-foreground))] mt-0.5`;


