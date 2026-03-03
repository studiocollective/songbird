import { useRef, useEffect, useState, useCallback, useMemo } from 'react';
import { useMixerStore, useTransportStore } from '@/data/store';
import { nativeFunction } from '@/data/bridge';
import type { NoteData } from '@/data/slices/mixer';
import { SheetMusicView } from './SheetMusicView';
import './midi-editor.css';

type ViewMode = 'pianoRoll' | 'sheetMusic';

// --- Native functions: individual note operations ---
const midiAddNote = nativeFunction('midiAddNote');
const midiRemoveNote = nativeFunction('midiRemoveNote');
const midiMoveNote = nativeFunction('midiMoveNote');

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

export function MidiEditor() {
  const {
    tracks, sections, totalBars,
    selectedClip,
    closeMidiEditor,
  } = useMixerStore();
  const midiEditorGridDiv = useMixerStore((s) => s.midiEditorGridDiv);
  const setMidiEditorGridDiv = useMixerStore((s) => s.setMidiEditorGridDiv);

  const gridRef = useRef<HTMLDivElement>(null);
  const [viewMode, setViewMode] = useState<ViewMode>('pianoRoll');
  const [swing, setSwing] = useState(0);
  const [beatWidth, setBeatWidth] = useState(40);

  // Compute beatWidth from gridRef body width
  useEffect(() => {
    const el = gridRef.current;
    if (!el || !selectedClip) return;
    const section = sections[selectedClip.sectionIndex];
    const secBeats = section ? section.length * 4 : totalBars * 4;
    const resizeObs = new ResizeObserver(() => {
      const w = el.clientWidth - PIANO_KEY_WIDTH;
      setBeatWidth(Math.max(20, w / secBeats));
    });
    resizeObs.observe(el);
    return () => resizeObs.disconnect();
  }, [selectedClip, sections, totalBars]);

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
    <div className="midi-editor">
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
        onClose={closeMidiEditor}
      />

      {viewMode === 'pianoRoll' ? (
        <div className="midi-editor__body" ref={gridRef}>
          {/* Note grid (piano keys rendered inline as sticky-left) */}
          <NoteGrid
            notes={sectionNotes}
            trackId={selectedClip.trackId}
            sectionName={sectionName}
            sectionStartBeat={sectionStartBeat}
            sectionLengthBeats={sectionLengthBeats}
            sectionLengthBars={sectionLengthBars}
            gridDiv={midiEditorGridDiv}
            beatWidth={beatWidth}
            color={track.color}
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
      {/* Velocity lane */}
      <VelocityLane
        notes={sectionNotes}
        trackId={selectedClip.trackId}
        sectionName={sectionName}
        sectionStartBeat={sectionStartBeat}
        sectionLengthBeats={sectionLengthBeats}
        beatWidth={beatWidth}
        color={track.color}
      />
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
  swing, onSwingChange, onClose,
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
  onClose: () => void;
}) {
  return (
    <div className="midi-editor__header">
      <div className="midi-editor__header-left">
        <div className="midi-editor__track-dot" style={{ backgroundColor: trackColor }} />
        <span className="midi-editor__track-name">{trackName}</span>
        <span className="midi-editor__section-badge">{sectionName}</span>
        <span className="midi-editor__section-length">{sectionBars} bar{sectionBars !== 1 ? 's' : ''}</span>
      </div>
      <div className="midi-editor__header-right">
        {/* Grid division selector (only relevant in piano roll mode) */}
        {viewMode === 'pianoRoll' && (
          <div className="midi-editor__grid-selector">
            {GRID_DIVISIONS.map((d) => (
              <button
                key={d}
                className={`midi-editor__grid-btn ${d === gridDiv ? 'midi-editor__grid-btn--active' : ''}`}
                onClick={() => onGridDivChange(d)}
              >
                1/{d}
              </button>
            ))}
          </div>
        )}

        {/* Swing slider */}
        {viewMode === 'pianoRoll' && (
          <div className="midi-editor__swing-control">
            <label className="midi-editor__swing-label">Swing</label>
            <input
              type="range"
              min={0}
              max={100}
              value={swing}
              onChange={(e) => onSwingChange(Number(e.target.value))}
              className="midi-editor__swing-slider"
            />
            <span className="midi-editor__swing-value">{swing}%</span>
          </div>
        )}

        {/* View toggle */}
        <div className="midi-editor__view-toggle">
          <button
            className={`midi-editor__view-btn ${viewMode === 'pianoRoll' ? 'midi-editor__view-btn--active' : ''}`}
            onClick={() => onViewModeChange('pianoRoll')}
          >
            Piano Roll
          </button>
          <button
            className={`midi-editor__view-btn ${viewMode === 'sheetMusic' ? 'midi-editor__view-btn--active' : ''}`}
            onClick={() => onViewModeChange('sheetMusic')}
          >
            Sheet Music
          </button>
        </div>
        <button className="midi-editor__close-btn" onClick={onClose} title="Close (Esc)">
          ✕
        </button>
      </div>
    </div>
  );
}

// --- Note Grid ---
function NoteGrid({
  notes, trackId, sectionName, sectionStartBeat, sectionLengthBeats, sectionLengthBars,
  gridDiv, beatWidth, color,
}: {
  notes: NoteData[];
  trackId: number;
  sectionName: string;
  sectionStartBeat: number;
  sectionLengthBeats: number;
  sectionLengthBars: number;
  gridDiv: number;
  beatWidth: number;
  color: string;
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
    setLocalNotes(notes);
    setSelectedIndices(new Set());
  }, [notes]);

  const gridStepBeats = 4 / gridDiv;


  const gridWidth = sectionLengthBeats * beatWidth;
  const gridHeight = TOTAL_KEYS * ROW_HEIGHT;

  // Snap a beat position to the grid
  const snapBeat = useCallback((beat: number) => {
    return Math.round(beat / gridStepBeats) * gridStepBeats;
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

  // Find note index at a given pitch + beat
  const findNoteAt = useCallback((pitch: number, beat: number): number => {
    return localNotes.findIndex(
      (n) => n.pitch === pitch && n.beat <= beat && n.beat + n.duration > beat
    );
  }, [localNotes]);

  // Mouse down on grid: start tracking for click-vs-drag
  const handleGridMouseDown = useCallback((e: React.MouseEvent) => {
    if ((e.target as HTMLElement).closest('.midi-editor__note')) return;

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
          midiAddNote(trackId, sectionName, pitch, snappedBeat, gridStepBeats, 100);
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
          midiMoveNote(trackId, sectionName,
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
  }, [dragState, trackId, sectionName, sectionStartBeat, localNotes, beatWidth, gridStepBeats, snapBeat]);

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
            midiRemoveNote(trackId, sectionName, note.pitch, note.beat - sectionStartBeat);
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

        midiMoveNote(trackId, sectionName,
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
  }, [selectedIndices, localNotes, trackId, sectionName, sectionStartBeat, gridStepBeats]);

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

      const classes = [
        'midi-editor__note',
        isDragging ? 'midi-editor__note--dragging' : '',
        isSelected ? 'midi-editor__note--selected' : '',
      ].filter(Boolean).join(' ');

      return (
        <div
          key={`${note.pitch}-${note.beat}-${i}`}
          className={classes}
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
    const layers = [
      `repeating-linear-gradient(90deg, hsl(0 0% 50% / 0.5) 0px, hsl(0 0% 50% / 0.5) 1px, transparent 1px, transparent ${barPx}px)`,
      `repeating-linear-gradient(90deg, hsl(0 0% 50% / 0.25) 0px, hsl(0 0% 50% / 0.25) 1px, transparent 1px, transparent ${beatPx}px)`,
      `repeating-linear-gradient(90deg, hsl(0 0% 50% / 0.12) 0px, hsl(0 0% 50% / 0.12) 1px, transparent 1px, transparent ${subBeatPx}px)`,
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
      rows.push(
        <div
          key={pitch}
          className={`midi-editor__grid-row ${bk ? 'midi-editor__grid-row--black' : ''} ${isC ? 'midi-editor__grid-row--c' : ''}`}
          style={{ top: `${top}px`, height: `${ROW_HEIGHT}px` }}
        />
      );
    }
    return rows;
  }, []);

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
          className={`midi-editor__key ${bk ? 'midi-editor__key--black' : 'midi-editor__key--white'} ${isC ? 'midi-editor__key--c' : ''}`}
          style={{ top: `${top}px` }}
        >
          {isC && <span className="midi-editor__key-label">{name}</span>}
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
        <span key={bar} className="midi-editor__bar-label" style={{ left: `${x}px` }}>
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
    <div className="midi-editor__grid-container" ref={gridContainerRef}>
      {/* Bar numbers */}
      <div className="midi-editor__bar-labels" style={{ width: `${PIANO_KEY_WIDTH + gridWidth}px` }}>
        {barLabels}
      </div>

      {/* Grid area */}
      <div
        className="midi-editor__grid"
        ref={gridAreaRef}
        style={{
          width: `${PIANO_KEY_WIDTH + gridWidth}px`,
          height: `${gridHeight}px`,
          ...gridBackground,
        }}
        onMouseDown={handleGridMouseDown}
      >
        {/* Piano keys — sticky left inside the grid */}
        <div className="midi-editor__keyboard" style={{ height: `${gridHeight}px` }}>
          {pianoKeys}
        </div>

        {rowBackgrounds}
        {noteElements}

        {/* Selection rectangle */}
        {selRectStyle && (
          <div className="midi-editor__selection-rect" style={selRectStyle} />
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

// --- Playhead ---
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

      if (currentBeat >= sectionStartBeat && currentBeat < sectionStartBeat + sectionLengthBeats) {
        const relativeBeat = currentBeat - sectionStartBeat;
        const pct = relativeBeat / sectionLengthBeats;
        el.style.left = `${pianoKeyWidth + pct * gridWidth}px`;
        el.style.display = 'block';
      } else {
        el.style.display = 'none';
      }

      if (state.playing) {
        rafRef.current = requestAnimationFrame(update);
      }
    };

    const unsub = useTransportStore.subscribe((state, prev) => {
      if (state.position !== prev.position || state.playing !== prev.playing) {
        if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
        rafRef.current = requestAnimationFrame(update);
      }
    });

    rafRef.current = requestAnimationFrame(update);

    return () => {
      unsub();
      if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
    };
  }, [sectionStartBeat, sectionLengthBeats, gridWidth, pianoKeyWidth]);

  return <div ref={elRef} className="midi-editor__playhead" />;
}

// --- Velocity Lane ---
function VelocityLane({
  notes, trackId, sectionName, sectionStartBeat, beatWidth, color,
}: {
  notes: NoteData[];
  trackId: number;
  sectionName: string;
  sectionStartBeat: number;
  sectionLengthBeats: number;
  beatWidth: number;
  color: string;
}) {
  const handleVelocityDrag = useCallback((e: React.MouseEvent, noteIdx: number) => {
    e.preventDefault();
    e.stopPropagation();
    const barEl = e.currentTarget as HTMLElement;
    const startY = e.clientY;
    const note = notes[noteIdx];
    const startVel = note.velocity;

    const onMove = (me: MouseEvent) => {
      const dy = startY - me.clientY;
      const newVel = Math.max(1, Math.min(127, startVel + Math.round(dy * 0.5)));
      barEl.style.height = `${(newVel / 127) * (VELOCITY_LANE_HEIGHT - 8)}px`;
      barEl.dataset.pendingVel = String(newVel);
      barEl.title = `Velocity: ${newVel}`;
    };

    const onUp = () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
      const pendingVel = Number(barEl.dataset.pendingVel ?? startVel);
      if (pendingVel !== startVel) {
        // Change velocity via remove + add with new velocity
        const beatRel = note.beat - sectionStartBeat;
        midiRemoveNote(trackId, sectionName, note.pitch, beatRel);
        midiAddNote(trackId, sectionName, note.pitch, beatRel, note.duration, pendingVel);
      }
    };

    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
  }, [notes, trackId, sectionName, sectionStartBeat]);

  return (
    <div className="midi-editor__velocity-lane">
      <div className="midi-editor__velocity-label">VEL</div>
      <div className="midi-editor__velocity-bars">
        {notes.map((note, i) => {
          const beatRelative = note.beat - sectionStartBeat;
          const left = beatRelative * beatWidth;
          const height = (note.velocity / 127) * (VELOCITY_LANE_HEIGHT - 8);

          return (
            <div
              key={`vel-${i}`}
              className="midi-editor__velocity-bar"
              style={{
                left: `${left}px`,
                height: `${height}px`,
                backgroundColor: color,
                width: `${Math.max(3, note.duration * beatWidth - 2)}px`,
              }}
              onMouseDown={(e) => handleVelocityDrag(e, i)}
              title={`Velocity: ${note.velocity}`}
            />
          );
        })}
      </div>
    </div>
  );
}
