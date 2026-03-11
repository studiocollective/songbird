import { useRef, useEffect } from 'react';
import { subscribeRtBuffer } from '@/data/meters';
import type { CpuData } from '@/data/meters';

/**
 * Compact CPU meter for the transport bar.
 * Shows a small bar + percentage that turns amber > red as load increases.
 * Uses direct DOM manipulation via subscribeRtBuffer — no React re-renders for CPU changes.
 */
export function CpuMeter() {
  const fillRef = useRef<HTMLDivElement>(null);
  const pctRef = useRef<HTMLSpanElement>(null);
  const wrapRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const unsub = subscribeRtBuffer((buf) => {
      const pct = Math.min(buf.cpuData.cpu, 100);
      const color =
        pct > 80 ? 'hsl(0 80% 55%)' :
        pct > 50 ? 'hsl(35 90% 55%)' :
        'hsl(var(--progress))';

      if (fillRef.current) {
        fillRef.current.style.transform = `scaleX(${pct / 100})`;
        fillRef.current.style.backgroundColor = color;
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
      <div className={meterTrack}>
        <div ref={fillRef} className={meterFill} />
      </div>
      <span ref={pctRef} className={meterPct}>0%</span>
    </div>
  );
}

/**
 * Per-track CPU breakdown panel.
 * Toggled from the stereo metering section.
 *
 * Note: Per-track CPU estimation was removed when DropoutDetector stopped
 * emitting its own cpuStats event. The panel now shows only overall CPU.
 * To re-add per-track estimates, include track data in the rtFrame payload.
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

// Keep the CpuData type re-export for any external consumers
export type { CpuData };

// ─── Tailwind classes ───

const meterWrap = `flex items-center gap-1.5 px-2`;
const meterLabel = `text-[9px] uppercase tracking-wider text-[hsl(var(--muted-foreground))] font-semibold`;
const meterTrack = `w-12 h-1.5 bg-[hsl(var(--border))] rounded-full overflow-hidden`;
const meterFill = `h-full w-full rounded-full origin-left will-change-transform`;
const meterPct = `text-[10px] font-mono min-w-[28px] text-right`;

const panelWrap = `
  bg-[hsl(var(--card))] border border-[hsl(var(--border))] rounded-lg
  shadow-lg p-2 w-48`;
const panelHeader = `flex justify-between items-center mb-2 px-1`;
