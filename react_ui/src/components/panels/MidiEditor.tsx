import { useRef, useEffect, useState, useCallback, useMemo } from 'react';
import { useMixerStore, useTransportStore } from '@/data/store';
import { getRtBuffer } from '@/data/meters';
import { nativeFunction } from '@/data/bridge';
import type { NoteData } from '@/data/slices/mixer';
import { SheetMusicView } from './SheetMusicView';
import { cn } from '@/lib/utils';

type ViewMode = 'pianoRoll' | 'sheetMusic';

// --- Native functions: individual note operations ---
const midiAddNote = nativeFunction('midiAddNote');
const midiRemoveNote = nativeFunction('midiRemoveNote');
const midiMoveNote = nativeFunction('midiMoveNote');
const midiSetLoopLength = nativeFunction('midiSetLoopLength');

// --- Constants ---
const PIANO_KEY_WIDTH = 48;
const VELOCITY_LANE_HEIGHT = 64;
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const BLACK_KEYS = new Set([1, 3, 6, 8, 10]); // C#, D#, F#, G#, A#

// Visible MIDI range
const MIN_PITCH = 24;  // C1
const MAX_PITCH = 96;  // C7
const TOTAL_KEYS = MAX_PITCH - MIN_PITCH + 1;

const GRID_DIVISIONS = [4, 8, 16] as const;
const ROW_HEIGHT = 14;

function noteName(pitch: number): string {
  return NOTE_NAMES[pitch % 12] + Math.floor(pitch / 12 - 1);
}

function isBlackKey(pitch: number): boolean {
  return BLACK_KEYS.has(pitch % 12);
}

// --- Scale helpers ---
const NOTE_TO_SEMITONE: Record<string, number> = {
  'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5,
  'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11,
};

const MODE_INTERVALS: Record<string, number[]> = {
  ionian:     [0, 2, 4, 5, 7, 9, 11],
  dorian:     [0, 2, 3, 5, 7, 9, 10],
  phrygian:   [0, 1, 3, 5, 7, 8, 10],
  lydian:     [0, 2, 4, 6, 7, 9, 11],
  mixolydian: [0, 2, 4, 5, 7, 9, 10],
  aeolian:    [0, 2, 3, 5, 7, 8, 10],
  locrian:    [0, 1, 3, 5, 6, 8, 10],
};

function getScalePitchClasses(root: string, mode: string): Set<number> {
  const rootSemitone = NOTE_TO_SEMITONE[root];
  const intervals = MODE_INTERVALS[mode];
  if (rootSemitone == null || !intervals) return new Set();
  return new Set(intervals.map((i) => (rootSemitone + i) % 12));
}

export function MidiEditor() {
  const {
    tracks, sections, totalBars,
    selectedClip,
    closeMidiEditor,
  } = useMixerStore();
  const midiEditorGridDiv = useMixerStore((s) => s.midiEditorGridDiv);
  const setMidiEditorGridDiv = useMixerStore((s) => s.setMidiEditorGridDiv);

  const gridRef = useRef<HTMLDivElement>(null);
  const editorRef = useRef<HTMLDivElement>(null);
  const [viewMode, setViewMode] = useState<ViewMode>('pianoRoll');
  const [swing, setSwing] = useState(0);
  const [highlightScale, setHighlightScale] = useState(false);
  const scale = useTransportStore((s) => s.scale);
  const [baseBeatWidth, setBaseBeatWidth] = useState(40);
  const [zoomLevel, setZoomLevel] = useState(1.0);
  const beatWidth = baseBeatWidth * zoomLevel;

  // Compute beatWidth from editor container width (not gridRef which unmounts on view toggle)
  useEffect(() => {
    const el = editorRef.current;
    if (!el || !selectedClip) return;
    const section = sections[selectedClip.sectionIndex];
    const secBeats = section ? section.length * 4 : totalBars * 4;
    const resizeObs = new ResizeObserver(() => {
      const w = el.clientWidth - PIANO_KEY_WIDTH;
      setBaseBeatWidth(Math.max(20, w / secBeats));
    });
    resizeObs.observe(el);
    return () => resizeObs.disconnect();
  }, [selectedClip, sections, totalBars]);

  // Zoom with Option/Cmd + scroll wheel — attach to editorRef (always mounted)
  useEffect(() => {
    const el = editorRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      if (e.altKey || e.metaKey) {
        e.preventDefault();
        const delta = e.deltaY > 0 ? 0.9 : 1.1;
        setZoomLevel((z) => Math.min(4, Math.max(1.0, z * delta)));
      }
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  }, []);

  // Scroll to the octave with the most notes on mount
  useEffect(() => {
    if (!gridRef.current || !selectedClip) return;
    // Read tracks snapshot inside effect (don't depend on tracks to avoid re-scrolling on note edits)
    const currentTracks = useMixerStore.getState().tracks;
    const t = currentTracks.find((tr) => tr.id === selectedClip.trackId);
    if (!t) return;
    const notes = t.notes ?? [];
    let focusPitch = 60; // fallback: C4

    if (notes.length > 0) {
      const octaveCounts = new Map<number, number>();
      for (const n of notes) {
        const oct = Math.floor(n.pitch / 12);
        octaveCounts.set(oct, (octaveCounts.get(oct) ?? 0) + 1);
      }
      let bestOct = 5;
      let bestCount = 0;
      for (const [oct, count] of octaveCounts) {
        if (count > bestCount) { bestOct = oct; bestCount = count; }
      }
      focusPitch = bestOct * 12 + 6;
    }

    const scrollTo = (MAX_PITCH - focusPitch) * ROW_HEIGHT - gridRef.current.clientHeight / 2;
    gridRef.current.scrollTop = Math.max(0, scrollTo);
  }, [selectedClip]);

  // Close on Escape
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') closeMidiEditor();
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [closeMidiEditor]);

  if (!selectedClip) return null;

  const track = tracks.find((t) => t.id === selectedClip.trackId);
  if (!track) return null;

  // Determine the section that was clicked
  const section = sections[selectedClip.sectionIndex];
  const sectionStartBeat = section ? section.start * 4 : 0;
  const sectionLengthBeats = section ? section.length * 4 : totalBars * 4;
  const sectionLengthBars = section ? section.length : totalBars;
  const sectionName = section?.name ?? 'intro';

  // Filter notes to the selected section
  const sectionNotes = (track.notes ?? []).filter(
    (n) => n.beat >= sectionStartBeat && n.beat < sectionStartBeat + sectionLengthBeats
  );

  return (
    <div className={editorRoot} ref={editorRef}>
      {/* Header bar */}
      <MidiEditorHeader
        trackName={track.name}
        trackColor={track.color}
        sectionName={sectionName}
        sectionBars={sectionLengthBars}
        gridDiv={midiEditorGridDiv}
        onGridDivChange={setMidiEditorGridDiv}
        viewMode={viewMode}
        onViewModeChange={setViewMode}
        swing={swing}
        onSwingChange={setSwing}
        highlightScale={highlightScale}
        onHighlightScaleChange={setHighlightScale}
        hasScale={!!scale}
        onClose={closeMidiEditor}
      />

      {viewMode === 'pianoRoll' ? (
        <div className={editorBody} ref={gridRef}>
          {/* Note grid (piano keys rendered inline as sticky-left) */}
          <NoteGrid
            notes={sectionNotes}
            trackId={selectedClip.trackId}
            sectionIndex={selectedClip.sectionIndex}
            sectionStartBeat={sectionStartBeat}
            sectionLengthBeats={sectionLengthBeats}
            sectionLengthBars={sectionLengthBars}
            gridDiv={midiEditorGridDiv}
            beatWidth={beatWidth}
            color={track.color}
            scalePitchClasses={highlightScale && scale ? getScalePitchClasses(scale.root, scale.mode) : null}
            loopLengthBeats={track.loopLengthBeats ?? 0}
          />
        </div>
      ) : (
        <SheetMusicView
          notes={sectionNotes}
          sectionStartBeat={sectionStartBeat}
          sectionLengthBeats={sectionLengthBeats}
          sectionLengthBars={sectionLengthBars}
          color={track.color}
        />
      )}
      {/* Velocity lane — only in piano roll mode */}
      {viewMode === 'pianoRoll' && (
        <VelocityLane
        notes={sectionNotes}
        trackId={selectedClip.trackId}
        sectionIndex={selectedClip.sectionIndex}
        sectionStartBeat={sectionStartBeat}
        sectionLengthBeats={sectionLengthBeats}
        beatWidth={beatWidth}
        color={track.color}
      />
      )}
    </div>
  );
}

// --- Types ---
interface DragState {
  type: 'move' | 'resize-right';
  noteIndex: number;
  startX: number;
  startY: number;
  originalBeat: number;
  originalPitch: number;
  originalDuration: number;
  currentBeat: number;
  currentPitch: number;
  currentDuration: number;
}

interface SelectionRect {
  startX: number;
  startY: number;
  currentX: number;
  currentY: number;
}

// --- Header ---
function MidiEditorHeader({
  trackName, trackColor, sectionName, sectionBars,
  gridDiv, onGridDivChange, viewMode, onViewModeChange,
  swing, onSwingChange, highlightScale, onHighlightScaleChange, hasScale, onClose,
}: {
  trackName: string;
  trackColor: string;
  sectionName: string;
  sectionBars: number;
  gridDiv: number;
  onGridDivChange: (d: number) => void;
  viewMode: ViewMode;
  onViewModeChange: (mode: ViewMode) => void;
  swing: number;
  onSwingChange: (v: number) => void;
  highlightScale: boolean;
  onHighlightScaleChange: (v: boolean) => void;
  hasScale: boolean;
  onClose: () => void;
}) {
  return (
    <div className={headerCls}>
      <div className={headerLeft}>
        <div className={trackDot} style={{ backgroundColor: trackColor }} />
        <span className={trackNameCls}>{trackName}</span>
        <span className={sectionBadge}>{sectionName}</span>
        <span className={sectionLength}>{sectionBars} bar{sectionBars !== 1 ? 's' : ''}</span>
        {/* Scale highlight toggle */}
        {viewMode === 'pianoRoll' && (
          <button
            className={cn(scaleBtn, highlightScale && scaleBtnActive)}
            onClick={() => onHighlightScaleChange(!highlightScale)}
            disabled={!hasScale}
            title={hasScale ? (highlightScale ? 'Hide scale overlay' : 'Highlight scale notes') : 'Set a scale in the transport bar first'}
          >
            Scale
          </button>
        )}
      </div>
      <div className={headerRight}>
        {/* Grid division selector (only relevant in piano roll mode) */}
        {viewMode === 'pianoRoll' && (
          <div className={gridSelector}>
            {GRID_DIVISIONS.map((d) => (
              <button
                key={d}
                className={cn(gridBtnCls, d === gridDiv && gridBtnActive)}
                onClick={() => onGridDivChange(d)}
              >
                1/{d}
              </button>
            ))}
          </div>
        )}

        {/* Swing slider */}
        {viewMode === 'pianoRoll' && (
          <div className={swingControl}>
            <label className={swingLabel}>Swing</label>
            <input
              type="range"
              min={0}
              max={100}
              value={swing}
              onChange={(e) => onSwingChange(Number(e.target.value))}
              className={swingSlider}
            />
            <span className={swingValue}>{swing}%</span>
          </div>
        )}

        {/* View toggle */}
        <div className={viewToggle}>
          <button
            className={cn(viewBtnCls, viewMode === 'pianoRoll' && viewBtnActive)}
            onClick={() => onViewModeChange('pianoRoll')}
          >
            Piano Roll
          </button>
          <button
            className={cn(viewBtnCls, viewMode === 'sheetMusic' && viewBtnActive)}
            onClick={() => onViewModeChange('sheetMusic')}
          >
            Sheet Music
          </button>
        </div>
        <button className={closeBtn} onClick={onClose} title="Close (Esc)">
          ✕
        </button>
      </div>
    </div>
  );
}

// --- Note Grid ---
function NoteGrid({
  notes, trackId, sectionIndex, sectionStartBeat, sectionLengthBeats, sectionLengthBars,
  gridDiv, beatWidth, color, scalePitchClasses, loopLengthBeats,
}: {
  notes: NoteData[];
  trackId: number;
  sectionIndex: number;
  sectionStartBeat: number;
  sectionLengthBeats: number;
  sectionLengthBars: number;
  gridDiv: number;
  beatWidth: number;
  color: string;
  scalePitchClasses: Set<number> | null;
  loopLengthBeats: number;
}) {
  const gridContainerRef = useRef<HTMLDivElement>(null);
  const gridAreaRef = useRef<HTMLDivElement>(null);
  const [dragState, setDragState] = useState<DragState | null>(null);
  const [selectionRect, setSelectionRect] = useState<SelectionRect | null>(null);
  const [selectedIndices, setSelectedIndices] = useState<Set<number>>(new Set());
  const [hoveredNote, setHoveredNote] = useState<{ pitch: number; beat: number } | null>(null);
  const [hoverEdge, setHoverEdge] = useState<'left' | 'right' | null>(null);

  // Optimistic local notes (for instant feedback)
  const [localNotes, setLocalNotes] = useState<NoteData[]>(notes);

  // Sync local notes when store notes change (from trackState event)
  useEffect(() => {
    setLocalNotes((prevNotes) => {
      // Remap selected indices: match old selected notes by pitch+beat to new indices
      setSelectedIndices((prevSelected) => {
        if (prevSelected.size === 0) return prevSelected;
        const oldSelected = [...prevSelected].map((i) => prevNotes[i]).filter(Boolean);
        const newSelected = new Set<number>();
        for (const old of oldSelected) {
          const newIdx = notes.findIndex(
            (n) => n.pitch === old.pitch && Math.abs(n.beat - old.beat) < 0.05
          );
          if (newIdx >= 0) newSelected.add(newIdx);
        }
        return newSelected;
      });
      return notes;
    });
  }, [notes]);

  const gridStepBeats = 4 / gridDiv;

  // Loop boundary (section-relative beats) — auto-detect from rightmost note
  // Loop boundary from track state (0 = no looping = full section)
  const [loopEndBeats, setLoopEndBeats] = useState(
    loopLengthBeats > 0 ? loopLengthBeats : sectionLengthBeats
  );
  const loopEndRef = useRef(loopEndBeats);
  useEffect(() => { loopEndRef.current = loopEndBeats; }, [loopEndBeats]);

  // Drag the loop marker
  const handleLoopDrag = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    e.stopPropagation();
    const gridEl = gridAreaRef.current;
    if (!gridEl) return;

    const onMove = (me: MouseEvent) => {
      const rect = gridEl.getBoundingClientRect();
      const relX = me.clientX - rect.left - PIANO_KEY_WIDTH;
      const beat = relX / beatWidth;
      // Snap to bar boundaries (every 4 beats), minimum 1 bar
      const snappedBars = Math.max(1, Math.round(beat / 4));
      setLoopEndBeats(Math.min(snappedBars * 4, sectionLengthBeats));
    };

    const onUp = () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
      const finalLoop = loopEndRef.current;
      // Call native function: 0 = disable looping, >0 = set loop length
      const loopBeats = finalLoop >= sectionLengthBeats ? 0 : finalLoop;
      midiSetLoopLength(trackId, loopBeats);
    };

    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
  }, [beatWidth, sectionLengthBeats, trackId]);

  const gridWidth = sectionLengthBeats * beatWidth;
  const gridHeight = TOTAL_KEYS * ROW_HEIGHT;

  // Snap a beat position to the grid
  const snapBeat = useCallback((beat: number) => {
    return Math.floor(beat / gridStepBeats) * gridStepBeats;
  }, [gridStepBeats]);

  // Convert pixel X to beat (section-relative), accounting for scroll
  const xToBeat = useCallback((clientX: number): number => {
    const el = gridAreaRef.current;
    if (!el) return 0;
    const rect = el.getBoundingClientRect();
    const relX = clientX - rect.left - PIANO_KEY_WIDTH;
    return Math.max(0, relX / beatWidth);
  }, [beatWidth]);

  // Convert pixel Y to pitch, accounting for scroll
  const yToPitch = useCallback((clientY: number): number => {
    const el = gridAreaRef.current;
    if (!el) return 60;
    const rect = el.getBoundingClientRect();
    const relY = clientY - rect.top;
    const row = Math.floor(relY / ROW_HEIGHT);
    return Math.max(MIN_PITCH, Math.min(MAX_PITCH, MAX_PITCH - row));
  }, []);

  // Find note index at a given pitch + beat (uses grid-step tolerance for hit-testing)
  const findNoteAt = useCallback((pitch: number, beat: number): number => {
    return localNotes.findIndex(
      (n) => n.pitch === pitch && beat >= n.beat && beat < n.beat + n.duration + 0.01
    );
  }, [localNotes]);

  // Mouse down on grid: start tracking for click-vs-drag
  const handleGridMouseDown = useCallback((e: React.MouseEvent) => {
    if ((e.target as HTMLElement).closest('.midi-note')) return;

    // Always start tracking — we'll decide on mouseup whether it's click or drag
    setSelectionRect({
      startX: e.clientX,
      startY: e.clientY,
      currentX: e.clientX,
      currentY: e.clientY,
    });
  }, []);

  // Rubber-band selection drag & click-to-add-note
  useEffect(() => {
    if (!selectionRect) return;

    const handleMouseMove = (e: MouseEvent) => {
      setSelectionRect((prev) => prev ? { ...prev, currentX: e.clientX, currentY: e.clientY } : null);
    };

    const handleMouseUp = (e: MouseEvent) => {
      if (!selectionRect) return;

      const dx = Math.abs(e.clientX - selectionRect.startX);
      const dy = Math.abs(e.clientY - selectionRect.startY);
      const wasDrag = dx > 5 || dy > 5;

      if (wasDrag) {
        // Rubber-band selection: select notes inside the rect
        const x1 = Math.min(selectionRect.startX, e.clientX);
        const x2 = Math.max(selectionRect.startX, e.clientX);
        const y1 = Math.min(selectionRect.startY, e.clientY);
        const y2 = Math.max(selectionRect.startY, e.clientY);

        const beatMin = sectionStartBeat + xToBeat(x1);
        const beatMax = sectionStartBeat + xToBeat(x2);
        const pitchMax = yToPitch(y1);
        const pitchMin = yToPitch(y2);

        const selected = new Set<number>();
        localNotes.forEach((n, i) => {
          if (n.pitch >= pitchMin && n.pitch <= pitchMax &&
              n.beat + n.duration > beatMin && n.beat < beatMax) {
            selected.add(i);
          }
        });
        setSelectedIndices(selected);
      } else {
        // Click: add note on empty space, or select existing note
        const rawBeat = xToBeat(e.clientX);
        const pitch = yToPitch(e.clientY);
        const rawAbsoluteBeat = sectionStartBeat + rawBeat;
        const clickedIdx = findNoteAt(pitch, rawAbsoluteBeat);

        if (clickedIdx >= 0) {
          if (e.shiftKey) {
            setSelectedIndices((prev) => {
              const next = new Set(prev);
              if (next.has(clickedIdx)) next.delete(clickedIdx);
              else next.add(clickedIdx);
              return next;
            });
          } else {
            setSelectedIndices(new Set([clickedIdx]));
          }
        } else {
          // Add note at snapped position
          setSelectedIndices(new Set());
          const snappedBeat = snapBeat(rawBeat);
          const snappedAbsoluteBeat = sectionStartBeat + snappedBeat;
          const newNote: NoteData = { pitch, beat: snappedAbsoluteBeat, duration: gridStepBeats, velocity: 100 };
          setLocalNotes([...localNotes, newNote]);
          midiAddNote(trackId, sectionIndex, pitch, snappedBeat, gridStepBeats, 100);
        }
      }

      setSelectionRect(null);
    };

    window.addEventListener('mousemove', handleMouseMove);
    window.addEventListener('mouseup', handleMouseUp);
    return () => {
      window.removeEventListener('mousemove', handleMouseMove);
      window.removeEventListener('mouseup', handleMouseUp);
    };
  }, [selectionRect, localNotes, sectionStartBeat, xToBeat, yToPitch]);

  // Note mouse interaction for dragging
  const handleNoteMouseDown = useCallback((e: React.MouseEvent, note: NoteData, noteIndex: number) => {
    e.stopPropagation();
    e.preventDefault();

    // Select the note if not already selected
    if (!selectedIndices.has(noteIndex)) {
      if (e.shiftKey) {
        setSelectedIndices((prev) => new Set([...prev, noteIndex]));
      } else {
        setSelectedIndices(new Set([noteIndex]));
      }
    }

    const noteBeatRelative = note.beat - sectionStartBeat;
    const noteRightX = (noteBeatRelative + note.duration) * beatWidth + PIANO_KEY_WIDTH;
    const el = gridContainerRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const relX = e.clientX - rect.left + el.scrollLeft;

    if (Math.abs(relX - noteRightX) < 8) {
      setDragState({
        type: 'resize-right',
        noteIndex,
        startX: e.clientX,
        startY: e.clientY,
        originalBeat: note.beat,
        originalPitch: note.pitch,
        originalDuration: note.duration,
        currentBeat: note.beat,
        currentPitch: note.pitch,
        currentDuration: note.duration,
      });
    } else {
      setDragState({
        type: 'move',
        noteIndex,
        startX: e.clientX,
        startY: e.clientY,
        originalBeat: note.beat,
        originalPitch: note.pitch,
        originalDuration: note.duration,
        currentBeat: note.beat,
        currentPitch: note.pitch,
        currentDuration: note.duration,
      });
    }
  }, [sectionStartBeat, beatWidth, selectedIndices]);

  // Global mouse move + up for dragging
  useEffect(() => {
    if (!dragState) return;

    const handleMouseMove = (e: MouseEvent) => {
      const dx = e.clientX - dragState.startX;
      const dy = e.clientY - dragState.startY;

      if (dragState.type === 'move') {
        const beatDelta = snapBeat(dx / beatWidth);
        const pitchDelta = -Math.round(dy / ROW_HEIGHT);
        const newBeat = Math.max(sectionStartBeat, dragState.originalBeat + beatDelta);
        const newPitch = Math.max(MIN_PITCH, Math.min(MAX_PITCH, dragState.originalPitch + pitchDelta));
        setDragState({
          ...dragState,
          currentBeat: newBeat,
          currentPitch: newPitch,
        });
      } else if (dragState.type === 'resize-right') {
        const beatDelta = snapBeat(dx / beatWidth);
        const newDuration = Math.max(gridStepBeats, dragState.originalDuration + beatDelta);
        setDragState({
          ...dragState,
          currentDuration: newDuration,
        });
      }
    };

    const handleMouseUp = () => {
      if (!dragState) return;

      const hasMoved =
        dragState.currentBeat !== dragState.originalBeat ||
        dragState.currentPitch !== dragState.originalPitch ||
        dragState.currentDuration !== dragState.originalDuration;

      if (hasMoved) {
        const note = localNotes[dragState.noteIndex];
        if (note) {
          const newNotes = localNotes.map((n, i) => {
            if (i !== dragState.noteIndex) return n;
            if (dragState.type === 'move') {
              return { ...n, beat: dragState.currentBeat, pitch: dragState.currentPitch };
            } else {
              return { ...n, duration: dragState.currentDuration };
            }
          });
          setLocalNotes(newNotes);
          midiMoveNote(trackId, sectionIndex,
            note.pitch, note.beat - sectionStartBeat,
            dragState.type === 'move' ? dragState.currentPitch : note.pitch,
            (dragState.type === 'move' ? dragState.currentBeat : note.beat) - sectionStartBeat,
            dragState.type === 'resize-right' ? dragState.currentDuration : note.duration
          );
        }
      }

      setDragState(null);
    };

    window.addEventListener('mousemove', handleMouseMove);
    window.addEventListener('mouseup', handleMouseUp);
    return () => {
      window.removeEventListener('mousemove', handleMouseMove);
      window.removeEventListener('mouseup', handleMouseUp);
    };
  }, [dragState, trackId, sectionIndex, sectionStartBeat, localNotes, beatWidth, gridStepBeats, snapBeat]);

  // Keyboard shortcuts: Delete, Backspace, Arrow keys
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (selectedIndices.size === 0) return;

      // Delete / Backspace — remove selected notes
      if (e.key === 'Delete' || e.key === 'Backspace') {
        e.preventDefault();
        const toRemove = Array.from(selectedIndices).sort((a, b) => b - a);
        for (const idx of toRemove) {
          const note = localNotes[idx];
          if (note) {
            midiRemoveNote(trackId, sectionIndex, note.pitch, note.beat - sectionStartBeat);
          }
        }
        setLocalNotes(localNotes.filter((_, i) => !selectedIndices.has(i)));
        setSelectedIndices(new Set());
        return;
      }

      // Arrow keys — move selected notes
      let pitchDelta = 0;
      let beatDelta = 0;

      if (e.key === 'ArrowUp') {
        e.preventDefault();
        pitchDelta = e.shiftKey ? 12 : 1;
      } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        pitchDelta = e.shiftKey ? -12 : -1;
      } else if (e.key === 'ArrowRight') {
        e.preventDefault();
        beatDelta = e.shiftKey ? 4 : gridStepBeats;
      } else if (e.key === 'ArrowLeft') {
        e.preventDefault();
        beatDelta = e.shiftKey ? -4 : -gridStepBeats;
      }

      if (pitchDelta === 0 && beatDelta === 0) return;

      const newNotes = [...localNotes];
      for (const idx of selectedIndices) {
        const note = newNotes[idx];
        if (!note) continue;
        const oldPitch = note.pitch;
        const oldBeat = note.beat;
        const newPitch = Math.max(MIN_PITCH, Math.min(MAX_PITCH, oldPitch + pitchDelta));
        const newBeat = Math.max(sectionStartBeat, oldBeat + beatDelta);

        midiMoveNote(trackId, sectionIndex,
          oldPitch, oldBeat - sectionStartBeat,
          newPitch, newBeat - sectionStartBeat,
          note.duration
        );
        newNotes[idx] = { ...note, pitch: newPitch, beat: newBeat };
      }
      setLocalNotes(newNotes);
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [selectedIndices, localNotes, trackId, sectionIndex, sectionStartBeat, gridStepBeats]);

  // Note hover for edge detection
  const handleNoteMouseMove = useCallback((e: React.MouseEvent, note: NoteData) => {
    const noteBeatRelative = note.beat - sectionStartBeat;
    const noteRightX = (noteBeatRelative + note.duration) * beatWidth + PIANO_KEY_WIDTH;
    const el = gridContainerRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const relX = e.clientX - rect.left + el.scrollLeft;

    if (Math.abs(relX - noteRightX) < 8) {
      setHoverEdge('right');
    } else {
      setHoverEdge(null);
    }
    setHoveredNote({ pitch: note.pitch, beat: note.beat });
  }, [sectionStartBeat, beatWidth]);

  // Build note elements (with drag preview)
  const noteElements = useMemo(() => {
    return localNotes.map((note, i) => {
      const isDragging = dragState?.noteIndex === i;
      const isSelected = selectedIndices.has(i);

      const displayBeat = isDragging ? dragState.currentBeat : note.beat;
      const displayPitch = isDragging ? dragState.currentPitch : note.pitch;
      const displayDuration = isDragging ? dragState.currentDuration : note.duration;

      const beatRelative = displayBeat - sectionStartBeat;
      const left = PIANO_KEY_WIDTH + beatRelative * beatWidth;
      const width = displayDuration * beatWidth;
      const top = (MAX_PITCH - displayPitch) * ROW_HEIGHT;
      const opacity = 0.5 + (note.velocity / 127) * 0.5;

      return (
        <div
          key={`${note.pitch}-${note.beat}-${i}`}
          className={cn('midi-note', noteBaseCls, isDragging && noteDragging, isSelected && noteSelected)}
          style={{
            left: `${left}px`,
            top: `${top}px`,
            width: `${Math.max(4, width - 1)}px`,
            height: `${ROW_HEIGHT - 1}px`,
            backgroundColor: color,
            opacity,
            cursor: hoverEdge === 'right' && hoveredNote?.pitch === note.pitch && hoveredNote?.beat === note.beat
              ? 'col-resize' : 'grab',
          }}
          onMouseDown={(e) => handleNoteMouseDown(e, note, i)}
          onMouseMove={(e) => handleNoteMouseMove(e, note)}
          onMouseLeave={() => { setHoveredNote(null); setHoverEdge(null); }}
        />
      );
    });
  }, [localNotes, dragState, selectedIndices, sectionStartBeat, beatWidth, color, hoveredNote, hoverEdge, handleNoteMouseDown, handleNoteMouseMove]);

  // Grid lines as CSS background (no DOM elements — instant grid div changes)
  const gridBackground = useMemo(() => {
    const subBeatPx = gridStepBeats * beatWidth;
    const beatPx = 1 * beatWidth;
    const barPx = 4 * beatWidth;

    // Layer 1: bar lines (thickest)
    // Layer 2: beat lines
    // Layer 3: sub-beat lines (thinnest)
    // Read theme-aware colors from CSS custom properties
    const style = getComputedStyle(document.documentElement);
    const barColor = `hsl(${style.getPropertyValue('--grid-line-bar').trim()})`;
    const beatColor = `hsl(${style.getPropertyValue('--grid-line-beat').trim()})`;
    const subColor = `hsl(${style.getPropertyValue('--grid-line-sub').trim()})`;
    const layers = [
      `repeating-linear-gradient(90deg, ${barColor} 0px, ${barColor} 1px, transparent 1px, transparent ${barPx}px)`,
      `repeating-linear-gradient(90deg, ${beatColor} 0px, ${beatColor} 1px, transparent 1px, transparent ${beatPx}px)`,
      `repeating-linear-gradient(90deg, ${subColor} 0px, ${subColor} 1px, transparent 1px, transparent ${subBeatPx}px)`,
    ];

    return {
      backgroundImage: layers.join(', '),
      backgroundPositionX: `${PIANO_KEY_WIDTH}px`,
    };
  }, [gridStepBeats, beatWidth]);

  // Row backgrounds
  const rowBackgrounds = useMemo(() => {
    const rows = [];
    for (let pitch = MAX_PITCH; pitch >= MIN_PITCH; pitch--) {
      const top = (MAX_PITCH - pitch) * ROW_HEIGHT;
      const bk = isBlackKey(pitch);
      const isC = pitch % 12 === 0;
      const outOfScale = scalePitchClasses ? !scalePitchClasses.has(pitch % 12) : false;
      rows.push(
        <div
          key={pitch}
          className={cn(gridRow, bk && gridRowBlack, isC && gridRowC, outOfScale && gridRowOutOfScale)}
          style={{ top: `${top}px`, height: `${ROW_HEIGHT}px` }}
        />
      );
    }
    return rows;
  }, [scalePitchClasses]);

  // Piano keys (rendered inside the grid so they scroll together)
  const pianoKeys = useMemo(() => {
    const keys = [];
    for (let pitch = MAX_PITCH; pitch >= MIN_PITCH; pitch--) {
      const name = noteName(pitch);
      const bk = isBlackKey(pitch);
      const isC = pitch % 12 === 0;
      const top = (MAX_PITCH - pitch) * ROW_HEIGHT;
      keys.push(
        <div
          key={pitch}
          className={cn(keyBase, bk ? keyBlack : keyWhite, isC && keyC)}
          style={{ top: `${top}px` }}
        >
          {!bk && <span className={keyLabelCls}>{name}</span>}
        </div>
      );
    }
    return keys;
  }, []);

  // Bar number labels
  const barLabels = useMemo(() => {
    const labels = [];
    for (let bar = 0; bar < sectionLengthBars; bar++) {
      const x = PIANO_KEY_WIDTH + bar * 4 * beatWidth;
      labels.push(
        <span key={bar} className={barLabelCls} style={{ left: `${x}px` }}>
          {bar + 1}
        </span>
      );
    }
    return labels;
  }, [sectionLengthBars, beatWidth]);

  // Selection rectangle style (viewport-relative, rendered as fixed overlay)
  const selRectStyle = useMemo(() => {
    if (!selectionRect) return null;
    const x1 = Math.min(selectionRect.startX, selectionRect.currentX);
    const y1 = Math.min(selectionRect.startY, selectionRect.currentY);
    const w = Math.abs(selectionRect.currentX - selectionRect.startX);
    const h = Math.abs(selectionRect.currentY - selectionRect.startY);
    return { left: `${x1}px`, top: `${y1}px`, width: `${w}px`, height: `${h}px` };
  }, [selectionRect]);

  return (
    <div className={gridContainer} ref={gridContainerRef}>
      {/* Bar numbers */}
      <div className={barLabelsCls} style={{ width: `${PIANO_KEY_WIDTH + gridWidth}px` }}>
        {barLabels}
      </div>

      {/* Grid area */}
      <div
        className={gridArea}
        ref={gridAreaRef}
        style={{
          width: `${PIANO_KEY_WIDTH + gridWidth}px`,
          height: `${gridHeight}px`,
        }}
        onMouseDown={handleGridMouseDown}
      >
        {/* Piano keys — sticky left inside the grid */}
        <div className={keyboardCls} style={{ height: `${gridHeight}px` }}>
          {pianoKeys}
        </div>

        {rowBackgrounds}

        {/* Grid lines overlay — sits above rows, below notes */}
        <div
          className="absolute inset-0 pointer-events-none z-[1]"
          style={gridBackground}
        />

        {/* Loop boundary: dimmed area beyond loop */}
        {loopEndBeats < sectionLengthBeats && (
          <div
            className="absolute top-0 right-0 bottom-0 bg-black/40 pointer-events-none z-[5]"
            style={{ left: `${PIANO_KEY_WIDTH + loopEndBeats * beatWidth}px` }}
          />
        )}

        {/* Loop boundary marker (draggable) */}
        {loopEndBeats < sectionLengthBeats && (
          <div
            className="absolute top-0 bottom-0 w-1.5 cursor-col-resize z-[15] border-l-2 border-[hsl(45_100%_60%)]"
            style={{ left: `${PIANO_KEY_WIDTH + loopEndBeats * beatWidth - 3}px` }}
            onMouseDown={handleLoopDrag}
            title={`Loop: ${loopEndBeats / 4} bars`}
          />
        )}

        {noteElements}

        {/* Selection rectangle */}
        {selRectStyle && (
          <div className={selectionRectCls} style={selRectStyle} />
        )}

        {/* Playhead */}
        <MidiEditorPlayhead
          sectionStartBeat={sectionStartBeat}
          sectionLengthBeats={sectionLengthBeats}
          gridWidth={gridWidth}
          pianoKeyWidth={PIANO_KEY_WIDTH}
        />
      </div>
    </div>
  );
}

// --- Playhead (lerp smoothing) ---
function MidiEditorPlayhead({
  sectionStartBeat, sectionLengthBeats, gridWidth, pianoKeyWidth,
}: {
  sectionStartBeat: number;
  sectionLengthBeats: number;
  gridWidth: number;
  pianoKeyWidth: number;
}) {
  const elRef = useRef<HTMLDivElement>(null);
  const rafRef = useRef<number | null>(null);
  const visualPosRef = useRef(0);

  useEffect(() => {
    const el = elRef.current;
    if (!el) return;

    const update = () => {
      const { playing, bpm } = useTransportStore.getState();
      const rt = getRtBuffer();

      let targetPos = rt.position;
      if (playing && rt.lastPositionUpdate > 0) {
        targetPos += (performance.now() - rt.lastPositionUpdate) / 1000;
      }

      if (playing) {
        const gap = targetPos - visualPosRef.current;
        if (Math.abs(gap) > 0.5) {
          visualPosRef.current = targetPos;
        } else {
          visualPosRef.current += gap * 0.4;
        }
      } else {
        visualPosRef.current = targetPos;
      }

      const currentBeat = visualPosRef.current * (bpm / 60);

      if (currentBeat >= sectionStartBeat && currentBeat < sectionStartBeat + sectionLengthBeats) {
        const relativeBeat = currentBeat - sectionStartBeat;
        const pct = relativeBeat / sectionLengthBeats;
        const px = pianoKeyWidth + pct * gridWidth;
        el.style.transform = `translateX(${px}px)`;
        el.style.display = 'block';
      } else {
        el.style.display = 'none';
      }

      if (playing) {
        rafRef.current = requestAnimationFrame(update);
      } else {
        rafRef.current = null;
      }
    };

    const unsub = useTransportStore.subscribe((state, prev) => {
      if (state.playing && !prev.playing) {
        const rt = getRtBuffer();
        visualPosRef.current = rt.position;
        if (rafRef.current === null) {
          rafRef.current = requestAnimationFrame(update);
        }
      } else if (!state.playing && prev.playing) {
        requestAnimationFrame(update);
      } else if (!state.playing && state.position !== prev.position) {
        requestAnimationFrame(update);
      }
    });

    rafRef.current = requestAnimationFrame(update);

    return () => {
      unsub();
      if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
    };
  }, [sectionStartBeat, sectionLengthBeats, gridWidth, pianoKeyWidth]);

  return <div ref={elRef} className={playheadCls} />;
}

// --- Velocity Lane ---
function VelocityLane({
  notes, trackId, sectionIndex, sectionStartBeat, beatWidth, color,
}: {
  notes: NoteData[];
  trackId: number;
  sectionIndex: number;
  sectionStartBeat: number;
  sectionLengthBeats: number;
  beatWidth: number;
  color: string;
}) {
  // Local velocity overrides for optimistic update during drag
  const [velOverrides, setVelOverrides] = useState<Map<number, number>>(new Map());

  // Clear overrides when notes change from store
  useEffect(() => {
    setVelOverrides(new Map());
  }, [notes]);

  const handleVelocityDrag = useCallback((e: React.MouseEvent, noteIdx: number) => {
    e.preventDefault();
    e.stopPropagation();
    const startY = e.clientY;
    const note = notes[noteIdx];
    const startVel = note.velocity;

    const onMove = (me: MouseEvent) => {
      const dy = startY - me.clientY;
      const newVel = Math.max(1, Math.min(127, startVel + Math.round(dy * 1.5)));
      setVelOverrides((prev) => new Map(prev).set(noteIdx, newVel));
    };

    const onUp = (me: MouseEvent) => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
      const dy = startY - me.clientY;
      const finalVel = Math.max(1, Math.min(127, startVel + Math.round(dy * 1.5)));
      if (finalVel !== startVel) {
        const beatRel = note.beat - sectionStartBeat;
        midiRemoveNote(trackId, sectionIndex, note.pitch, beatRel);
        midiAddNote(trackId, sectionIndex, note.pitch, beatRel, note.duration, finalVel);
      }
    };

    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
  }, [notes, trackId, sectionIndex, sectionStartBeat]);

  return (
    <div className={velLane}>
      <div className={velLabel}>VEL</div>
      <div className={velBars}>
        {notes.map((note, i) => {
          const beatRelative = note.beat - sectionStartBeat;
          const left = beatRelative * beatWidth;
          const vel = velOverrides.get(i) ?? note.velocity;
          const height = (vel / 127) * (VELOCITY_LANE_HEIGHT - 8);

          return (
            <div
              key={`vel-${i}`}
              className={velBarCls}
              style={{
                left: `${left}px`,
                height: `${height}px`,
                backgroundColor: color,
                width: `${Math.max(3, note.duration * beatWidth - 2)}px`,
              }}
              onMouseDown={(e) => handleVelocityDrag(e, i)}
              title={`Velocity: ${vel}`}
            />
          );
        })}
      </div>
    </div>
  );
}

// ═══════════════════════════════════════
// Tailwind class constants — MIDI Editor
// ═══════════════════════════════════════

const editorRoot = `
  flex flex-col bg-[hsl(var(--mixer))] border-t border-[hsl(var(--border))]
  h-[360px] overflow-hidden`;

// --- Header ---
const headerCls = `
  flex items-center justify-between h-9 px-3
  bg-[hsl(var(--background))] border-b border-[hsl(var(--border))] shrink-0`;
const headerLeft = `flex items-center gap-2`;
const headerRight = `flex items-center gap-3`;

const trackDot = `w-2.5 h-2.5 rounded-full`;
const trackNameCls = `text-xs font-semibold text-[hsl(var(--foreground))]`;
const sectionBadge = `
  text-[10px] font-medium px-2 py-0.5 rounded
  bg-[hsl(var(--accent))] text-[hsl(var(--muted-foreground))]
  uppercase tracking-wide`;
const sectionLength = `text-[10px] text-[hsl(var(--muted-foreground))] tabular-nums`;

// Grid division buttons
const gridSelector = `flex gap-0.5`;
const gridBtnCls = `
  text-[10px] px-1.5 py-0.5 rounded-sm border-none cursor-pointer
  bg-transparent text-[hsl(var(--muted-foreground))] transition-all tabular-nums
  hover:bg-[hsl(var(--accent))] hover:text-[hsl(var(--foreground))]`;
const gridBtnActive = `!bg-[hsl(var(--primary))] !text-[hsl(var(--primary-foreground))]`;

// Swing
const swingControl = `flex items-center gap-1.5 ml-1`;
const swingLabel = `text-[11px] text-[hsl(var(--muted-foreground))] whitespace-nowrap`;
const swingSlider = `w-16 h-1 accent-[hsl(var(--primary))] cursor-pointer`;
const swingValue = `text-[11px] text-[hsl(var(--muted-foreground))] tabular-nums min-w-7 text-right`;

// Close
const closeBtn = `
  text-sm px-1.5 py-0.5 rounded border-none cursor-pointer bg-transparent
  text-[hsl(var(--muted-foreground))] transition-all leading-none
  hover:bg-[hsl(var(--destructive))] hover:text-[hsl(var(--destructive-foreground))]`;

// Scale button
const scaleBtn = `
  text-[10px] font-medium px-2.5 py-[3px] rounded-sm border-none cursor-pointer
  bg-transparent text-[hsl(var(--muted-foreground))] transition-all
  hover:bg-[hsl(var(--accent))] hover:text-[hsl(var(--foreground))]
  disabled:opacity-35 disabled:cursor-not-allowed`;
const scaleBtnActive = `!bg-[hsl(var(--selection))] !text-[hsl(var(--primary-foreground))]`;

// View toggle
const viewToggle = `flex gap-px bg-[hsl(var(--border))] rounded overflow-hidden`;
const viewBtnCls = `
  text-[10px] font-medium px-2.5 py-[3px] border-none cursor-pointer
  bg-[hsl(var(--background))] text-[hsl(var(--muted-foreground))] transition-all whitespace-nowrap
  hover:text-[hsl(var(--foreground))] hover:bg-[hsl(var(--accent))]`;
const viewBtnActive = `!bg-[hsl(var(--primary))] !text-[hsl(var(--primary-foreground))]`;

// --- Body ---
const editorBody = `
  flex-1 overflow-auto min-h-0
  [scrollbar-width:thin] [scrollbar-color:hsl(var(--border))_transparent]`;

// --- Grid ---
const gridContainer = `flex flex-col min-w-full`;
const barLabelsCls = `
  sticky top-0 z-[45] h-5 shrink-0
  border-b border-[hsl(var(--border))] bg-[hsl(var(--mixer))]`;
const barLabelCls = `
  absolute top-0 h-5 flex items-center pl-1.5
  text-[9px] font-medium text-[hsl(var(--muted-foreground))] tabular-nums`;
const gridArea = `relative select-none cursor-[var(--pencil-cursor)]`;

// Grid rows
const gridRow = `absolute left-12 right-0 pointer-events-none`;
const gridRowBlack = `bg-[hsl(var(--background)/0.5)]`;
const gridRowC = `border-b border-[hsl(var(--border)/0.4)]`;
const gridRowOutOfScale = `!bg-[hsl(var(--grid-out-of-scale))]`;

// --- Piano keys ---
const keyboardCls = `sticky left-0 top-0 w-12 z-50 bg-[hsl(var(--mixer))] pointer-events-none`;
const keyBase = `absolute left-0 h-3.5 flex items-center justify-end pr-1 box-border`;
const keyWhite = `
  w-12 bg-white z-[1]
  border-b border-b-[hsl(0_0%_82%)] border-r border-r-[hsl(0_0%_75%)]`;
const keyBlack = `
  w-[34px] bg-[hsl(0_0%_18%)] z-[2]
  border-b border-b-[hsl(0_0%_10%)] border-r border-r-[hsl(0_0%_10%)]`;
const keyC = `!border-b-[hsl(0_0%_65%)]`;
const keyLabelCls = `text-[8px] font-semibold text-[hsl(0_0%_50%)] tabular-nums`;

// --- Notes ---
const noteBaseCls = `
  absolute rounded-sm z-10 transition-shadow
  border border-white/15
  hover:shadow-[0_0_0_1px_rgba(255,255,255,0.3)] hover:z-20`;
const noteSelected = `
  !border-2 !border-[hsl(190_100%_70%)] !z-[25]
  shadow-[0_0_0_1px_hsl(190_100%_50%),0_0_8px_hsl(190_100%_50%/0.4)]`;
const noteDragging = `!opacity-80 !z-30 shadow-[0_2px_8px_rgba(0,0,0,0.3)]`;

// Selection rectangle
const selectionRectCls = `
  fixed border border-[hsl(var(--primary))] bg-[hsl(var(--primary)/0.1)]
  z-[9999] pointer-events-none`;

// Playhead
const playheadCls = `
  absolute top-0 bottom-0 left-0 w-px z-40 pointer-events-none will-change-transform
  bg-[hsl(var(--playhead))] shadow-[0_0_4px_hsl(var(--playhead)/0.3)]`;

// --- Velocity lane ---
const velLane = `
  h-16 shrink-0 flex border-t border-[hsl(var(--border))]
  bg-[hsl(var(--background)/0.5)] relative`;
const velLabel = `
  w-12 shrink-0 flex items-center justify-center
  text-[8px] font-semibold tracking-widest
  text-[hsl(var(--muted-foreground))] border-r border-[hsl(var(--border))]`;
const velBars = `flex-1 relative overflow-hidden`;
const velBarCls = `
  absolute bottom-1 rounded-t-sm cursor-ns-resize
  opacity-70 hover:opacity-100 hover:z-10 transition-opacity min-w-[3px]`;

// --- Sheet music (exported for SheetMusicView) ---
export const sheetMusicCls = `
  flex-1 overflow-auto min-h-0 py-2 bg-[hsl(var(--background)/0.3)]
  [scrollbar-width:thin] [scrollbar-color:hsl(var(--border))_transparent]`;
