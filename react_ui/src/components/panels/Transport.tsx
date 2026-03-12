import { useState, useRef, useEffect, useCallback } from 'react';
import { useTransportStore, useMixerStore, useChatStore } from '@/data/store';
import { cn } from '@/lib/utils';
import { Juce, isPlugin } from '@/lib';
import { nativeFunction } from '@/data/bridge';
import { CpuMeter } from '@/components/molecules/CpuMeter';

const setProjectScale = nativeFunction('setProjectScale');

export function Transport({ onSettingsOpen }: { onSettingsOpen: () => void }) {
  const { playing, togglePlaying, stop, bpm, setBpm, currentBar, looping, toggleLooping, keySignature, scale, setScale } = useTransportStore();
  const { toggleMixer, mixerOpen, keyboardMidiMode, toggleKeyboardMidiMode } = useMixerStore();
  const { rightPanel, setRightPanel } = useChatStore();

  const [exportMenuOpen, setExportMenuOpen] = useState(false);
  const exportMenuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    function handleClickOutside(event: MouseEvent) {
      if (exportMenuRef.current && !exportMenuRef.current.contains(event.target as Node)) {
        setExportMenuOpen(false);
      }
    }
    document.addEventListener("mousedown", handleClickOutside);
    return () => document.removeEventListener("mousedown", handleClickOutside);
  }, [exportMenuRef]);

  const handleExportStems = (includeReturnFx: boolean) => {
    setExportMenuOpen(false);
    if (isPlugin) Juce.getNativeFunction('exportStems')?.(includeReturnFx);
  };

  const handleExportMaster = () => {
    setExportMenuOpen(false);
    if (isPlugin) Juce.getNativeFunction('exportMaster')?.();
  };

  const handleExportMidi = () => {
    setExportMenuOpen(false);
    if (isPlugin) Juce.getNativeFunction('exportSheetMusic')?.();
  };

  const handleScaleChange = useCallback((newScale: { root: string; mode: string } | null) => {
    setScale(newScale);
    setProjectScale(newScale?.root ?? '', newScale?.mode ?? '');
  }, [setScale]);

  return (
    <div className={bar}>
      {/* Transport controls */}
      <div className={controlGroup}>
        <button onClick={stop} className={controlBtn} title="Stop">
          <svg width="12" height="12" viewBox="0 0 12 12" fill="currentColor">
            <rect width="12" height="12" rx="1" />
          </svg>
        </button>
        <button
          onClick={togglePlaying}
          className={cn(controlBtn, playing && playingBtn)}
          title={playing ? 'Pause' : 'Play'}
        >
          {playing ? (
            <svg width="12" height="14" viewBox="0 0 12 14" fill="currentColor">
              <rect width="4" height="14" rx="1" />
              <rect x="8" width="4" height="14" rx="1" />
            </svg>
          ) : (
            <svg width="12" height="14" viewBox="0 0 12 14" fill="currentColor">
              <path d="M0 0L12 7L0 14V0Z" />
            </svg>
          )}
        </button>
        <button className={controlBtn} title="Record">
          <div className={recordDot} />
        </button>
        <button
          onClick={toggleLooping}
          className={cn(controlBtn, looping && loopingBtn)}
          title={looping ? 'Loop On' : 'Loop Off'}
        >
          <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="1.5">
            <path d="M11 4H3.5C2.1 4 1 5.1 1 6.5S2.1 9 3.5 9H10.5C11.9 9 13 7.9 13 6.5S11.9 4 10.5 4" />
            <path d="M9 2l2 2-2 2" fill="currentColor" stroke="none" />
          </svg>
        </button>
      </div>

      <div className={divider} />

      {/* BPM & Key */}
      <div className={controlGroup}>
        <span className={dimLabel}>BPM</span>
        <input
          type="number"
          value={bpm}
          onChange={(e) => setBpm(Number(e.target.value))}
          className={bpmInput}
        />
        {keySignature && (
          <>
            <div className={`ml-2 mr-2 ${divider}`} />
            <span className={dimLabel}>Key</span>
            <span className={keyText}>{keySignature}</span>
          </>
        )}
        <div className={`ml-2 mr-2 ${divider}`} />
        <span className={dimLabel}>Scale</span>
        <select
          value={scale?.root ?? ''}
          onChange={(e) => {
            const root = e.target.value;
            if (!root) { handleScaleChange(null); return; }
            handleScaleChange({ root, mode: scale?.mode ?? 'ionian' });
          }}
          className={scaleSelect}
        >
          <option value="">—</option>
          {['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'].map((n) => (
            <option key={n} value={n}>{n}</option>
          ))}
        </select>
        <select
          value={scale?.mode ?? ''}
          onChange={(e) => {
            const mode = e.target.value;
            if (!mode || !scale) return;
            handleScaleChange({ ...scale, mode });
          }}
          className={scaleSelect}
          disabled={!scale}
        >
          {['ionian','dorian','phrygian','lydian','mixolydian','aeolian','locrian'].map((m) => (
            <option key={m} value={m}>{m.charAt(0).toUpperCase() + m.slice(1)}</option>
          ))}
        </select>
      </div>

      <div className={divider} />

      {/* Position display */}
      <div className={positionGroup}>
        <div className={positionText}>
          <span className={dimText}>Bar </span>
          <span className={accentText}>{currentBar}</span>
        </div>
      </div>

      <div className="flex-1" />
      <CpuMeter />
      <div className="flex-1" />

      {/* Panel toggles */}
      <div className={controlGroup}>
        <div className="relative" ref={exportMenuRef}>
          <button
            onClick={() => setExportMenuOpen(!exportMenuOpen)}
            className={cn(panelBtn, exportMenuOpen ? panelBtnActive : panelBtnInactive)}
            title="Export Options"
          >
            Export
          </button>
          
          {exportMenuOpen && (
            <div className={`absolute top-full right-0 mt-2 w-48 bg-[hsl(var(--card))] border border-[hsl(var(--border))] rounded-md shadow-lg overflow-hidden z-50`}>
              <div className="py-1">
                <button
                  onClick={() => handleExportStems(false)}
                  className="w-full text-left px-4 py-2 text-sm text-[hsl(var(--foreground))] hover:bg-[hsl(var(--muted))]"
                >
                  Export Stems (Dry)
                </button>
                <button
                  onClick={() => handleExportStems(true)}
                  className="w-full text-left px-4 py-2 text-sm text-[hsl(var(--foreground))] hover:bg-[hsl(var(--muted))]"
                >
                  Export Stems (+ Return FX)
                </button>
                <button
                  onClick={handleExportMaster}
                  className="w-full text-left px-4 py-2 text-sm text-[hsl(var(--foreground))] hover:bg-[hsl(var(--muted))]"
                >
                  Export Master
                </button>
                <div className="border-t border-[hsl(var(--border))] my-1"></div>
                <button
                  onClick={handleExportMidi}
                  className="w-full text-left px-4 py-2 text-sm text-[hsl(var(--foreground))] hover:bg-[hsl(var(--muted))]"
                >
                  Export MIDI (Sheet Music)
                </button>
              </div>
            </div>
          )}
        </div>
        <button
          onClick={toggleMixer}
          className={cn(panelBtn, mixerOpen ? panelBtnActive : panelBtnInactive)}
        >
          Mixer
        </button>
        <button
          onClick={() => setRightPanel('chat')}
          className={cn(panelBtn, rightPanel === 'chat' ? panelBtnActive : panelBtnInactive)}
        >
          Chat
        </button>
        <button
          onClick={() => setRightPanel('history')}
          className={cn(panelBtn, rightPanel === 'history' ? panelBtnActive : panelBtnInactive)}
        >
          History
        </button>
        <button
          onClick={() => setRightPanel('bird')}
          className={cn(panelBtn, rightPanel === 'bird' ? panelBtnActive : panelBtnInactive)}
        >
          Bird
        </button>
        <div className={`ml-1 ${divider}`} />
        <button
          onClick={toggleKeyboardMidiMode}
          className={cn(controlBtn, 'ml-1', keyboardMidiMode && 'bg-[hsl(var(--selection))] text-[hsl(var(--primary-foreground))]')}
          title={keyboardMidiMode ? 'Keyboard: MIDI input (click to switch to shortcuts)' : 'Keyboard: Shortcuts (click to switch to MIDI input)'}
        >
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
            <rect x="2" y="6" width="20" height="12" rx="2" />
            <line x1="6" y1="10" x2="6" y2="10.01" />
            <line x1="10" y1="10" x2="10" y2="10.01" />
            <line x1="14" y1="10" x2="14" y2="10.01" />
            <line x1="18" y1="10" x2="18" y2="10.01" />
            <line x1="7" y1="14" x2="17" y2="14" />
          </svg>
        </button>
        <button
          onClick={onSettingsOpen}
          className={cn(controlBtn, 'ml-1')}
          title="Settings"
        >
          <svg width="15" height="15" viewBox="0 0 15 15" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round">
            <circle cx="7.5" cy="7.5" r="2.2" />
            <path d="M6.3 1.5h2.4l.3 1.5.9.4 1.3-.8 1.7 1.7-.8 1.3.4.9 1.5.3v2.4l-1.5.3-.4.9.8 1.3-1.7 1.7-1.3-.8-.9.4-.3 1.5H6.3l-.3-1.5-.9-.4-1.3.8-1.7-1.7.8-1.3-.4-.9-1.5-.3V6.3l1.5-.3.4-.9-.8-1.3 1.7-1.7 1.3.8.9-.4z" />
          </svg>
        </button>
      </div>
    </div>
  );
}

const bar = `
  h-12 bg-[hsl(var(--transport))] border-b border-[hsl(var(--border))]
  flex items-center px-4 gap-3 select-none shrink-0`;

const controlGroup = `flex items-center gap-1`;
const controlBtn = `
  w-8 h-8 rounded flex items-center justify-center
  hover:bg-[hsl(var(--card))] text-[hsl(var(--muted-foreground))]
  hover:text-[hsl(var(--foreground))] transition-colors`;
const playingBtn = `bg-[hsl(var(--progress))] text-[hsl(var(--primary-foreground))]`;
const loopingBtn = `bg-[hsl(var(--selection))] text-[hsl(var(--primary-foreground))]`;
const recordDot = `w-3 h-3 rounded-full bg-[hsl(var(--destructive))]`;

const divider = `w-px h-6 bg-[hsl(var(--border))]`;

const dimLabel = `text-[10px] text-[hsl(var(--muted-foreground))] uppercase tracking-wider`;
const bpmInput = `
  w-14 h-7 bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  rounded text-center text-sm text-[hsl(var(--foreground))] font-mono
  focus:outline-none focus:border-[hsl(var(--ring))]`;
const keyText = `
  px-2 h-7 bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  rounded flex items-center justify-center text-sm text-[hsl(var(--foreground))] font-mono`;

const positionGroup = `flex items-center gap-3`;
const positionText = `text-sm font-mono text-[hsl(var(--foreground))]`;
const dimText = `text-[hsl(var(--muted-foreground))]`;
const accentText = `text-[hsl(var(--progress))]`;

const panelBtn = `px-3 h-7 rounded text-xs font-medium transition-colors`;
const panelBtnActive = `bg-[hsl(var(--muted))] text-[hsl(var(--foreground))]`;
const panelBtnInactive = `text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] hover:bg-[hsl(var(--card))]`;

const scaleSelect = `
  h-7 bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  rounded text-xs text-[hsl(var(--foreground))] font-mono px-1
  focus:outline-none focus:border-[hsl(var(--ring))]
  cursor-pointer appearance-none`;
