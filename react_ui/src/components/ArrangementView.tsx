import { useMixerStore } from '@/data/store';
import type { NoteData } from '@/data/slices/mixer';

export function ArrangementView() {
  const { tracks } = useMixerStore();

  // Compute total bars from the maximum note beat position across all tracks
  const maxBeat = tracks.reduce((max, track) => {
    const trackMax = track.notes.reduce((m, n) => Math.max(m, n.beat + n.duration), 0);
    return Math.max(max, trackMax);
  }, 4); // minimum 4 beats = 1 bar
  const totalBars = Math.max(1, Math.ceil(maxBeat / 4));

  return (
    <div className={container}>
      {/* Timeline ruler */}
      <div className={rulerRow}>
        <div className={rulerSpacer} />
        <div className={rulerTrack}>
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
        </div>
      </div>

      {/* Track lanes */}
      <div className={lanesScroll}>
        {tracks.map((track) => (
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
                <NoteClip
                  notes={track.notes}
                  color={track.color}
                  totalBars={totalBars}
                />
              )}
            </div>
          </div>
        ))}
      </div>
    </div>
  );
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

  // Find pitch range for vertical positioning
  const minPitch = notes.reduce((m, n) => Math.min(m, n.pitch), 127);
  const maxPitch = notes.reduce((m, n) => Math.max(m, n.pitch), 0);
  const pitchRange = Math.max(maxPitch - minPitch, 1);

  // Find time range
  const minBeat = notes.reduce((m, n) => Math.min(m, n.beat), Infinity);
  const maxBeatEnd = notes.reduce((m, n) => Math.max(m, n.beat + n.duration), 0);
  const totalBeats = totalBars * 4;

  // Clip region
  const clipLeft = (minBeat / totalBeats) * 100;
  const clipWidth = ((maxBeatEnd - minBeat) / totalBeats) * 100;

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
          const y = maxPitch - note.pitch; // high notes at top
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

const gridLine = `
  absolute inset-y-0 w-px bg-[hsl(var(--border))]/30
  left-[var(--grid-left)]`;

const clip = `
  absolute top-1.5 bottom-1.5 rounded-sm overflow-hidden
  left-[var(--clip-left)] w-[var(--clip-width)]
  bg-[var(--clip-color)]/30 border border-[var(--clip-color)]/50`;

const notesSvg = `w-full h-full`;
