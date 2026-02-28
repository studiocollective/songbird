import { useState, useRef, useEffect } from 'react';
import { useTransportStore, useMixerStore, useChatStore } from '@/data/store';
import { cn } from '@/lib/utils';
import { Juce, isPlugin } from '@/lib';

export function Transport() {
  const { playing, togglePlaying, stop, bpm, setBpm, currentBar, looping, toggleLooping, keySignature } = useTransportStore();
  const { toggleMixer, mixerOpen } = useMixerStore();
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
      <span className={title}>🐦 Songbird Player</span>
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


const title = `text-sm font-medium text-[hsl(var(--muted-foreground))] tracking-wide`;

const panelBtn = `px-3 h-7 rounded text-xs font-medium transition-colors`;
const panelBtnActive = `bg-[hsl(var(--muted))] text-[hsl(var(--foreground))]`;
const panelBtnInactive = `text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] hover:bg-[hsl(var(--card))]`;
