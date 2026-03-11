import { useRef, useEffect } from 'react';
import { subscribeRtBuffer } from '@/data/meters';

// ─── Layout constants ───
const W = 80, H = 70;
const cx = W / 2, cy = H / 2 + 4;
const r = 28;
const sin60 = Math.sin(Math.PI / 3);
const cos60 = Math.cos(Math.PI / 3);
const baseY = cy + r * cos60;
const peakY = cy - r;
const maxHalfWidth = r * sin60;

/**
 * BalanceOMeter — morphing triangle inscribed in a circle.
 * Canvas-based: zero layout recalculation.
 */
export function StereoWidthMeter() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const ctxRef = useRef<CanvasRenderingContext2D | null>(null);

  useEffect(() => {
    if (canvasRef.current) ctxRef.current = canvasRef.current.getContext('2d');
  }, []);

  useEffect(() => {
    const dpr = window.devicePixelRatio || 1;
    const canvas = canvasRef.current;
    if (canvas) {
      canvas.width = W * dpr;
      canvas.height = H * dpr;
      const ctx = canvas.getContext('2d');
      if (ctx) {
        ctx.scale(dpr, dpr);
        ctxRef.current = ctx;
      }
    }
  }, []);

  useEffect(() => {
    const unsub = subscribeRtBuffer((buf) => {
      const ctx = ctxRef.current;
      if (!ctx) return;

      ctx.clearRect(0, 0, W, H);

      const { phaseCorrelation: corr } = buf;
      const { left, right } = buf.master;
      const linL = left / 100;
      const linR = right / 100;
      const peak = Math.max(linL, linR, 0.001);

      const scaleTop = Math.max(0.15, ((corr + 1) / 2) * peak);
      const balance = (linR - linL) / (linL + linR || 1);
      const sideActivity = 1 - Math.max(0, corr);
      const scaleLeft = Math.max(0.15, (sideActivity + (balance < 0 ? -balance : 0)) * peak);
      const scaleRight = Math.max(0.15, (sideActivity + (balance > 0 ? balance : 0)) * peak);

      const tx = cx;
      const ty = baseY - (baseY - peakY) * scaleTop;
      const lx = cx - maxHalfWidth * scaleLeft;
      const ly = baseY;
      const rx = cx + maxHalfWidth * scaleRight;
      const ry = baseY;

      // Reference circle
      ctx.strokeStyle = 'hsla(0,0%,60%,0.25)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, Math.PI * 2);
      ctx.stroke();

      // Reference equilateral triangle (dashed)
      const refTop = { x: cx, y: cy - r };
      const refLeft = { x: cx - sin60 * r, y: cy + cos60 * r };
      const refRight = { x: cx + sin60 * r, y: cy + cos60 * r };
      ctx.setLineDash([2, 2]);
      ctx.strokeStyle = 'hsla(0,0%,60%,0.2)';
      ctx.lineWidth = 0.5;
      ctx.beginPath();
      ctx.moveTo(refTop.x, refTop.y);
      ctx.lineTo(refLeft.x, refLeft.y);
      ctx.lineTo(refRight.x, refRight.y);
      ctx.closePath();
      ctx.stroke();
      ctx.setLineDash([]);

      // Vertex dots
      ctx.fillStyle = 'hsla(0,0%,60%,0.35)';
      for (const pt of [refTop, refLeft, refRight]) {
        ctx.beginPath();
        ctx.arc(pt.x, pt.y, 2, 0, Math.PI * 2);
        ctx.fill();
      }

      // Live morphing triangle
      ctx.strokeStyle = 'hsl(var(--progress))';
      ctx.lineWidth = 1.5;
      ctx.lineJoin = 'round';
      ctx.globalAlpha = 0.9;
      ctx.beginPath();
      ctx.moveTo(tx, ty);
      ctx.lineTo(lx, ly);
      ctx.lineTo(rx, ry);
      ctx.closePath();
      ctx.stroke();
      ctx.globalAlpha = 1;

      // Labels
      ctx.font = '6px sans-serif';
      ctx.fillStyle = 'hsla(0,0%,60%,0.7)';
      ctx.textAlign = 'center';
      ctx.fillText('Mid/Side', cx, cy - r - 5);
      ctx.textAlign = 'right';
      ctx.fillText('Left', cx - r - 3, cy + r * cos60 + 8);
      ctx.textAlign = 'left';
      ctx.fillText('Right', cx + r + 3, cy + r * cos60 + 8);
    });
    return unsub;
  }, []);

  return (
    <div className={wrapper}>
      <canvas ref={canvasRef} style={{ width: W, height: H }} className="shrink-0" />
    </div>
  );
}


/**
 * PhaseCorrelationMeter — horizontal bar from -1 to +1.
 * Canvas-based.
 */
const PHASE_W = 64, PHASE_H = 8;

export function PhaseCorrelationMeter() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const ctxRef = useRef<CanvasRenderingContext2D | null>(null);

  useEffect(() => {
    const dpr = window.devicePixelRatio || 1;
    const canvas = canvasRef.current;
    if (canvas) {
      canvas.width = PHASE_W * dpr;
      canvas.height = PHASE_H * dpr;
      const ctx = canvas.getContext('2d');
      if (ctx) { ctx.scale(dpr, dpr); ctxRef.current = ctx; }
    }
  }, []);

  useEffect(() => {
    const unsub = subscribeRtBuffer((buf) => {
      const ctx = ctxRef.current;
      if (!ctx) return;

      const correlation = buf.phaseCorrelation;
      const pct = (correlation + 1) / 2; // 0..1

      let color: string;
      if (correlation > 0.5) color = 'rgb(34,197,94)';
      else if (correlation > 0) color = 'rgb(234,179,8)';
      else color = 'rgb(239,68,68)';

      ctx.clearRect(0, 0, PHASE_W, PHASE_H);

      // Scale marks
      ctx.fillStyle = 'hsla(0,0%,60%,0.2)';
      ctx.fillRect(0, 0, 1, PHASE_H);
      ctx.fillRect(PHASE_W / 2, 0, 1, PHASE_H);
      ctx.fillRect(PHASE_W - 1, 0, 1, PHASE_H);

      // Fill bar
      ctx.globalAlpha = 0.6;
      ctx.fillStyle = color;
      const fillW = pct * PHASE_W;
      ctx.fillRect(0, 0, fillW, PHASE_H);

      // Indicator line
      ctx.globalAlpha = 1;
      ctx.fillStyle = color;
      ctx.fillRect(fillW - 1, 0, 2, PHASE_H);
    });
    return unsub;
  }, []);

  return (
    <div className={wrapper}>
      <span className={label}>PHASE</span>
      <canvas
        ref={canvasRef}
        style={{ width: PHASE_W, height: PHASE_H }}
        className="rounded-full border border-[hsl(var(--border))]"
      />
      <div className={phaseLabels}>
        <span>−1</span>
        <span>0</span>
        <span>+1</span>
      </div>
    </div>
  );
}

/**
 * SpectrumAnalyzer — 16-band FFT visualizer.
 * Canvas-based.
 */
const NUM_BANDS = 16;
const BAR_WIDTH = 6;
const GAP = 1.5;
const SPEC_W = NUM_BANDS * (BAR_WIDTH + GAP) - GAP;
const SPEC_H = 48;

export function SpectrumAnalyzer() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const ctxRef = useRef<CanvasRenderingContext2D | null>(null);

  useEffect(() => {
    const dpr = window.devicePixelRatio || 1;
    const canvas = canvasRef.current;
    if (canvas) {
      canvas.width = SPEC_W * dpr;
      canvas.height = SPEC_H * dpr;
      const ctx = canvas.getContext('2d');
      if (ctx) { ctx.scale(dpr, dpr); ctxRef.current = ctx; }
    }
  }, []);

  useEffect(() => {
    const unsub = subscribeRtBuffer((buf) => {
      const ctx = ctxRef.current;
      if (!ctx) return;

      const spectrum = buf.spectrum;
      if (!spectrum || spectrum.length === 0) return;

      ctx.clearRect(0, 0, SPEC_W, SPEC_H);
      ctx.fillStyle = 'rgb(52,211,153)'; // emerald

      for (let i = 0; i < Math.min(NUM_BANDS, spectrum.length); i++) {
        const val = Math.sqrt(Math.max(0, spectrum[i]));
        const h = Math.max(1, val * SPEC_H);
        const x = i * (BAR_WIDTH + GAP);

        // Rounded rect approximation
        ctx.beginPath();
        ctx.roundRect(x, SPEC_H - h, BAR_WIDTH, h, 1);
        ctx.fill();
      }
    });
    return unsub;
  }, []);

  return (
    <div className={wrapper}>
      <span className={label}>SPECTRUM</span>
      <div className="bg-[hsl(var(--card))] border border-[hsl(var(--border))] rounded p-1">
        <canvas ref={canvasRef} style={{ width: SPEC_W, height: SPEC_H }} />
      </div>
    </div>
  );
}

const wrapper = `flex flex-col items-center gap-1`;
const label = `text-[9px] uppercase tracking-wider text-[hsl(var(--muted-foreground))] font-medium`;
const phaseLabels = `
  flex justify-between w-16
  text-[7px] text-[hsl(var(--muted-foreground))] leading-none`;
