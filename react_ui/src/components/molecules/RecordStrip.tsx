import { useState, useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import { cn } from '@/lib/utils';
import { useMixerStore } from '@/data/store';
import type { Track, AudioSource, MidiInput } from '@/data/slices/mixer';
import { isPlugin, Juce } from '@/lib';

const listMidiInputs = isPlugin ? Juce.getNativeFunction('listMidiInputs') : null;
const listAudioInputs = isPlugin ? Juce.getNativeFunction('listAudioInputs') : null;

interface RecordStripCellProps {
  track: Track;
  trackList: Track[];
}

/* ── Portal Helper ─────────────────────────────────────────────── */
function PortaledDropdown({ open, onClose, triggerRef, className, children }: {
  open: boolean;
  onClose: () => void;
  triggerRef: React.RefObject<HTMLElement | null>;
  className?: string;
  children: React.ReactNode;
}) {
  const [coords, setCoords] = useState<{top: number | 'auto', bottom: number | 'auto', left: number, minWidth: number}>({ top: 0, bottom: 'auto', left: 0, minWidth: 0 });
  const menuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (open && triggerRef.current) {
      const rect = triggerRef.current.getBoundingClientRect();
      const spaceBelow = window.innerHeight - rect.bottom;
      if (spaceBelow < 250) {
        setCoords({
          top: 'auto',
          bottom: window.innerHeight - rect.top,
          left: Math.min(rect.left + window.scrollX, window.innerWidth - 180),
          minWidth: rect.width
        });
      } else {
        setCoords({
          top: rect.bottom + window.scrollY,
          bottom: 'auto',
          left: Math.min(rect.left + window.scrollX, window.innerWidth - 180),
          minWidth: rect.width
        });
      }
    }
  }, [open, triggerRef]);

  useEffect(() => {
    if (!open) return;
    const handleScroll = (e: Event) => {
      // ignore scrolling inside the menu itself
      if (menuRef.current?.contains(e.target as Node)) return;
      onClose();
    };
    const handleClickOutside = (e: MouseEvent) => {
      if (
        triggerRef.current && !triggerRef.current.contains(e.target as Node) &&
        menuRef.current && !menuRef.current.contains(e.target as Node)
      ) {
        onClose();
      }
    };
    window.addEventListener('scroll', handleScroll, { passive: true, capture: true });
    window.addEventListener('mousedown', handleClickOutside);
    return () => {
      window.removeEventListener('scroll', handleScroll, { capture: true });
      window.removeEventListener('mousedown', handleClickOutside);
    };
  }, [open, onClose, triggerRef]);

  if (!open) return null;

  return createPortal(
    <div 
      ref={menuRef}
      className={className} 
      style={{
        position: 'absolute',
        top: coords.top === 'auto' ? 'auto' : (coords.top as number) + 2,
        bottom: coords.bottom === 'auto' ? 'auto' : (coords.bottom as number) + 2,
        left: coords.left,
        minWidth: Math.max(coords.minWidth, 120),
        margin: 0,
        zIndex: 9999
      }}
    >
      {children}
    </div>,
    document.body
  );
}

/* ── MIDI Channel Selector ─────────────────────────────────────── */
function MidiChannelSelector({ track }: { track: Track }) {
  const [open, setOpen] = useState(false);
  const triggerRef = useRef<HTMLButtonElement>(null);
  const current = track.midiChannel; // null = All

  const select = (ch: number | null) => {
    useMixerStore.getState().setMidiChannel(track.id, ch);
    setOpen(false);
  };

  return (
    <div className="relative">
      <button
        ref={triggerRef}
        className={channelSelectBtn}
        onClick={() => setOpen(!open)}
        title="MIDI channel filter"
      >
        <span className="text-[7px] uppercase tracking-wider opacity-60">Ch</span>
        <span className="flex-1 text-left">{current ? current : 'All'}</span>
        <svg width="5" height="5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3">
          <polyline points="6 9 12 15 18 9" />
        </svg>
      </button>
      <PortaledDropdown open={open} onClose={() => setOpen(false)} triggerRef={triggerRef} className={channelDropdown}>
        <div
          className={cn(channelOption, !current && channelOptionActive)}
          onClick={() => select(null)}
        >
          All
        </div>
        {Array.from({ length: 16 }, (_, i) => i + 1).map((ch) => (
          <div
            key={ch}
            className={cn(channelOption, current === ch && channelOptionActive)}
            onClick={() => select(ch)}
          >
            {ch}
          </div>
        ))}
      </PortaledDropdown>
    </div>
  );
}

const getAudioDeviceInfo = isPlugin ? Juce.getNativeFunction('getAudioDeviceInfo') : null;

/* ── Audio Channel Selector ────────────────────────────────────── */
function AudioChannelSelector({ track }: { track: Track }) {
  const [open, setOpen] = useState(false);
  const triggerRef = useRef<HTMLButtonElement>(null);
  const channels = track.audioSource?.channels;

  const [inputChannelNames, setInputChannelNames] = useState<string[]>([]);
  useEffect(() => {
    if (open) {
      if (getAudioDeviceInfo) {
        getAudioDeviceInfo()
          .then((raw: unknown) => {
            const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
            if (parsed && Array.isArray(parsed.inputChannels)) {
              setInputChannelNames(parsed.inputChannels);
            }
          })
          .catch(() => {});
      }
    }
  }, [open]);

  // Display label: show channel pair like "1-2" (or real names if available), or "All" if not set
  let label = '1-2';
  if (channels && channels.length > 0) {
    if (inputChannelNames.length > 0) {
      label = channels.map(c => inputChannelNames[c] || `Ch ${c + 1}`).join(' + ');
    } else {
      label = channels.map(c => c + 1).join('-');
    }
  }

  const select = (chs: number[]) => {
    useMixerStore.getState().setAudioInputChannels(track.id, chs);
    setOpen(false);
  };

  // Generate pairs and mono based on actual device channels (fallback to 16 if undetermined yet)
  const numChans = inputChannelNames.length > 0 ? inputChannelNames.length : 16;
  
  const pairs: { label: string; channels: number[] }[] = [];
  for (let i = 0; i < numChans - 1; i += 2) {
    const l1 = inputChannelNames[i] || `${i + 1}`;
    const l2 = inputChannelNames[i + 1] || `${i + 2}`;
    pairs.push({ label: `${l1} + ${l2}`, channels: [i, i + 1] });
  }

  const monos: { label: string; channels: number[] }[] = [];
  for (let i = 0; i < numChans; i++) {
    monos.push({ label: inputChannelNames[i] || `${i + 1}`, channels: [i] });
  }

  return (
    <div className="relative">
      <button
        ref={triggerRef}
        className={channelSelectBtn}
        onClick={() => setOpen(!open)}
        title="Audio input channels"
      >
        <span className="text-[7px] uppercase tracking-wider opacity-60">Ch</span>
        <span className="flex-1 text-left truncate">{label}</span>
        <svg width="5" height="5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" className="shrink-0">
          <polyline points="6 9 12 15 18 9" />
        </svg>
      </button>
      <PortaledDropdown open={open} onClose={() => setOpen(false)} triggerRef={triggerRef} className={channelDropdown}>
        <div className="text-[7px] uppercase tracking-wider text-[hsl(var(--muted-foreground))]/60 px-1.5 pt-1 pb-0.5">Stereo</div>
        {pairs.map((p) => (
          <div
            key={p.label}
            className={cn(channelOption,
              channels && channels.length === 2 && channels[0] === p.channels[0] && channelOptionActive
            )}
            onClick={() => select(p.channels)}
          >
            {p.label}
          </div>
        ))}
        <div className="text-[7px] uppercase tracking-wider text-[hsl(var(--muted-foreground))]/60 px-1.5 pt-1.5 pb-0.5 border-t border-[hsl(var(--border))]/30">Mono</div>
        <div className="grid grid-cols-2 gap-0">
          {monos.map((m) => (
            <div
              key={m.label}
              className={cn(channelOption, 'truncate',
                channels && channels.length === 1 && channels[0] === m.channels[0] && channelOptionActive
              )}
              title={m.label}
              onClick={() => select(m.channels)}
            >
              {m.label}
            </div>
          ))}
        </div>
      </PortaledDropdown>
    </div>
  );
}

/* ── MIDI Input Cell ───────────────────────────────────────────── */
function MidiInputCell({ track }: RecordStripCellProps) {
  const [open, setOpen] = useState(false);
  const triggerRef = useRef<HTMLButtonElement>(null);
  const [devices, setDevices] = useState<string[]>([]);

  useEffect(() => {
    if (open) {
      listMidiInputs?.()
        .then((result: unknown) => {
          const parsed = typeof result === 'string' ? JSON.parse(result) : result;
          if (Array.isArray(parsed)) setDevices(parsed);
        })
        .catch(() => {});
    }
  }, [open]);

  const currentInput = track.midiInput;
  const inputLabel = currentInput === 'all'
    ? 'All Inputs'
    : currentInput === 'computer-keyboard'
      ? 'Computer KB'
      : currentInput || 'All Inputs';

  const selectInput = (input: MidiInput | null) => {
    useMixerStore.getState().setMidiInput(track.id, input);
    setOpen(false);
  };

  return (
    <div className={cellWrapper}>
      {/* MIDI input selector */}
      <div className={inputRow}>
        <button
          ref={triggerRef}
          className={inputSelectBtn}
          onClick={() => setOpen(!open)}
          title="Select MIDI input"
        >
          <svg width="8" height="8" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round">
            <circle cx="12" cy="12" r="9" />
            <circle cx="9" cy="10.5" r="1.5" fill="currentColor" stroke="none" />
            <circle cx="15" cy="10.5" r="1.5" fill="currentColor" stroke="none" />
            <circle cx="12" cy="15.5" r="1.5" fill="currentColor" stroke="none" />
          </svg>
          <span className="truncate flex-1 text-left">{inputLabel}</span>
          <svg width="6" height="6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3">
            <polyline points="6 9 12 15 18 9" />
          </svg>
        </button>
      </div>
      {/* MIDI channel selector */}
      <MidiChannelSelector track={track} />

      <PortaledDropdown open={open} onClose={() => setOpen(false)} triggerRef={triggerRef} className={dropdownMenu}>
        <div
          className={cn(dropdownOption, (!currentInput || currentInput === 'all') && dropdownOptionActive)}
          onClick={() => selectInput('all')}
        >
          All Inputs
        </div>
        <div
          className={cn(dropdownOption, currentInput === 'computer-keyboard' && dropdownOptionActive)}
          onClick={() => selectInput('computer-keyboard')}
        >
          ⌨️ Computer Keyboard
        </div>
        {devices.map((name) => (
          <div
            key={name}
            className={cn(dropdownOption, currentInput === name && dropdownOptionActive)}
            onClick={() => selectInput(name)}
          >
            🎹 {name}
          </div>
        ))}
      </PortaledDropdown>

      {/* Record & Monitor buttons */}
      <div className={btnRow}>
        <button
          className={cn(recBtn, track.recordArmed && recBtnActive)}
          onClick={() => useMixerStore.getState().setMidiRecordArm(track.id, !track.recordArmed)}
          title={track.recordArmed ? 'Disarm recording' : 'Arm recording'}
        >
          ●
        </button>
        <button
          className={cn(monBtn, track.inputMonitoring && monBtnActive)}
          onClick={() => useMixerStore.getState().toggleInputMonitoring(track.id)}
          title={track.inputMonitoring ? 'Disable monitoring' : 'Enable monitoring'}
        >
          <svg width="9" height="9" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
            <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5" />
            <path d="M15.54 8.46a5 5 0 0 1 0 7.07" />
          </svg>
        </button>
      </div>
    </div>
  );
}

/* ── Audio Input Cell ──────────────────────────────────────────── */
function AudioInputCell({ track, trackList }: RecordStripCellProps) {
  const [open, setOpen] = useState(false);
  const triggerRef = useRef<HTMLButtonElement>(null);
  const [availableInputs, setAvailableInputs] = useState<string[]>([]);

  useEffect(() => {
    if (open) {
      listAudioInputs?.()
        .then((result: unknown) => {
          const parsed = typeof result === 'string' ? JSON.parse(result) : result;
          if (Array.isArray(parsed)) setAvailableInputs(parsed);
        })
        .catch(() => {});
    }
  }, [open]);

  const audioSource = track.audioSource;
  const inputLabel = audioSource?.type === 'hardware'
    ? (audioSource.deviceName || 'Hardware')
    : audioSource?.type === 'loopback'
      ? `Track ${(audioSource.sourceTrackId ?? 0) + 1}`
      : 'No Input';

  const selectSource = (source: AudioSource | null) => {
    useMixerStore.getState().setAudioSource(track.id, source);
    setOpen(false);
  };

  return (
    <div className={cellWrapper}>
      {/* Audio input selector */}
      <div className={inputRow}>
        <button
          ref={triggerRef}
          className={inputSelectBtn}
          onClick={() => setOpen(!open)}
          title="Select audio input"
        >
          <svg width="8" height="8" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
            <path d="M12 1a3 3 0 0 0-3 3v8a3 3 0 0 0 6 0V4a3 3 0 0 0-3-3z" />
            <path d="M19 10v2a7 7 0 0 1-14 0v-2" />
            <line x1="12" y1="19" x2="12" y2="23" />
          </svg>
          <span className="truncate flex-1 text-left">{inputLabel}</span>
          <svg width="6" height="6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3">
            <polyline points="6 9 12 15 18 9" />
          </svg>
        </button>
      </div>
      {/* Audio channel selector */}
      {audioSource && <AudioChannelSelector track={track} />}

      <PortaledDropdown open={open} onClose={() => setOpen(false)} triggerRef={triggerRef} className={dropdownMenu}>
        <div
          className={cn(dropdownOption, !audioSource && dropdownOptionActive)}
          onClick={() => selectSource(null)}
        >
          No Input
        </div>
        {availableInputs.map((name) => (
          <div
            key={name}
            className={cn(dropdownOption, audioSource?.type === 'hardware' && audioSource.deviceName === name && dropdownOptionActive)}
            onClick={() => selectSource({ type: 'hardware', deviceName: name })}
          >
            🎤 {name}
          </div>
        ))}
        {trackList.filter(t => t.id !== track.id && !t.isReturn && !t.isMaster).map(t => (
          <div
            key={`lb-${t.id}`}
            className={cn(dropdownOption, audioSource?.type === 'loopback' && audioSource.sourceTrackId === t.id && dropdownOptionActive)}
            onClick={() => selectSource({ type: 'loopback', sourceTrackId: t.id })}
          >
            🔁 {t.name}
          </div>
        ))}
      </PortaledDropdown>

      {/* Record & Monitor buttons */}
      <div className={btnRow}>
        <button
          className={cn(recBtn, track.recordArmed && recBtnActive)}
          onClick={() => useMixerStore.getState().setAudioRecordArm(track.id, !track.recordArmed)}
          title={track.recordArmed ? 'Disarm recording' : 'Arm recording'}
        >
          ●
        </button>
        <button
          className={cn(monBtn, track.inputMonitoring && monBtnActive)}
          onClick={() => useMixerStore.getState().toggleInputMonitoring(track.id)}
          title={track.inputMonitoring ? 'Disable monitoring' : 'Enable monitoring'}
        >
          <svg width="9" height="9" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
            <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5" />
            <path d="M15.54 8.46a5 5 0 0 1 0 7.07" />
          </svg>
        </button>
      </div>
    </div>
  );
}

/* ── Record Strip Container ────────────────────────────────────── */
interface RecordStripProps {
  tracks: Track[];
}

export function RecordStrip({ tracks }: RecordStripProps) {
  return (
    <div className={stripContainer}>
      {tracks.map((track) => (
        <div key={track.id} className={cellOuter}>
          {track.type === 'midi'
            ? <MidiInputCell track={track} trackList={tracks} />
            : <AudioInputCell track={track} trackList={tracks} />
          }
        </div>
      ))}
    </div>
  );
}

// --- Styles ---
const stripContainer = `flex`;

const cellOuter = `w-28 shrink-0 flex items-end justify-center`;

const cellWrapper = `
  relative bg-[hsl(var(--mixer))] border border-[hsl(var(--border))] rounded-md shadow-xl
  p-1.5 w-[85%] flex flex-col gap-1`;

const inputRow = `flex items-center gap-0.5`;

const inputSelectBtn = `
  flex-1 flex items-center gap-1 px-1.5 py-1 rounded text-[8px] font-medium
  bg-[hsl(var(--muted))]/40 hover:bg-[hsl(var(--muted))]/70
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  transition-colors cursor-pointer border border-[hsl(var(--border))]/30
  min-w-0`;

const channelSelectBtn = `
  w-full flex items-center gap-1 px-1.5 py-0.5 rounded text-[8px] font-medium
  bg-[hsl(var(--muted))]/30 hover:bg-[hsl(var(--muted))]/60
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  transition-colors cursor-pointer border border-[hsl(var(--border))]/20`;

const channelDropdown = `
  bg-[hsl(var(--background))] border border-[hsl(var(--border))]
  rounded-md shadow-xl max-h-48 overflow-y-auto`;

const channelOption = `
  px-1.5 py-1 text-[8px] cursor-pointer
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  hover:bg-[hsl(var(--muted))]/50 transition-colors`;

const channelOptionActive = `
  bg-[hsl(var(--primary))]/10 text-[hsl(var(--primary))]`;

const btnRow = `flex items-center justify-center gap-1`;

const recBtn = `
  w-5 h-5 rounded flex items-center justify-center text-[10px]
  text-[hsl(var(--muted-foreground))] hover:text-red-400
  transition-colors cursor-pointer`;

const recBtnActive = `text-red-500 animate-pulse`;

const monBtn = `
  w-5 h-5 rounded flex items-center justify-center
  text-[hsl(var(--muted-foreground))] hover:text-amber-400
  transition-colors cursor-pointer`;

const monBtnActive = `text-amber-400`;

const dropdownMenu = `
  bg-[hsl(var(--background))] border border-[hsl(var(--border))]
  rounded-md shadow-xl max-h-40 overflow-y-auto`;

const dropdownOption = `
  px-2 py-1.5 text-[9px] cursor-pointer
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  hover:bg-[hsl(var(--muted))]/50 transition-colors`;

const dropdownOptionActive = `
  bg-[hsl(var(--primary))]/10 text-[hsl(var(--primary))]`;
