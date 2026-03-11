import { useRef, useEffect } from 'react';
import { cn } from '@/lib/utils';
import { TrackColorDot, MuteSoloButtons, VolumeFader, PanControl, PluginSlots } from '@/components/molecules';
import { subscribeRtBuffer } from '@/data/meters';
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
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const ctxRef = useRef<CanvasRenderingContext2D | null>(null);

  // Get 2D context once on mount
  useEffect(() => {
    const canvas = canvasRef.current;
    if (canvas) ctxRef.current = canvas.getContext('2d');
  }, []);

  // Subscribe to 60Hz smoothed data — draw to canvas
  useEffect(() => {
    const unsub = subscribeRtBuffer((buf) => {
      const ctx = ctxRef.current;
      const canvas = canvasRef.current;
      if (!ctx || !canvas) return;

      let level: number;
      if (isMaster) {
        level = Math.max(buf.master.left, buf.master.right);
      } else {
        const ch = buf.levels[trackIndex];
        level = ch ? Math.max(ch.left, ch.right) : 0;
      }
      const displayLevel = muted ? 0 : level;

      const w = canvas.width;
      const h = canvas.height;
      ctx.clearRect(0, 0, w, h);

      // Volume fill (dim)
      const volH = (volume / 127) * h;
      ctx.globalAlpha = 0.1;
      ctx.fillStyle = color;
      ctx.fillRect(0, h - volH, w, volH);

      // Level fill (bright gradient)
      const levelH = (displayLevel / 100) * h;
      if (levelH > 0) {
        ctx.globalAlpha = 1;
        const grad = ctx.createLinearGradient(0, h, 0, h - levelH);
        grad.addColorStop(0, color + '80');
        grad.addColorStop(1, color + 'E6');
        ctx.fillStyle = grad;
        ctx.fillRect(0, h - levelH, w, levelH);
      }
    });
    return unsub;
  }, [trackIndex, isMaster, muted, volume, color]);

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
        <canvas
          ref={canvasRef}
          width={6}
          height={80}
          className="w-1.5 h-20 rounded-full"
          style={{ background: 'hsl(var(--card))' }}
        />
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
