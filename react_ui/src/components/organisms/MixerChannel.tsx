import { useState, useEffect } from 'react';
import { cn } from '@/lib/utils';
import { TrackColorDot, MuteSoloButtons, LevelMeter, VolumeFader, PanControl, PluginSlots } from '@/components/molecules';
import { useMeterStore } from '@/data/meters';
import { useMixerStore } from '@/data/store';
import type { PluginSlot, Track, AudioSource } from '@/data/slices/mixer';
import { isPlugin, Juce } from '@/lib';

const listAudioInputs = isPlugin ? Juce.getNativeFunction('listAudioInputs') : null;

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
  trackId, trackIndex, name, trackType, color, muted, solo, volume, pan, instrument, fx, channelStrip, isReturn = false, isMaster = false, sidechainTrackId, trackList, sidechainSensitivity, audioSource, recordArmed
}: MixerChannelProps) {
  const level = useMeterStore((s) => {
    if (isMaster) return Math.max(s.master.left, s.master.right);
    const ch = s.levels[trackIndex];
    return ch ? Math.max(ch.left, ch.right) : 0;
  });

  const [inputsOpen, setInputsOpen] = useState(false);
  const [availableInputs, setAvailableInputs] = useState<string[]>([]);

  useEffect(() => {
    if (inputsOpen && trackType === 'audio') {
      listAudioInputs?.()
        .then((result: unknown) => {
          const parsed = typeof result === 'string' ? JSON.parse(result) : result;
          if (Array.isArray(parsed)) setAvailableInputs(parsed);
        })
        .catch(() => {});
    }
  }, [inputsOpen, trackType]);

  const isAudio = trackType === 'audio' && !isReturn && !isMaster;
  const inputLabel = audioSource?.type === 'hardware'
    ? (audioSource.deviceName || 'Hardware')
    : audioSource?.type === 'loopback'
      ? `Track ${(audioSource.sourceTrackId ?? 0) + 1}`
      : 'No Input';

  return (
    <div className={channel}>

      <div className={header}>
        <TrackColorDot color={color} size="md" />
        <span className={trackName}>{name}</span>
        <span className={cn(badge, (isReturn || trackType === 'audio') ? badgeAudio : badgeMidi, isMaster && 'bg-emerald-500/20 text-emerald-300')}>
          {isMaster ? 'master' : (isReturn ? 'return' : trackType)}
        </span>
      </div>

      {/* Audio input selector */}
      {isAudio && (
        <div className={inputSection}>
          <div className={inputRow}>
            <button
              className={inputSelect}
              onClick={() => setInputsOpen(!inputsOpen)}
              title="Select audio input"
            >
              <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <path d="M12 1a3 3 0 0 0-3 3v8a3 3 0 0 0 6 0V4a3 3 0 0 0-3-3z"/>
                <path d="M19 10v2a7 7 0 0 1-14 0v-2"/>
                <line x1="12" y1="19" x2="12" y2="23"/>
              </svg>
              <span className="truncate flex-1 text-left">{inputLabel}</span>
              <svg width="8" height="8" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3">
                <polyline points="6 9 12 15 18 9"/>
              </svg>
            </button>
            <button
              className={cn(recBtn, recordArmed && recBtnActive)}
              onClick={() => useMixerStore.getState().setAudioRecordArm(trackId, !recordArmed)}
              title={recordArmed ? 'Disarm recording' : 'Arm recording'}
            >
              ●
            </button>
          </div>

          {inputsOpen && (
            <div className={inputDropdown}>
              <div
                className={cn(inputOption, !audioSource && inputOptionActive)}
                onClick={() => {
                  useMixerStore.getState().setAudioSource(trackId, null);
                  setInputsOpen(false);
                }}
              >
                No Input
              </div>
              {availableInputs.map((name) => (
                <div
                  key={name}
                  className={cn(inputOption, audioSource?.type === 'hardware' && audioSource.deviceName === name && inputOptionActive)}
                  onClick={() => {
                    useMixerStore.getState().setAudioSource(trackId, { type: 'hardware', deviceName: name });
                    setInputsOpen(false);
                  }}
                >
                  🎤 {name}
                </div>
              ))}
              {trackList?.filter(t => t.id !== trackId && !t.isReturn && !t.isMaster).map(t => (
                <div
                  key={`lb-${t.id}`}
                  className={cn(inputOption, audioSource?.type === 'loopback' && audioSource.sourceTrackId === t.id && inputOptionActive)}
                  onClick={() => {
                    useMixerStore.getState().setAudioSource(trackId, { type: 'loopback', sourceTrackId: t.id });
                    setInputsOpen(false);
                  }}
                >
                  🔁 {t.name}
                </div>
              ))}
            </div>
          )}
        </div>
      )}

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

const header = `flex items-center gap-1 mb-1`;
const trackName = `text-[10px] text-[hsl(var(--muted-foreground))] font-medium truncate max-w-[50px]`;
const badge = `text-[7px] uppercase tracking-wider px-1 rounded`;
const badgeMidi = `bg-[hsl(var(--badge-midi-bg))]/20 text-[hsl(var(--badge-midi-bg))]`;
const badgeAudio = `bg-[hsl(var(--badge-audio-bg))]/20 text-[hsl(var(--badge-audio-bg))]`;

const msWrapper = `mb-1`;
const faderArea = `flex-1 flex items-center gap-1.5`;
const panWrapper = `mt-1`;
const volumeReadout = `text-[9px] font-mono text-[hsl(var(--muted-foreground))] mt-0.5`;

// --- Audio input selector styles ---
const inputSection = `w-full px-1.5 mb-1 relative`;

const inputRow = `flex items-center gap-0.5`;

const inputSelect = `
  flex-1 flex items-center gap-1 px-1.5 py-1 rounded text-[8px] font-medium
  bg-[hsl(var(--muted))]/40 hover:bg-[hsl(var(--muted))]/70
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  transition-colors cursor-pointer border border-[hsl(var(--border))]/30
  min-w-0`;

const recBtn = `
  w-5 h-5 rounded flex items-center justify-center text-[10px]
  text-[hsl(var(--muted-foreground))] hover:text-red-400
  transition-colors cursor-pointer`;

const recBtnActive = `
  text-red-500 animate-pulse`;

const inputDropdown = `
  absolute left-1.5 right-1.5 top-full z-50 mt-0.5
  bg-[hsl(var(--background))] border border-[hsl(var(--border))]
  rounded-md shadow-xl max-h-40 overflow-y-auto`;

const inputOption = `
  px-2 py-1.5 text-[9px] cursor-pointer
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  hover:bg-[hsl(var(--muted))]/50 transition-colors`;

const inputOptionActive = `
  bg-[hsl(var(--primary))]/10 text-[hsl(var(--primary))]`;
