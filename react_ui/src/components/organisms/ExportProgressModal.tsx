import { useEffect, useState } from 'react';
import { addStateListener } from '@/data/bridge';

interface ExportProgress {
  current: number;
  total: number;
  name: string;
}

export function ExportProgressModal() {
  const [progress, setProgress] = useState<ExportProgress | null>(null);

  useEffect(() => {
    const removeProgress = addStateListener('exportProgress', (data) => {
      setProgress(data as ExportProgress);
    });

    const removeDone = addStateListener('exportDone', () => {
      // Brief pause so user sees 100% before dismissing
      setTimeout(() => setProgress(null), 800);
    });

    return () => {
      removeProgress();
      removeDone();
    };
  }, []);

  if (!progress) return null;

  const pct = progress.total > 0
    ? Math.round((progress.current / progress.total) * 100)
    : 0;

  return (
    <div className={overlay}>
      <div className={modal}>
        <p className={heading}>Exporting Stems</p>

        <div className={barTrack}>
          <div className={barFill} style={{ width: `${pct}%` }} />
        </div>

        <p className={stats}>
          {progress.current} / {progress.total}
          {progress.name ? ` — ${progress.name}` : ''}
        </p>
      </div>
    </div>
  );
}

const overlay = `
  fixed inset-0 z-[100] flex items-center justify-center
  bg-black/60 backdrop-blur-sm`;

const modal = `
  w-80 rounded-xl bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  px-6 py-5 shadow-2xl flex flex-col gap-4`;

const heading = `text-sm font-medium text-[hsl(var(--foreground))] text-center tracking-wide`;

const barTrack = `
  w-full h-2 rounded-full bg-[hsl(var(--muted))] overflow-hidden`;

const barFill = `
  h-full rounded-full bg-[hsl(var(--progress))]
  transition-[width] duration-300 ease-out`;

const stats = `
  text-xs text-[hsl(var(--muted-foreground))] text-center font-mono truncate`;
