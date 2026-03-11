import { useRef, useEffect } from 'react';
import { subscribeRtBuffer } from '@/data/meters';
import type { CpuData } from '@/data/meters';

const CPU_W = 48, CPU_H = 6;

/**
 * Compact CPU meter for the transport bar.
 * Canvas-based fill + DOM text readout.
 */
export function CpuMeter() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const ctxRef = useRef<CanvasRenderingContext2D | null>(null);
  const pctRef = useRef<HTMLSpanElement>(null);
  const wrapRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const dpr = window.devicePixelRatio || 1;
    const canvas = canvasRef.current;
    if (canvas) {
      canvas.width = CPU_W * dpr;
      canvas.height = CPU_H * dpr;
      const ctx = canvas.getContext('2d');
      if (ctx) { ctx.scale(dpr, dpr); ctxRef.current = ctx; }
    }
  }, []);

  useEffect(() => {
    const unsub = subscribeRtBuffer((buf) => {
      const ctx = ctxRef.current;
      const pct = Math.min(buf.cpuData.cpu, 100);
      const color =
        pct > 80 ? 'hsl(0,80%,55%)' :
        pct > 50 ? 'hsl(35,90%,55%)' :
        'hsl(142,70%,45%)';

      if (ctx) {
        ctx.clearRect(0, 0, CPU_W, CPU_H);
        ctx.fillStyle = color;
        ctx.fillRect(0, 0, (pct / 100) * CPU_W, CPU_H);
      }
      if (pctRef.current) {
        pctRef.current.textContent = `${pct.toFixed(0)}%`;
        pctRef.current.style.color = color;
      }
      if (wrapRef.current) {
        wrapRef.current.title = `CPU: ${pct.toFixed(1)}% | Buffer: ${buf.cpuData.bufferSize} @ ${(buf.cpuData.sampleRate / 1000).toFixed(1)}kHz`;
      }
    });
    return unsub;
  }, []);

  return (
    <div ref={wrapRef} className={meterWrap} title="CPU: 0%">
      <span className={meterLabel}>CPU</span>
      <canvas
        ref={canvasRef}
        style={{ width: CPU_W, height: CPU_H }}
        className="rounded-full"
      />
      <span ref={pctRef} className={meterPct}>0%</span>
    </div>
  );
}

/**
 * Per-track CPU breakdown panel.
 */
export function TrackCpuPanel({ open }: { open: boolean }) {
  const labelRef = useRef<HTMLSpanElement>(null);

  useEffect(() => {
    const unsub = subscribeRtBuffer((buf) => {
      if (labelRef.current) {
        labelRef.current.textContent = `Total: ${buf.cpuData.cpu.toFixed(1)}%`;
      }
    });
    return unsub;
  }, []);

  if (!open) return null;

  return (
    <div className={panelWrap}>
      <div className={panelHeader}>
        <span className="text-[10px] uppercase tracking-wider font-semibold text-[hsl(var(--muted-foreground))]">
          CPU
        </span>
        <span ref={labelRef} className="text-[10px] text-[hsl(var(--muted-foreground))]">
          Total: 0.0%
        </span>
      </div>
    </div>
  );
}

export type { CpuData };

const meterWrap = `flex items-center gap-1.5 px-2`;
const meterLabel = `text-[9px] uppercase tracking-wider text-[hsl(var(--muted-foreground))] font-semibold`;
const meterPct = `text-[10px] font-mono min-w-[28px] text-right`;

const panelWrap = `
  bg-[hsl(var(--card))] border border-[hsl(var(--border))] rounded-lg
  shadow-lg p-2 w-48`;
const panelHeader = `flex justify-between items-center mb-2 px-1`;
