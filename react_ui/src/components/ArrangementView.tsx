import { useRef, useEffect, useState, useCallback } from 'react';
import { useMixerStore, useTransportStore } from '@/data/store';
import type { NoteData } from '@/data/slices/mixer';
import { Chord } from '@tonaljs/tonal';
import { AutomationOverlay } from './AutomationOverlay';

// --- Zoom constants ---
const MIN_ZOOM = 0.25;
const MAX_ZOOM = 6;
const BASE_BAR_WIDTH = 80; // pixels per bar at zoom=1

export function ArrangementView() {
  const { tracks, sections, totalBars: storeTotalBars } = useMixerStore();
  const { looping, loopBars, loopStartBar } = useTransportStore();
  const [showAutomation, setShowAutomation] = useState(false);
  const zoomRef = useRef(1.0);
  const containerRef = useRef<HTMLDivElement>(null);
  const rulerScrollRef = useRef<HTMLDivElement>(null);
  const lanesScrollRef = useRef<HTMLDivElement>(null);
  const pendingZoom = useRef<{ zoom: number; scrollLeft: number } | null>(null);
  const rafId = useRef<number | null>(null);
  // Track which area the user is hovering/interacting with to prevent scroll feedback loops
  const activeScrollRegion = useRef<'ruler' | 'lanes' | null>(null);

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
    const trackMax = (track.notes ?? []).reduce((m, n) => Math.max(m, n.beat + n.duration), 0);
    return Math.max(max, trackMax);
  }, 4); // minimum 4 beats = 1 bar
  const totalBars = storeTotalBars > 1 ? storeTotalBars : Math.max(1, Math.ceil(maxBeat / 4));

  // Set initial --bar-w CSS variable
  useEffect(() => {
    containerRef.current?.style.setProperty('--bar-w', `${BASE_BAR_WIDTH}px`);
  }, []);

  // Zoom with Alt/Cmd + scroll wheel — rAF-throttled CSS variable updates
  useEffect(() => {
    const el = lanesScrollRef.current;
    const root = containerRef.current;
    if (!el || !root) return;

    const flushZoom = () => {
      rafId.current = null;
      const p = pendingZoom.current;
      if (!p) return;
      pendingZoom.current = null;
      // Single DOM write per frame — CSS engine recalcs all calc() positions
      root.style.setProperty('--bar-w', `${BASE_BAR_WIDTH * p.zoom}px`);
      el.scrollLeft = p.scrollLeft;
      if (rulerScrollRef.current) rulerScrollRef.current.scrollLeft = p.scrollLeft;
    };

    const onWheel = (e: WheelEvent) => {
      if (e.altKey || e.metaKey) {
        e.preventDefault();
        const rect = el.getBoundingClientRect();
        const mouseX = e.clientX - rect.left + el.scrollLeft;
        const oldZoom = zoomRef.current;
        // Continuous exponential zoom proportional to scroll delta
        const newZoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM,
          oldZoom * Math.pow(2, -e.deltaY * 0.004)));
        zoomRef.current = newZoom;
        // Compute target scroll position
        const scale = newZoom / oldZoom;
        const newScrollLeft = mouseX * scale - (e.clientX - rect.left);
        // Coalesce into pending — only one layout per frame
        pendingZoom.current = { zoom: newZoom, scrollLeft: newScrollLeft };
        if (rafId.current === null) {
          rafId.current = requestAnimationFrame(flushZoom);
        }
      }
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => {
      el.removeEventListener('wheel', onWheel);
      if (rafId.current !== null) cancelAnimationFrame(rafId.current);
    };
  }, []);

  // Sync ruler scroll with lanes scroll
  const handleLanesScroll = useCallback(() => {
    if (activeScrollRegion.current !== 'lanes') return;
    if (lanesScrollRef.current && rulerScrollRef.current) {
      if (rulerScrollRef.current.scrollLeft !== lanesScrollRef.current.scrollLeft) {
        rulerScrollRef.current.scrollLeft = lanesScrollRef.current.scrollLeft;
      }
    }
  }, []);

  // Sync lanes scroll with ruler scroll
  const handleRulerScroll = useCallback(() => {
    if (activeScrollRegion.current !== 'ruler') return;
    if (lanesScrollRef.current && rulerScrollRef.current) {
      if (lanesScrollRef.current.scrollLeft !== rulerScrollRef.current.scrollLeft) {
        lanesScrollRef.current.scrollLeft = rulerScrollRef.current.scrollLeft;
      }
    }
  }, []);

  return (
    <div className={container} ref={containerRef}>
      {/* Timeline ruler */}
      <div className={rulerRow} onMouseEnter={() => activeScrollRegion.current = 'ruler'}>
        <div className={rulerSpacer} />
        <div className={rulerTrack} ref={rulerScrollRef} onScroll={handleRulerScroll}>
          <div style={{ width: `calc(${totalBars} * var(--bar-w, ${BASE_BAR_WIDTH}px))`, minWidth: '100%' }} className="relative h-full">
            {/* Loop region overlay on ruler */}
            {looping && loopBars > 0 && (loopBars - loopStartBar) > 0 && (
              <LoopRegion
                loopStartBar={loopStartBar}
                loopEndBar={loopBars}
                totalBars={totalBars}
              />
            )}
            {/* Section labels in top half */}
            {sections.map((sec, i) => (
              <div
                key={i}
                className={sectionLabel}
                style={{
                  left: `calc(${sec.start} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
                  width: `calc(${sec.length} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
                }}
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
                  left: `calc(${i} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
                  width: `var(--bar-w, ${BASE_BAR_WIDTH}px)`,
                }}
              >
                {i + 1}
              </div>
            ))}
          </div>
        </div>
      </div>

      {/* Track lanes — scrolls both X (zoom) and Y (many tracks) */}
      <div 
        className={lanesScroll} 
        ref={lanesScrollRef} 
        onScroll={handleLanesScroll}
        onMouseEnter={() => activeScrollRegion.current = 'lanes'}
      >
        <div style={{ width: `calc(${totalBars} * var(--bar-w, ${BASE_BAR_WIDTH}px))`, minWidth: '100%' }}>
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
                      left: `calc(${sec.start} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
                      width: `calc(${sec.length} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
                    }}
                  />
                ))}

                {/* Grid lines for each bar */}
                {Array.from({ length: totalBars }, (_, i) => (
                  <div
                    key={i}
                    className={gridLine}
                    style={{ left: `calc(${i} * var(--bar-w, ${BASE_BAR_WIDTH}px))` }}
                  />
                ))}

                {/* Audio tracks: show audio clips or empty source options */}
                {track.type === 'audio' ? (
                  <div className="absolute inset-0">
                    {track.audioClips && track.audioClips.length > 0 ? (
                      track.audioClips.map((clip) => {
                        return (
                          <div
                            key={clip.id}
                            className={audioClipBlock}
                            style={{
                              left: `calc(${clip.startBeat / 4} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
                              width: `calc(${clip.duration / 4} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
                              '--clip-color': track.color,
                            } as React.CSSProperties}
                            onClick={() => useMixerStore.getState().openSampleEditor(track.id, clip.id)}
                          >
                            <div className={audioClipWaveform}>
                              {/* Waveform placeholder bars */}
                              {Array.from({ length: 24 }, (_, i) => (
                                <div
                                  key={i}
                                  className={waveformBar}
                                  style={{
                                    height: `${20 + Math.sin(i * 0.8) * 30 + Math.sin(i * 2.3 + 3) * 15}%`,
                                    backgroundColor: track.color,
                                  }}
                                />
                              ))}
                            </div>
                            <span className={audioClipLabel}>{clip.filePath.split('/').pop()}</span>
                          </div>
                        );
                      })
                    ) : (
                      <div className={audioEmptyLane}>
                        <button
                          className={audioSourceBtn}
                          title="Record audio"
                          onClick={() => useMixerStore.getState().setAudioRecordArm(track.id, true)}
                        >
                          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                            <circle cx="12" cy="12" r="6" fill="currentColor" opacity="0.3"/>
                            <circle cx="12" cy="12" r="10"/>
                          </svg>
                          <span>Record</span>
                        </button>
                        <button
                          className={audioSourceBtn}
                          title="Import sample"
                        >
                          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                            <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
                            <polyline points="7 10 12 15 17 10"/>
                            <line x1="12" y1="15" x2="12" y2="3"/>
                          </svg>
                          <span>Sample</span>
                        </button>
                        <button
                          className={`${audioSourceBtn} opacity-40`}
                          title="Generate with Lyria (coming soon)"
                          disabled
                        >
                          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                            <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/>
                          </svg>
                          <span>Generate</span>
                        </button>
                      </div>
                    )}
                  </div>
                ) : (
                  <>
                    {/* MIDI notes mini piano-roll — clickable to open editor */}
                    <div
                        className={`transition-opacity duration-300 ${showAutomation && track.automation && track.automation.length > 0 ? 'opacity-30' : 'opacity-100'} absolute inset-0 cursor-pointer`}
                        onClick={(e) => {
                          // Determine which section was clicked (pixel-based)
                          const rect = e.currentTarget.getBoundingClientRect();
                          const clickedPx = e.clientX - rect.left;
                          const currentBarW = BASE_BAR_WIDTH * zoomRef.current;
                          const clickedBar = clickedPx / currentBarW;
                          let sectionIdx = 0;
                          for (let si = 0; si < sections.length; si++) {
                            const sec = sections[si];
                            if (clickedBar >= sec.start && clickedBar < sec.start + sec.length) {
                              sectionIdx = si;
                              break;
                            }
                          }
                          useMixerStore.getState().openMidiEditor(track.id, sectionIdx);
                        }}
                      >
                        {track.notes && track.notes.length > 0 && (
                          <NoteClip
                            notes={track.notes}
                            color={track.color}
                          />
                        )}
                      </div>
                  </>
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

          {/* Track creation buttons */}
          <div className={addTrackRow}>
            <div className={addTrackSpacerWithButtons}>
              <button
                className={addTrackBtn}
                onClick={() => useMixerStore.getState().addAudioTrack()}
              >
                <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round">
                  <line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>
                </svg>
                Audio
              </button>
              <button
                className={addTrackBtn}
                onClick={() => useMixerStore.getState().addMidiTrack()}
              >
                <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round">
                  <line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>
                </svg>
                MIDI
              </button>
            </div>
            <div className={addTrackContent} />
          </div>
        </div>
      </div>
    </div>
  );
}

// --- Interactive Loop Region Component ---

function LoopRegion({
  loopStartBar,
  loopEndBar,
  totalBars,
}: {
  loopStartBar: number;
  loopEndBar: number;
  totalBars: number;
}) {
  const containerRef = useRef<HTMLDivElement>(null);
  const [dragMode, setDragMode] = useState<'none' | 'move' | 'resize-left' | 'resize-right'>('none');
  const dragStart = useRef({ x: 0, startBar: 0, endBar: 0 });

  const setLoopRange = useTransportStore.getState().setLoopRange;

  // Read current bar width from CSS variable at drag time
  const getBarWidth = useCallback((): number => {
    const root = containerRef.current?.closest('[style*="--bar-w"]') as HTMLElement | null;
    if (!root) return BASE_BAR_WIDTH;
    const val = getComputedStyle(root).getPropertyValue('--bar-w');
    return parseFloat(val) || BASE_BAR_WIDTH;
  }, []);

  const getBarFromX = useCallback((clientX: number): number => {
    const parent = containerRef.current?.parentElement;
    if (!parent) return 0;
    const rect = parent.getBoundingClientRect();
    const px = clientX - rect.left + parent.scrollLeft;
    return Math.round(Math.max(0, Math.min(totalBars, px / getBarWidth())));
  }, [totalBars, getBarWidth]);

  const getCursorZone = useCallback((clientX: number): 'left' | 'right' | 'body' => {
    const el = containerRef.current;
    if (!el) return 'body';
    const rect = el.getBoundingClientRect();
    const edgeSize = 6;
    if (clientX - rect.left < edgeSize) return 'left';
    if (rect.right - clientX < edgeSize) return 'right';
    return 'body';
  }, []);

  const handlePointerDown = useCallback((e: React.PointerEvent) => {
    e.preventDefault();
    e.stopPropagation();
    const zone = getCursorZone(e.clientX);
    const mode = zone === 'left' ? 'resize-left' : zone === 'right' ? 'resize-right' : 'move';
    setDragMode(mode);
    dragStart.current = { x: e.clientX, startBar: loopStartBar, endBar: loopEndBar };
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
  }, [getCursorZone, loopStartBar, loopEndBar]);

  const handlePointerMove = useCallback((e: React.PointerEvent) => {
    if (dragMode === 'none') {
      // Just update cursor
      const zone = getCursorZone(e.clientX);
      const el = containerRef.current;
      if (el) {
        el.style.cursor = zone === 'body' ? 'grab' : 'col-resize';
      }
      return;
    }

    e.preventDefault();
    const { startBar, endBar } = dragStart.current;
    const loopLen = endBar - startBar;

    if (dragMode === 'move') {
      const currentBar = getBarFromX(e.clientX);
      const origBar = getBarFromX(dragStart.current.x);
      const delta = currentBar - origBar;
      let newStart = startBar + delta;
      let newEnd = endBar + delta;
      // Clamp to bounds
      if (newStart < 0) { newStart = 0; newEnd = loopLen; }
      if (newEnd > totalBars) { newEnd = totalBars; newStart = totalBars - loopLen; }
      setLoopRange(newStart, newEnd);
    } else if (dragMode === 'resize-left') {
      let newStart = getBarFromX(e.clientX);
      newStart = Math.max(0, Math.min(newStart, endBar - 1)); // min 1 bar
      setLoopRange(newStart, endBar);
    } else if (dragMode === 'resize-right') {
      let newEnd = getBarFromX(e.clientX);
      newEnd = Math.max(startBar + 1, Math.min(newEnd, totalBars)); // min 1 bar
      setLoopRange(startBar, newEnd);
    }
  }, [dragMode, getBarFromX, getCursorZone, totalBars, setLoopRange]);

  const handlePointerUp = useCallback((e: React.PointerEvent) => {
    if (dragMode !== 'none') {
      setDragMode('none');
      (e.target as HTMLElement).releasePointerCapture(e.pointerId);
    }
  }, [dragMode]);

  return (
    <div
      ref={containerRef}
      className={loopRegionRuler}
      style={{
        left: `calc(${loopStartBar} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
        width: `calc(${loopEndBar - loopStartBar} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
      }}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onPointerLeave={() => {
        if (dragMode !== 'none') {
          // Don't cancel if pointer captured
          return;
        }
        const el = containerRef.current;
        if (el) el.style.cursor = 'grab';
      }}
    >
      {/* Left edge handle */}
      <div className={loopEdgeHandle} style={{ left: 0 }} />
      {/* Right edge handle */}
      <div className={loopEdgeHandle} style={{ right: 0 }} />
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
}: {
  notes: NoteData[];
  color: string;
}) {
  if (notes.length === 0) return null;

  const minPitch = notes.reduce((m, n) => Math.min(m, n.pitch), 127);
  const maxPitch = notes.reduce((m, n) => Math.max(m, n.pitch), 0);
  const pitchRange = Math.max(maxPitch - minPitch, 1);

  const minBeat = notes.reduce((m, n) => Math.min(m, n.beat), Infinity);
  const maxBeatEnd = notes.reduce((m, n) => Math.max(m, n.beat + n.duration), 0);

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
        left: `calc(${minBeat / 4} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
        width: `calc(${(maxBeatEnd - minBeat) / 4} * var(--bar-w, ${BASE_BAR_WIDTH}px))`,
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
  relative overflow-x-auto overflow-y-hidden
  scrollbar-hide`; // Hide visual scrollbar so styles look identical to before

const barNumber = `
  absolute bottom-0 h-5 flex items-center justify-center
  text-[9px] font-mono text-[hsl(var(--muted-foreground))]
  border-r border-[hsl(var(--border))]/50`;

const sectionLabel = `
  absolute top-0 h-5 flex items-center justify-center
  text-[10px] font-medium text-[hsl(var(--muted-foreground))]
  border-x border-[hsl(var(--border))]/50`;

// Loop region overlays
const loopRegionRuler = `
  absolute inset-y-0
  bg-[hsl(var(--selection))]/10 border-x-2 border-[hsl(var(--selection))]/30
  cursor-grab z-40 touch-none`;

const loopEdgeHandle = `
  absolute inset-y-0 w-1.5
  bg-[hsl(var(--selection))]/40
  hover:bg-[hsl(var(--selection))]/60
  cursor-col-resize z-50`;

// Playhead
const playheadLane = `
  absolute inset-y-0 w-px
  bg-[hsl(var(--foreground))]/60 z-30
  pointer-events-none`;

const lanesScroll = `flex-1 overflow-auto`;

const laneRow = `flex h-16 border-b border-[hsl(var(--border))]/50 group`;
const laneHeader = `
  w-44 shrink-0 bg-[hsl(var(--background))] border-r border-[hsl(var(--border))]
  flex items-center px-3 gap-2 sticky left-0 z-30`;
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
  z-0 pointer-events-none`;

const gridLine = `
  absolute inset-y-0 w-px bg-[hsl(var(--border))]/30
  z-10 pointer-events-none`;

const clip = `
  absolute top-1.5 bottom-1.5 rounded-sm overflow-hidden
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

// --- Audio clip styles ---
const audioClipBlock = `
  absolute top-1.5 bottom-1.5 rounded-sm overflow-hidden cursor-pointer
  bg-[var(--clip-color)]/20 border border-[var(--clip-color)]/50
  hover:bg-[var(--clip-color)]/30 transition-colors
  z-20 flex flex-col`;

const audioClipWaveform = `
  flex-1 flex items-center gap-px px-1 overflow-hidden`;

const waveformBar = `
  w-[2px] min-w-[2px] rounded-full opacity-70`;

const audioClipLabel = `
  text-[8px] text-[hsl(var(--foreground))]/70 px-1.5 pb-0.5 truncate font-medium`;

// --- Audio track empty-state ---
const audioEmptyLane = `
  absolute inset-0 flex items-center justify-center gap-3
  text-[hsl(var(--muted-foreground))]`;

const audioSourceBtn = `
  flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[10px] font-medium
  bg-[hsl(var(--muted))]/50 hover:bg-[hsl(var(--muted))]
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  transition-colors cursor-pointer border border-[hsl(var(--border))]/30
  hover:border-[hsl(var(--border))]/60`;

// --- Add track buttons ---
const addTrackRow = `flex h-10 border-b border-[hsl(var(--border))]/30`;

const addTrackSpacerWithButtons = `
  w-44 shrink-0 bg-[hsl(var(--background))] border-r border-[hsl(var(--border))]
  sticky left-0 z-40 flex items-center gap-1.5 px-3`;

const addTrackContent = `
  flex-1 flex items-center gap-2 px-3`;

const addTrackBtn = `
  flex items-center gap-1.5 px-3 py-1 rounded-md text-[10px] font-semibold
  bg-[hsl(var(--muted))]/40 hover:bg-[hsl(var(--muted))]/80
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  transition-colors cursor-pointer border border-dashed border-[hsl(var(--border))]/40
  hover:border-[hsl(var(--border))]/70`;
