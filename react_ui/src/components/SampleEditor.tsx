import { useMixerStore } from '@/data/store';

/**
 * SampleEditor — bottom-panel editor for audio clips.
 * Shows waveform with crop handles, loop toggle, quantize, and future inpaint/outpaint buttons.
 */
export function SampleEditor() {
  const { sampleEditorOpen, selectedAudioClip, tracks, closeSampleEditor } = useMixerStore();

  if (!sampleEditorOpen || !selectedAudioClip) return null;

  const track = tracks.find((t) => t.id === selectedAudioClip.trackId);
  const clip = track?.audioClips?.find((c) => c.id === selectedAudioClip.clipId);

  if (!clip) return null;

  const fileName = clip.filePath.split('/').pop() ?? 'Unknown';

  return (
    <div className={panel}>
      {/* Header */}
      <div className={header}>
        <div className={headerLeft}>
          <div className={headerDot} style={{ backgroundColor: track?.color ?? '#888' }} />
          <span className={headerTitle}>{track?.name}</span>
          <span className={headerSeparator}>›</span>
          <span className={headerFileName}>{fileName}</span>
        </div>
        <div className={headerActions}>
          <button
            className={`${actionBtn} ${clip.looping ? actionBtnActive : ''}`}
            title="Toggle loop"
          >
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <polyline points="17 1 21 5 17 9"/>
              <path d="M3 11V9a4 4 0 0 1 4-4h14"/>
              <polyline points="7 23 3 19 7 15"/>
              <path d="M21 13v2a4 4 0 0 1-4 4H3"/>
            </svg>
            Loop
          </button>
          <button className={actionBtn} title="Quantize to grid">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <rect x="3" y="3" width="18" height="18" rx="2"/>
              <line x1="3" y1="9" x2="21" y2="9"/>
              <line x1="3" y1="15" x2="21" y2="15"/>
              <line x1="9" y1="3" x2="9" y2="21"/>
              <line x1="15" y1="3" x2="15" y2="21"/>
            </svg>
            Quantize
          </button>
          <div className={divider} />
          <button className={`${actionBtn} opacity-30 cursor-not-allowed`} disabled title="Inpaint (coming soon)">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M12 20h9"/>
              <path d="M16.5 3.5a2.121 2.121 0 0 1 3 3L7 19l-4 1 1-4L16.5 3.5z"/>
            </svg>
            Inpaint
          </button>
          <button className={`${actionBtn} opacity-30 cursor-not-allowed`} disabled title="Outpaint (coming soon)">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <polyline points="15 3 21 3 21 9"/>
              <polyline points="9 21 3 21 3 15"/>
              <line x1="21" y1="3" x2="14" y2="10"/>
              <line x1="3" y1="21" x2="10" y2="14"/>
            </svg>
            Outpaint
          </button>
          <div className={divider} />
          <button className={closeBtn} onClick={closeSampleEditor} title="Close">
            ✕
          </button>
        </div>
      </div>

      {/* Waveform area */}
      <div className={waveformArea}>
        {/* Crop region */}
        <div className={cropOverlayLeft} style={{ width: `${(clip.cropStart / (clip.cropEnd || 1)) * 100}%` }} />
        <div className={cropOverlayRight} style={{ width: `${Math.max(0, 100 - (clip.cropEnd / (clip.duration || 1)) * 100)}%` }} />

        {/* Waveform visualization placeholder */}
        <div className={waveformContainer}>
          {Array.from({ length: 80 }, (_, i) => {
            const h = 15 + Math.sin(i * 0.4) * 25 + Math.sin(i * 0.15) * 20 + Math.sin(i * 1.7 + 5) * 10;
            return (
              <div
                key={i}
                className={waveformBarStyle}
                style={{
                  height: `${h}%`,
                  backgroundColor: track?.color ?? '#888',
                }}
              />
            );
          })}
        </div>

        {/* Crop handles */}
        <div className={cropHandle} style={{ left: `${(clip.cropStart / (clip.cropEnd || 1)) * 100}%` }}>
          <div className={cropHandleGrip} />
        </div>
        <div className={cropHandle} style={{ left: `${((clip.cropEnd || clip.duration) / (clip.duration || 1)) * 100}%` }}>
          <div className={cropHandleGrip} />
        </div>
      </div>

      {/* Info bar */}
      <div className={infoBar}>
        <span className={infoItem}>Start: {clip.startBeat.toFixed(1)} beats</span>
        <span className={infoItem}>Duration: {clip.duration.toFixed(1)} beats</span>
        <span className={infoItem}>Crop: {clip.cropStart.toFixed(2)}s – {clip.cropEnd.toFixed(2)}s</span>
        {clip.looping && <span className={`${infoItem} ${infoHighlight}`}>🔁 Looping</span>}
        {clip.quantized && <span className={`${infoItem} ${infoHighlight}`}>📐 Quantized</span>}
      </div>
    </div>
  );
}

// --- Styles ---

const panel = `
  bg-[hsl(var(--mixer))] border-t border-[hsl(var(--border))]
  h-64 flex flex-col overflow-hidden`;

const header = `
  flex items-center justify-between px-3 py-2
  border-b border-[hsl(var(--border))]/50
  bg-[hsl(var(--background))]/50`;

const headerLeft = `flex items-center gap-2`;
const headerDot = `w-2.5 h-2.5 rounded-full shrink-0`;
const headerTitle = `text-xs font-semibold text-[hsl(var(--foreground))]`;
const headerSeparator = `text-xs text-[hsl(var(--muted-foreground))]`;
const headerFileName = `text-xs text-[hsl(var(--muted-foreground))] font-mono`;

const headerActions = `flex items-center gap-1`;

const actionBtn = `
  flex items-center gap-1 px-2 py-1 rounded text-[10px] font-medium
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  hover:bg-[hsl(var(--muted))]/50 transition-colors`;

const actionBtnActive = `
  bg-[hsl(var(--primary))]/20 text-[hsl(var(--primary))]
  hover:bg-[hsl(var(--primary))]/30`;

const divider = `w-px h-4 bg-[hsl(var(--border))]/50 mx-1`;

const closeBtn = `
  w-6 h-6 flex items-center justify-center rounded
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  hover:bg-[hsl(var(--muted))]/50 transition-colors text-xs`;

const waveformArea = `
  flex-1 relative mx-3 my-2 rounded-md overflow-hidden
  bg-[hsl(var(--background))]/30 border border-[hsl(var(--border))]/30`;

const cropOverlayLeft = `
  absolute inset-y-0 left-0 bg-black/30 z-10 pointer-events-none`;

const cropOverlayRight = `
  absolute inset-y-0 right-0 bg-black/30 z-10 pointer-events-none`;

const waveformContainer = `
  absolute inset-0 flex items-center gap-px px-2 overflow-hidden`;

const waveformBarStyle = `
  w-[2px] min-w-[2px] rounded-full opacity-60`;

const cropHandle = `
  absolute inset-y-0 w-1 z-20 cursor-col-resize
  hover:bg-[hsl(var(--primary))]/50 transition-colors`;

const cropHandleGrip = `
  absolute top-1/2 -translate-y-1/2 left-1/2 -translate-x-1/2
  w-2 h-8 rounded-full bg-[hsl(var(--foreground))]/40
  hover:bg-[hsl(var(--foreground))]/60 transition-colors`;

const infoBar = `
  flex items-center gap-4 px-3 py-1.5
  border-t border-[hsl(var(--border))]/30
  bg-[hsl(var(--background))]/30`;

const infoItem = `text-[9px] text-[hsl(var(--muted-foreground))] font-mono`;
const infoHighlight = `text-[hsl(var(--primary))]`;
