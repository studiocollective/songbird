import { useMixerStore } from '@/data/store';

export function ArrangementView() {
  const { tracks } = useMixerStore();
  const totalBars = 32;
  const sections = [
    { name: 'Verse', start: 0, length: 8, color: 'bg-indigo-900/40 dark:bg-indigo-900/40 bg-indigo-200/40' },
    { name: 'Chorus', start: 8, length: 8, color: 'bg-rose-900/40 dark:bg-rose-900/40 bg-rose-200/40' },
    { name: 'Verse', start: 16, length: 8, color: 'bg-indigo-900/40 dark:bg-indigo-900/40 bg-indigo-200/40' },
    { name: 'Chorus', start: 24, length: 8, color: 'bg-rose-900/40 dark:bg-rose-900/40 bg-rose-200/40' },
  ];

  return (
    <div className={container}>
      {/* Timeline ruler */}
      <div className={rulerRow}>
        <div className={rulerSpacer} />
        <div className={rulerTrack}>
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
              {sections.map((sec, i) => (
                <div
                  key={i}
                  className={`${sectionBg} ${sec.color}`}
                  style={{
                    '--sec-left': `${(sec.start / totalBars) * 100}%`,
                    '--sec-width': `${(sec.length / totalBars) * 100}%`,
                  } as React.CSSProperties}
                />
              ))}
              <div
                className={clip}
                style={{
                  '--clip-left': `${((track.id % 3) * 2 / totalBars) * 100}%`,
                  '--clip-width': `${((8 - (track.id % 3)) / totalBars) * 100}%`,
                  '--clip-color': track.color,
                } as React.CSSProperties}
              >
                <div className={clipLabel}>{track.name}</div>
                <div className={notesContainer}>
                  {Array.from({ length: 6 + (track.id % 4) }, (_, j) => (
                    <div
                      key={j}
                      className={noteLine}
                      style={{
                        '--note-w': `${4 + (j % 3) * 3}px`,
                        '--note-mt': `${(j * 2) % 5}px`,
                      } as React.CSSProperties}
                    />
                  ))}
                </div>
              </div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

const container = `flex-1 flex flex-col overflow-hidden bg-[hsl(var(--arrangement))]`;

const rulerRow = `flex shrink-0`;
const rulerSpacer = `w-44 shrink-0 bg-[hsl(var(--background))] border-b border-r border-[hsl(var(--border))]`;
const rulerTrack = `
  flex-1 h-10 bg-[hsl(var(--background))] border-b border-[hsl(var(--border))]
  flex items-end relative overflow-hidden`;

const sectionLabel = `
  absolute top-0 h-5 flex items-center justify-center
  text-[10px] font-medium text-[hsl(var(--muted-foreground))]
  border-x border-[hsl(var(--border))]/50
  left-[var(--label-left)] w-[var(--label-width)]`;

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
const sectionBg = `
  absolute inset-y-0 border-r border-[hsl(var(--border))]/30
  left-[var(--sec-left)] w-[var(--sec-width)]`;

const clip = `
  absolute top-1.5 bottom-1.5 rounded-sm opacity-60
  left-[var(--clip-left)] w-[var(--clip-width)]
  bg-[var(--clip-color)]`;
const clipLabel = `px-1.5 py-0.5 text-[9px] font-medium text-white/80 truncate`;
const notesContainer = `px-1.5 flex gap-px flex-wrap`;
const noteLine = `h-0.5 rounded-full bg-white/40 w-[var(--note-w)] mt-[var(--note-mt)]`;
