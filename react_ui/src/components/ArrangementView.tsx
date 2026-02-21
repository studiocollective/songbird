import { useRef, useEffect, useState } from 'react';
import { useMixerStore, useTransportStore } from '@/data/store';
import type { NoteData } from '@/data/slices/mixer';
import { Chord } from '@tonaljs/tonal';
import { AutomationOverlay } from './AutomationOverlay';

export function ArrangementView() {
  const { tracks, sections, totalBars: storeTotalBars } = useMixerStore();
  const { looping, loopBars } = useTransportStore();
  const [showAutomation, setShowAutomation] = useState(false);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      // Ignore if typing in an input
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) return;
      
      if (e.key.toLowerCase() === 'a' && !e.metaKey && !e.ctrlKey) {
        setShowAutomation((prev) => !prev);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);

  // Compute total bars: use store value (from arrangement) if available,
  // otherwise fall back to max note beat position
  const arrangementTracks = tracks.filter((t) => !t.isReturn && !t.isMaster);
  const maxBeat = arrangementTracks.reduce((max, track) => {
    const trackMax = track.notes.reduce((m, n) => Math.max(m, n.beat + n.duration), 0);
    return Math.max(max, trackMax);
  }, 4); // minimum 4 beats = 1 bar
  const totalBars = storeTotalBars > 1 ? storeTotalBars : Math.max(1, Math.ceil(maxBeat / 4));

  // Loop region (0 to loopBars)
  const loopEndPct = looping && loopBars > 0
    ? Math.min((loopBars / totalBars) * 100, 100)
    : 0;

  return (
    <div className={container}>
      {/* Timeline ruler */}
      <div className={rulerRow}>
        <div className={rulerSpacer} />
        <div className={rulerTrack}>
          {/* Loop region overlay on ruler */}
          {looping && loopEndPct > 0 && (
            <div
              className={loopRegionRuler}
              style={{ width: `${loopEndPct}%` }}
            />
          )}
          {/* Section labels in top half */}
          {sections.map((sec, i) => (
            <div
              key={i}
              className={sectionLabel}
              style={{
                '--label-left': `${(sec.start / totalBars) * 100}%`,
                '--label-width': `${(sec.length / totalBars) * 100}%`,
              } as React.CSSProperties}
            >
              {sec.name}
            </div>
          ))}
          {/* Bar numbers in bottom half */}
          {Array.from({ length: totalBars }, (_, i) => (
            <div
              key={i}
              className={barNumber}
              style={{
                '--bar-left': `${(i / totalBars) * 100}%`,
                '--bar-width': `${(1 / totalBars) * 100}%`,
              } as React.CSSProperties}
            >
              {i + 1}
            </div>
          ))}
          {/* Playhead removed from ruler as per request */}
        </div>
      </div>

      {/* Track lanes */}
      <div className={lanesScroll}>
        {arrangementTracks.map((track) => (
          <div key={track.id} className={laneRow}>
            <div className={laneHeader}>
              <div
                className={colorDot}
                style={{ '--dot-color': track.color } as React.CSSProperties}
              />
              <span className={trackName}>{track.name}</span>
              <div className={muteSoloGroup}>
                <button
                  onClick={() => useMixerStore.getState().toggleMute(track.id)}
                  className={`${muteSoloBtn} ${
                    track.muted ? muteBtnActive : muteSoloBtnInactive
                  }`}
                >
                  M
                </button>
                <button
                  onClick={() => useMixerStore.getState().toggleSolo(track.id)}
                  className={`${muteSoloBtn} ${
                    track.solo ? soloBtnActive : muteSoloBtnInactive
                  }`}
                >
                  S
                </button>
              </div>
            </div>

            <div className={laneContent}>
              {/* Section background bands */}
              {sections.map((sec, i) => (
                <div
                  key={`sec-${i}`}
                  className={`${sectionBg} ${sec.color}`}
                  style={{
                    '--sec-left': `${(sec.start / totalBars) * 100}%`,
                    '--sec-width': `${(sec.length / totalBars) * 100}%`,
                  } as React.CSSProperties}
                />
              ))}

              {/* Grid lines for each bar */}
              {Array.from({ length: totalBars }, (_, i) => (
                <div
                  key={i}
                  className={gridLine}
                  style={{
                    '--grid-left': `${(i / totalBars) * 100}%`,
                  } as React.CSSProperties}
                />
              ))}

              {/* MIDI notes mini piano-roll */}
              {track.notes.length > 0 && (
                <div className={`transition-opacity duration-300 ${showAutomation && track.automation && track.automation.length > 0 ? 'opacity-30' : 'opacity-100'} absolute inset-0`}>
                    <NoteClip
                      notes={track.notes}
                      color={track.color}
                      totalBars={totalBars}
                    />
                </div>
              )}

              {/* Automation Curves Layer */}
              {showAutomation && track.automation && track.automation.length > 0 && (
                <div className="absolute inset-0 z-30 pointer-events-none">
                    <AutomationOverlay 
                      automation={track.automation} 
                      totalBars={totalBars} 
                      color={track.color} 
                    />
                </div>
              )}

              {/* Playhead line on lane */}
              <Playhead totalBars={totalBars} className={playheadLane} />
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

// --- Animated Playhead Component (direct DOM for max smoothness) ---

function Playhead({
  totalBars,
  className,
}: {
  totalBars: number;
  className: string;
}) {
  const elRef = useRef<HTMLDivElement>(null);
  const rAF = useRef<number | null>(null);

  useEffect(() => {
    const el = elRef.current;
    if (!el) return;

    const update = () => {
      const state = useTransportStore.getState();
      let currentPos = state.position;

      if (state.playing) {
        const elapsed = (performance.now() - state.lastPositionUpdate) / 1000;
        currentPos += elapsed;
      }

      const currentBeat = currentPos * (state.bpm / 60);
      const pct = Math.min((currentBeat / (totalBars * 4)) * 100, 100);

      // Direct DOM write — no React re-render
      el.style.transform = `translateX(0) translateZ(0)`;
      el.style.left = `${pct}%`;

      if (state.playing) {
        rAF.current = requestAnimationFrame(update);
      }
    };

    // Subscribe to store changes to restart loop when position/playing changes
    const unsub = useTransportStore.subscribe((state, prev) => {
      if (
        state.position !== prev.position ||
        state.playing !== prev.playing ||
        state.bpm !== prev.bpm
      ) {
        if (rAF.current !== null) cancelAnimationFrame(rAF.current);
        rAF.current = requestAnimationFrame(update);
      }
    });

    // Kick off initial frame
    rAF.current = requestAnimationFrame(update);

    return () => {
      unsub();
      if (rAF.current !== null) cancelAnimationFrame(rAF.current);
    };
  }, [totalBars]);

  return <div ref={elRef} className={className} />;
}

// --- Mini piano-roll clip ---

function NoteClip({
  notes,
  color,
  totalBars,
}: {
  notes: NoteData[];
  color: string;
  totalBars: number;
}) {
  if (notes.length === 0) return null;

  const minPitch = notes.reduce((m, n) => Math.min(m, n.pitch), 127);
  const maxPitch = notes.reduce((m, n) => Math.max(m, n.pitch), 0);
  const pitchRange = Math.max(maxPitch - minPitch, 1);

  const minBeat = notes.reduce((m, n) => Math.min(m, n.beat), Infinity);
  const maxBeatEnd = notes.reduce((m, n) => Math.max(m, n.beat + n.duration), 0);
  const totalBeats = totalBars * 4;

  const clipLeft = (minBeat / totalBeats) * 100;
  const clipWidth = ((maxBeatEnd - minBeat) / totalBeats) * 100;

  // --- Auto-detect chords ---
  // Group notes that start at approximately the same time
  const chordGroups: { startBeat: number; endBeat: number; chordName: string }[] = [];
  // Use a looser tolerance because chords in Bird can be written as several notes
  // in the same slot but may have tiny floating point offsets
  const beatTolerance = 0.5;

  // Sort notes by start time
  const sortedNotes = [...notes].sort((a, b) => a.beat - b.beat);
  
  let currentGroup: NoteData[] = [];
  
  const MIDI_NOTE_NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];

  const processGroup = () => {
    if (currentGroup.length >= 3) {
      // Get unique pitch classes (0-11), not full MIDI numbers, so voicings across octaves collapse
      const pitchClasses = [...new Set(currentGroup.map(n => n.pitch % 12))].sort((a, b) => a - b);
      
      if (pitchClasses.length >= 3) {
        // Chord.detect needs note names WITHOUT octave numbers: 'C', 'E', 'G'
        const noteNames = pitchClasses.map(pc => MIDI_NOTE_NAMES[pc]);
        const detected = Chord.detect(noteNames);
        
        if (detected && detected.length > 0) {
          // Pick the shortest/simplest chord name as the best match
          const bestName = detected.reduce((a, b) => a.length <= b.length ? a : b);
          
          const groupMinBeat = Math.min(...currentGroup.map(n => n.beat));
          // Use note duration to determine the chord block width; don't let it collapse to 0
          const groupDuration = Math.max(...currentGroup.map(n => n.duration));
          const groupMaxBeat = groupMinBeat + groupDuration;
          
          chordGroups.push({
            startBeat: groupMinBeat,
            endBeat: groupMaxBeat,
            chordName: bestName
          });
        }
      }
    }
    currentGroup = [];
  };

  for (const note of sortedNotes) {
    if (currentGroup.length === 0) {
      currentGroup.push(note);
    } else {
      const groupStart = currentGroup[0].beat;
      if (Math.abs(note.beat - groupStart) <= beatTolerance) {
        currentGroup.push(note);
      } else {
        processGroup();
        currentGroup.push(note);
      }
    }
  }
  processGroup(); // process final group

  return (
    <div
      className={clip}
      style={{
        '--clip-left': `${clipLeft}%`,
        '--clip-width': `${clipWidth}%`,
        '--clip-color': color,
      } as React.CSSProperties}
    >
      <svg
        className={notesSvg}
        viewBox={`0 0 ${maxBeatEnd - minBeat} ${pitchRange + 1}`}
        preserveAspectRatio="none"
      >
        {notes.map((note, i) => {
          const x = note.beat - minBeat;
          const y = maxPitch - note.pitch;
          const w = note.duration;
          const opacity = 0.4 + (note.velocity / 127) * 0.6;
          return (
            <rect
              key={i}
              x={x}
              y={y}
              width={w}
              height={0.7}
              rx={0.1}
              fill="white"
              opacity={opacity}
            />
          );
        })}
      </svg>
      {/* Chord Overlays */}
      {chordGroups.map((group, i) => {
        const span = maxBeatEnd - minBeat;
        const xPct = span > 0 ? ((group.startBeat - minBeat) / span) * 100 : 0;
        const wPct = span > 0 ? ((group.endBeat - group.startBeat) / span) * 100 : 100;
        
        // Render a small text box centered horizontally over the chord
        return (
          <div
            key={`chord-${i}`}
            className={chordLabelContainer}
            style={{
              left: `${xPct}%`,
              width: `max(${wPct}%, 32px)`,
            }}
          >
            <span className={chordLabelText}>{group.chordName}</span>
          </div>
        );
      })}
    </div>
  );
}

// --- Styles ---

const container = `flex-1 flex flex-col overflow-hidden bg-[hsl(var(--arrangement))]`;

const rulerRow = `flex shrink-0`;
const rulerSpacer = `w-44 shrink-0 bg-[hsl(var(--background))] border-b border-r border-[hsl(var(--border))]`;
const rulerTrack = `
  flex-1 h-10 bg-[hsl(var(--background))] border-b border-[hsl(var(--border))]
  flex items-end relative overflow-hidden`;

const barNumber = `
  absolute bottom-0 h-5 flex items-center justify-center
  text-[9px] font-mono text-[hsl(var(--muted-foreground))]
  border-r border-[hsl(var(--border))]/50
  left-[var(--bar-left)] w-[var(--bar-width)]`;

const sectionLabel = `
  absolute top-0 h-5 flex items-center justify-center
  text-[10px] font-medium text-[hsl(var(--muted-foreground))]
  border-x border-[hsl(var(--border))]/50
  left-[var(--label-left)] w-[var(--label-width)]`;

// Loop region overlays
const loopRegionRuler = `
  absolute inset-y-0 left-0
  bg-[hsl(var(--selection))]/5 border-r-2 border-[hsl(var(--selection))]/20`;

// Playhead
const playheadLane = `
  absolute inset-y-0 w-px
  bg-[hsl(var(--foreground))]/60 z-30
  pointer-events-none`;

const lanesScroll = `flex-1 overflow-y-auto`;

const laneRow = `flex h-16 border-b border-[hsl(var(--border))]/50 group`;
const laneHeader = `
  w-44 shrink-0 bg-[hsl(var(--background))]/80 border-r border-[hsl(var(--border))]
  flex items-center px-3 gap-2`;
const colorDot = `w-2.5 h-2.5 rounded-full shrink-0 bg-[var(--dot-color)]`;
const trackName = `text-xs text-[hsl(var(--foreground))] font-medium truncate flex-1`;

const muteSoloGroup = `flex gap-0.5 opacity-0 group-hover:opacity-100 transition-opacity`;
const muteSoloBtn = `w-5 h-5 rounded text-[9px] font-bold flex items-center justify-center transition-colors`;
const muteBtnActive = `bg-[hsl(var(--plugin-bypassed))] text-[hsl(var(--primary-foreground))]`;
const soloBtnActive = `bg-yellow-500 text-black`;
const muteSoloBtnInactive = `text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]`;

const laneContent = `flex-1 relative`;

const sectionBg = `
  absolute inset-y-0 border-r border-[hsl(var(--border))]/30
  left-[var(--sec-left)] w-[var(--sec-width)]
  z-0 pointer-events-none`;

const gridLine = `
  absolute inset-y-0 w-px bg-[hsl(var(--border))]/30
  left-[var(--grid-left)]
  z-10 pointer-events-none`;

const clip = `
  absolute top-1.5 bottom-1.5 rounded-sm overflow-hidden
  left-[var(--clip-left)] w-[var(--clip-width)]
  bg-[var(--clip-color)] border border-[var(--clip-color)]
  z-20`;

const notesSvg = `w-full h-full`;

const chordLabelContainer = `
  absolute inset-y-0
  flex items-center justify-center pointer-events-none z-30
`;

const chordLabelText = `
  bg-[hsl(var(--background))]/50 backdrop-blur-sm
  text-[hsl(var(--foreground))] text-[9px] font-bold tracking-wider
  px-1.5 py-0.5 rounded
  border border-[hsl(var(--border))]/30
  shadow-sm whitespace-nowrap
`;
