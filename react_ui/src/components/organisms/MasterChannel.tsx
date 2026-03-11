import { useRef, useEffect } from 'react';
import { subscribeRtBuffer } from '@/data/meters';

interface MasterChannelProps {
  onToggleRecordStrip?: () => void;
  recordStripOpen?: boolean;
  onToggleReturns?: () => void;
  returnsOpen?: boolean;
}

export function MasterChannel({ onToggleRecordStrip, recordStripOpen, onToggleReturns, returnsOpen }: MasterChannelProps) {
  const leftCanvasRef = useRef<HTMLCanvasElement>(null);
  const rightCanvasRef = useRef<HTMLCanvasElement>(null);
  const leftCtxRef = useRef<CanvasRenderingContext2D | null>(null);
  const rightCtxRef = useRef<CanvasRenderingContext2D | null>(null);
  const readoutRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (leftCanvasRef.current) leftCtxRef.current = leftCanvasRef.current.getContext('2d');
    if (rightCanvasRef.current) rightCtxRef.current = rightCanvasRef.current.getContext('2d');
  }, []);

  useEffect(() => {
    const unsub = subscribeRtBuffer((buf) => {
      const { master } = buf;

      // Draw left channel
      drawMeterBar(leftCtxRef.current, leftCanvasRef.current, master.left);
      // Draw right channel
      drawMeterBar(rightCtxRef.current, rightCanvasRef.current, master.right);

      if (readoutRef.current) {
        const avg = Math.max(master.left, master.right);
        readoutRef.current.textContent = avg > 0
          ? `-${(60 - avg * 0.6).toFixed(1)}`
          : '-∞';
      }
    });
    return unsub;
  }, []);

  return (
    <div className={channel}>
      <span className={label}>Master</span>
      <div className={meterWrapper}>
        <div className={meterTrack}>
          <canvas ref={leftCanvasRef} width={6} height={112} className="w-full h-full rounded-full" />
        </div>
        <div className={meterTrack}>
          <canvas ref={rightCanvasRef} width={6} height={112} className="w-full h-full rounded-full" />
        </div>
      </div>
      <div ref={readoutRef} className={readout}>-∞</div>
      <div className="flex gap-1 mt-1">
        {onToggleRecordStrip && (
          <button
            onClick={onToggleRecordStrip}
            className={[
              "w-6 h-6 rounded flex items-center justify-center text-[10px] font-bold transition-colors",
              recordStripOpen
                ? "bg-white text-black"
                : "bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--muted))]/80"
            ].join(' ')}
            title="Toggle Input / Record Strip"
          >
            I
          </button>
        )}
        {onToggleReturns && (
          <button
            onClick={onToggleReturns}
            className={[
              "w-6 h-6 rounded flex items-center justify-center text-[10px] font-bold transition-colors",
              returnsOpen
                ? "bg-white text-black"
                : "bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--muted))]/80"
            ].join(' ')}
            title="Toggle Return Tracks & Sends"
          >
            R
          </button>
        )}
      </div>
    </div>
  );
}

function drawMeterBar(ctx: CanvasRenderingContext2D | null, canvas: HTMLCanvasElement | null, level: number) {
  if (!ctx || !canvas) return;
  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  const levelH = (level / 100) * h;
  if (levelH <= 0) return;

  const grad = ctx.createLinearGradient(0, h, 0, 0);
  grad.addColorStop(0, 'rgba(16,185,129,0.8)');   // emerald-500
  grad.addColorStop(0.7, 'rgba(52,211,153,1)');    // emerald-400
  grad.addColorStop(1, 'rgba(239,68,68,1)');       // red-500
  ctx.fillStyle = grad;
  ctx.fillRect(0, h - levelH, w, levelH);

  // Segment lines (like the original dividers)
  ctx.fillStyle = 'hsl(var(--mixer))';
  for (let i = 1; i < 12; i++) {
    const y = Math.round((i / 12) * h);
    ctx.fillRect(0, y, w, 1);
  }
}

const channel = `
  w-16 shrink-0 border-r border-[hsl(var(--border))]
  flex flex-col items-center py-2 bg-[hsl(var(--mixer))]`;
const label = `text-[9px] text-[hsl(var(--muted-foreground))] uppercase tracking-widest mb-2`;
const meterWrapper = `flex-1 flex items-center justify-center gap-0.5`;
const meterTrack = `w-1.5 h-28 bg-[hsl(var(--card))] rounded-full overflow-hidden`;
const readout = `text-[10px] font-mono text-[hsl(var(--muted-foreground))] mt-1`;
