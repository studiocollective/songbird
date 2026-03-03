import React, { useMemo } from 'react';
import type { NoteData } from '@/data/slices/mixer';

// ─── Music theory constants ───────────────────────────
const MIDDLE_C = 60;
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const BLACK_KEYS = new Set([1, 3, 6, 8, 10]);

// Staff rendering constants
const STAFF_LINE_SPACING = 10;     // px between adjacent staff lines
const STAFF_LINES = 5;
const TREBLE_TOP = 30;             // y-offset for top line of treble staff
const BASS_TOP = TREBLE_TOP + (STAFF_LINES - 1) * STAFF_LINE_SPACING + 50; // gap between staves
const STAFF_WIDTH_PER_BEAT = 40;   // horizontal scale
const LEFT_MARGIN = 60;            // space for clef symbols
const RIGHT_MARGIN = 20;
const NOTE_RADIUS = 4.5;

// ─── Pitch → staff Y position ─────────────────────────
// Treble clef: bottom line = E4 (pitch 64), top line = F5 (pitch 77)
// Bass clef:   bottom line = G2 (pitch 43), top line = A3 (pitch 57)
//
// We map pitch to a "staff position" (semitone-independent, based on the
// diatonic scale). Each half-step on the staff = STAFF_LINE_SPACING / 2.
//
// The diatonic note positions (within an octave, relative to C):
// C=0, D=1, E=2, F=3, G=4, A=5, B=6
const DIATONIC_POS: Record<number, number> = {
  0: 0,  // C
  1: 0,  // C# → same line as C, with accidental
  2: 1,  // D
  3: 1,  // D# → same line as D
  4: 2,  // E
  5: 3,  // F
  6: 3,  // F# → same line as F
  7: 4,  // G
  8: 4,  // G# → same line as G
  9: 5,  // A
  10: 5, // A# → same line as A
  11: 6, // B
};

/**
 * Convert MIDI pitch to a Y coordinate on the SVG.
 * Returns { y, isLedger, numLedgers, ledgerDir, staffType }
 */
function pitchToY(pitch: number): {
  y: number;
  staffType: 'treble' | 'bass';
  ledgerLines: number[];
} {
  const isTreble = pitch >= MIDDLE_C;
  const octave = Math.floor(pitch / 12);
  const semitone = pitch % 12;
  const diatonicInOctave = DIATONIC_POS[semitone];

  // Total diatonic position from C0
  const diatonicAbsolute = octave * 7 + diatonicInOctave;

  if (isTreble) {
    // Treble clef reference: B4 (pitch 71) = middle line (3rd line from bottom)
    // B4 diatonic absolute = 5*7 + 6 = 41
    const refDiatonic = 41; // B4
    const refY = TREBLE_TOP + 2 * STAFF_LINE_SPACING; // 3rd line from top = middle
    const diff = diatonicAbsolute - refDiatonic;
    const y = refY - diff * (STAFF_LINE_SPACING / 2);

    // Calculate ledger lines needed
    const ledgerLines: number[] = [];
    const topLineY = TREBLE_TOP;
    const bottomLineY = TREBLE_TOP + (STAFF_LINES - 1) * STAFF_LINE_SPACING;

    if (y > bottomLineY) {
      // Below the staff — ledger lines below
      for (let ly = bottomLineY + STAFF_LINE_SPACING; ly <= y + 1; ly += STAFF_LINE_SPACING) {
        ledgerLines.push(ly);
      }
    } else if (y < topLineY) {
      // Above the staff — ledger lines above
      for (let ly = topLineY - STAFF_LINE_SPACING; ly >= y - 1; ly -= STAFF_LINE_SPACING) {
        ledgerLines.push(ly);
      }
    }

    return { y, staffType: 'treble', ledgerLines };
  } else {
    // Bass clef reference: D3 (pitch 50) = middle line (3rd line from bottom)
    // D3 diatonic absolute = 4*7 + 1 = 29
    const refDiatonic = 29; // D3
    const refY = BASS_TOP + 2 * STAFF_LINE_SPACING; // middle line
    const diff = diatonicAbsolute - refDiatonic;
    const y = refY - diff * (STAFF_LINE_SPACING / 2);

    const ledgerLines: number[] = [];
    const topLineY = BASS_TOP;
    const bottomLineY = BASS_TOP + (STAFF_LINES - 1) * STAFF_LINE_SPACING;

    if (y > bottomLineY) {
      for (let ly = bottomLineY + STAFF_LINE_SPACING; ly <= y + 1; ly += STAFF_LINE_SPACING) {
        ledgerLines.push(ly);
      }
    } else if (y < topLineY) {
      for (let ly = topLineY - STAFF_LINE_SPACING; ly >= y - 1; ly -= STAFF_LINE_SPACING) {
        ledgerLines.push(ly);
      }
    }

    return { y, staffType: 'bass', ledgerLines };
  }
}

/** Get note head shape based on duration in beats */
function noteShape(duration: number): 'whole' | 'half' | 'quarter' | 'eighth' | 'sixteenth' {
  if (duration >= 4) return 'whole';
  if (duration >= 2) return 'half';
  if (duration >= 1) return 'quarter';
  if (duration >= 0.5) return 'eighth';
  return 'sixteenth';
}

/** Check if pitch is a sharp/accidental */
function isAccidental(pitch: number): boolean {
  return BLACK_KEYS.has(pitch % 12);
}

function noteName(pitch: number): string {
  return NOTE_NAMES[pitch % 12] + Math.floor(pitch / 12 - 1);
}

// ─── Component ────────────────────────────────────────
interface SheetMusicViewProps {
  notes: NoteData[];
  sectionStartBeat: number;
  sectionLengthBeats: number;
  sectionLengthBars: number;
  color: string;
}

export function SheetMusicView({
  notes,
  sectionStartBeat,
  sectionLengthBeats,
  sectionLengthBars,
}: SheetMusicViewProps) {
  const svgWidth = LEFT_MARGIN + sectionLengthBeats * STAFF_WIDTH_PER_BEAT + RIGHT_MARGIN;
  const svgHeight = BASS_TOP + (STAFF_LINES - 1) * STAFF_LINE_SPACING + 40;

  // Split notes into treble/bass
  const { trebleNotes, bassNotes } = useMemo(() => {
    const treble: NoteData[] = [];
    const bass: NoteData[] = [];
    for (const n of notes) {
      if (n.pitch >= MIDDLE_C) treble.push(n);
      else bass.push(n);
    }
    return { trebleNotes: treble, bassNotes: bass };
  }, [notes]);

  // ─── Staff lines ──────────────────────────────
  const staffLines = useMemo(() => {
    const lines: React.ReactElement[] = [];
    // Treble staff
    for (let i = 0; i < STAFF_LINES; i++) {
      const y = TREBLE_TOP + i * STAFF_LINE_SPACING;
      lines.push(
        <line
          key={`treble-${i}`}
          x1={LEFT_MARGIN - 10}
          y1={y}
          x2={svgWidth - RIGHT_MARGIN + 5}
          y2={y}
          stroke="hsl(0 0% 40%)"
          strokeWidth={0.8}
        />
      );
    }
    // Bass staff
    for (let i = 0; i < STAFF_LINES; i++) {
      const y = BASS_TOP + i * STAFF_LINE_SPACING;
      lines.push(
        <line
          key={`bass-${i}`}
          x1={LEFT_MARGIN - 10}
          y1={y}
          x2={svgWidth - RIGHT_MARGIN + 5}
          y2={y}
          stroke="hsl(0 0% 40%)"
          strokeWidth={0.8}
        />
      );
    }
    return lines;
  }, [svgWidth]);

  // ─── Bar lines ────────────────────────────────
  const barLines = useMemo(() => {
    const lines: React.ReactElement[] = [];
    for (let bar = 0; bar <= sectionLengthBars; bar++) {
      const x = LEFT_MARGIN + bar * 4 * STAFF_WIDTH_PER_BEAT;
      const trebleTop = TREBLE_TOP;
      const bassBottom = BASS_TOP + (STAFF_LINES - 1) * STAFF_LINE_SPACING;
      lines.push(
        <line
          key={`bar-${bar}`}
          x1={x}
          y1={trebleTop}
          x2={x}
          y2={bassBottom}
          stroke="hsl(0 0% 40%)"
          strokeWidth={bar === 0 || bar === sectionLengthBars ? 1.5 : 0.8}
        />
      );
      // Bar number
      if (bar < sectionLengthBars) {
        lines.push(
          <text
            key={`barnum-${bar}`}
            x={x + 4}
            y={TREBLE_TOP - 6}
            fill="hsl(0 0% 55%)"
            fontSize={9}
            fontFamily="Inter, system-ui, sans-serif"
          >
            {bar + 1}
          </text>
        );
      }
    }
    return lines;
  }, [sectionLengthBars]);

  // ─── Clef symbols ─────────────────────────────
  const clefs = useMemo(() => (
    <>
      {/* Treble clef (𝄞) */}
      <text
        x={LEFT_MARGIN - 28}
        y={TREBLE_TOP + (STAFF_LINES - 1) * STAFF_LINE_SPACING - 2}
        fontSize={42}
        fill="hsl(0 0% 60%)"
        fontFamily="'Noto Music', serif"
      >
        𝄞
      </text>
      {/* Bass clef (𝄢) */}
      <text
        x={LEFT_MARGIN - 26}
        y={BASS_TOP + (STAFF_LINES - 1) * STAFF_LINE_SPACING - 6}
        fontSize={34}
        fill="hsl(0 0% 60%)"
        fontFamily="'Noto Music', serif"
      >
        𝄢
      </text>
    </>
  ), []);

  // ─── Render notes ─────────────────────────────
  const renderNote = (note: NoteData, index: number) => {
    const beatRelative = note.beat - sectionStartBeat;
    const x = LEFT_MARGIN + beatRelative * STAFF_WIDTH_PER_BEAT + STAFF_WIDTH_PER_BEAT / 2;
    const { y, ledgerLines } = pitchToY(note.pitch);
    const shape = noteShape(note.duration);
    const acc = isAccidental(note.pitch);
    const opacity = 0.6 + (note.velocity / 127) * 0.4;

    const elements: React.ReactElement[] = [];

    // Ledger lines
    for (const ly of ledgerLines) {
      elements.push(
        <line
          key={`ledger-${index}-${ly}`}
          x1={x - NOTE_RADIUS - 3}
          y1={ly}
          x2={x + NOTE_RADIUS + 3}
          y2={ly}
          stroke="hsl(0 0% 40%)"
          strokeWidth={0.8}
        />
      );
    }

    // Accidental symbol
    if (acc) {
      elements.push(
        <text
          key={`acc-${index}`}
          x={x - NOTE_RADIUS - 9}
          y={y + 3.5}
          fontSize={11}
          fill={"hsl(0 0% 15%)"}
          opacity={opacity}
          fontFamily="'Noto Music', serif"
        >
          ♯
        </text>
      );
    }

    // Note head
    if (shape === 'whole') {
      // Hollow oval
      elements.push(
        <ellipse
          key={`note-${index}`}
          cx={x}
          cy={y}
          rx={NOTE_RADIUS + 1}
          ry={NOTE_RADIUS - 0.5}
          fill="none"
          stroke={"hsl(0 0% 15%)"}
          strokeWidth={1.5}
          opacity={opacity}
        />
      );
    } else if (shape === 'half') {
      // Hollow oval with stem
      elements.push(
        <ellipse
          key={`note-${index}`}
          cx={x}
          cy={y}
          rx={NOTE_RADIUS}
          ry={NOTE_RADIUS - 1}
          fill="none"
          stroke={"hsl(0 0% 15%)"}
          strokeWidth={1.5}
          opacity={opacity}
          transform={`rotate(-15, ${x}, ${y})`}
        />
      );
      elements.push(
        <line
          key={`stem-${index}`}
          x1={x + NOTE_RADIUS}
          y1={y}
          x2={x + NOTE_RADIUS}
          y2={y - 28}
          stroke={"hsl(0 0% 15%)"}
          strokeWidth={1.2}
          opacity={opacity}
        />
      );
    } else {
      // Filled oval (quarter, eighth, sixteenth)
      elements.push(
        <ellipse
          key={`note-${index}`}
          cx={x}
          cy={y}
          rx={NOTE_RADIUS}
          ry={NOTE_RADIUS - 1}
          fill={"hsl(0 0% 15%)"}
          opacity={opacity}
          transform={`rotate(-15, ${x}, ${y})`}
        />
      );
      // Stem
      elements.push(
        <line
          key={`stem-${index}`}
          x1={x + NOTE_RADIUS}
          y1={y}
          x2={x + NOTE_RADIUS}
          y2={y - 28}
          stroke={"hsl(0 0% 15%)"}
          strokeWidth={1.2}
          opacity={opacity}
        />
      );
      // Flags for eighth and sixteenth
      if (shape === 'eighth' || shape === 'sixteenth') {
        elements.push(
          <path
            key={`flag1-${index}`}
            d={`M${x + NOTE_RADIUS},${y - 28} Q${x + NOTE_RADIUS + 8},${y - 20} ${x + NOTE_RADIUS + 2},${y - 14}`}
            fill="none"
            stroke={"hsl(0 0% 15%)"}
            strokeWidth={1.2}
            opacity={opacity}
          />
        );
      }
      if (shape === 'sixteenth') {
        elements.push(
          <path
            key={`flag2-${index}`}
            d={`M${x + NOTE_RADIUS},${y - 24} Q${x + NOTE_RADIUS + 8},${y - 16} ${x + NOTE_RADIUS + 2},${y - 10}`}
            fill="none"
            stroke={"hsl(0 0% 15%)"}
            strokeWidth={1.2}
            opacity={opacity}
          />
        );
      }
    }

    // Tooltip via title
    elements.push(
      <title key={`title-${index}`}>
        {noteName(note.pitch)} — {note.duration} beat{note.duration !== 1 ? 's' : ''} (vel {note.velocity})
      </title>
    );

    return <g key={`notegroup-${index}`}>{elements}</g>;
  };

  return (
    <div className="midi-editor__sheet-music">
      <svg
        width={svgWidth}
        height={svgHeight}
        viewBox={`0 0 ${svgWidth} ${svgHeight}`}
        xmlns="http://www.w3.org/2000/svg"
      >
        {/* Staff lines */}
        {staffLines}

        {/* Bar lines */}
        {barLines}

        {/* Clefs */}
        {clefs}

        {/* Middle C reference line (dashed) */}
        <line
          x1={LEFT_MARGIN}
          y1={(TREBLE_TOP + (STAFF_LINES - 1) * STAFF_LINE_SPACING + BASS_TOP) / 2}
          x2={svgWidth - RIGHT_MARGIN}
          y2={(TREBLE_TOP + (STAFF_LINES - 1) * STAFF_LINE_SPACING + BASS_TOP) / 2}
          stroke="hsl(0 0% 35%)"
          strokeWidth={0.5}
          strokeDasharray="4 4"
          opacity={0.4}
        />

        {/* Notes */}
        {trebleNotes.map((n, i) => renderNote(n, i))}
        {bassNotes.map((n, i) => renderNote(n, i + trebleNotes.length))}
      </svg>
    </div>
  );
}
