import { useRef, useEffect } from 'react';
import { useMeterStore } from '@/data/meters';

/**
 * BalanceOMeter — morphing triangle inscribed in a circle.
 *   Top vertex   = Mid (center) content
 *   Bottom-left  = Left channel
 *   Bottom-right = Right channel
 * A balanced stereo mix → equilateral triangle touching the circle.
 * Uses direct DOM manipulation for zero-overhead updates.
 */
const W = 80, H = 70;
const cx = W / 2, cy = H / 2 + 4;
const r = 28;
const sin60 = Math.sin(Math.PI / 3); // ≈0.866
const cos60 = Math.cos(Math.PI / 3); // ≈0.5

const topDir   = { x: 0,      y: -1    };
const leftDir  = { x: -sin60, y: cos60 };
const rightDir = { x:  sin60, y: cos60 };

export function StereoWidthMeter() {
  const polyRef = useRef<SVGPolygonElement>(null);

  useEffect(() => {
    const unsub = useMeterStore.subscribe((state) => {
      const { phaseCorrelation: corr } = state; // -1 to +1
      const { left, right } = state.master; // 0–100
      
      const linL = left / 100;
      const linR = right / 100;
      const peak = Math.max(linL, linR, 0.001);

      // Ensure it never shrinks completely to a point, keep a 15% base size
      const scaleTop = Math.max(0.15, ((corr + 1) / 2) * peak);

      const balance = (linR - linL) / (linL + linR || 1); 
      const sideActivity = 1 - Math.max(0, corr); 
      
      const scaleLeft  = Math.max(0.15, (sideActivity + (balance < 0 ? -balance : 0)) * peak);
      const scaleRight = Math.max(0.15, (sideActivity + (balance > 0 ?  balance : 0)) * peak);

      const baseY = cy + r * cos60;
      const peakY = cy - r; // top peak is at cy + topDir.y * r
      const maxHalfWidth = r * sin60;

      // Top vertex: X is always centered, Y scales from base up to peak
      const tx = cx;
      const ty = baseY - (baseY - peakY) * scaleTop;

      // Left vertex: Y is always at base, X scales from center out to left edge
      const lx = cx - maxHalfWidth * scaleLeft;
      const ly = baseY;

      // Right vertex: Y is always at base, X scales from center out to right edge
      const rx = cx + maxHalfWidth * scaleRight;
      const ry = baseY;

      if (polyRef.current) {
        polyRef.current.setAttribute('points', `${tx},${ty} ${lx},${ly} ${rx},${ry}`);
      }
    });
    return unsub;
  }, []);

  // Reference equilateral triangle points (at full scale)
  const refTop   = `${cx + topDir.x   * r},${cy + topDir.y   * r}`;
  const refLeft  = `${cx + leftDir.x  * r},${cy + leftDir.y  * r}`;
  const refRight = `${cx + rightDir.x * r},${cy + rightDir.y * r}`;

  return (
    <div className={wrapper}>
      <svg width={W} height={H} viewBox={`0 0 ${W} ${H}`} className="shrink-0 overflow-visible">
        {/* Circle outline */}
        <circle cx={cx} cy={cy} r={r}
          fill="none" stroke="hsl(var(--muted-foreground))" strokeWidth="1" opacity="0.25" />
        {/* Reference equilateral triangle (target shape for balanced mix) */}
        <polygon points={`${refTop} ${refLeft} ${refRight}`}
          fill="none" stroke="hsl(var(--muted-foreground))" strokeWidth="0.5" opacity="0.2"
          strokeDasharray="2 2" />
        {/* Vertex dots on the reference circle */}
        {[refTop, refLeft, refRight].map((pt, i) => {
          const [px, py] = pt.split(',').map(Number);
          return <circle key={i} cx={px} cy={py} r="2"
            fill="hsl(var(--muted-foreground))" opacity="0.35" />;
        })}
        {/* Live morphing triangle */}
        <polygon
          ref={polyRef}
          points={`${refTop} ${refLeft} ${refRight}`}
          fill="none"
          stroke="hsl(var(--progress))"
          strokeWidth="1.5"
          strokeLinejoin="round"
          opacity="0.9"
          style={{ transition: 'all 0.15s ease-out' }}
        />
        {/* Labels */}
        <text x={cx} y={cy - r - 5} textAnchor="middle"
          fontSize="6" fill="hsl(var(--muted-foreground))" opacity="0.7">Mid/Side</text>
        <text x={cx - r - 3} y={cy + r * cos60 + 8} textAnchor="end"
          fontSize="6" fill="hsl(var(--muted-foreground))" opacity="0.7">Left</text>
        <text x={cx + r + 3} y={cy + r * cos60 + 8} textAnchor="start"
          fontSize="6" fill="hsl(var(--muted-foreground))" opacity="0.7">Right</text>
      </svg>
    </div>
  );
}


/**
 * PhaseCorrelationMeter — vertical bar showing phase correlation from -1 to +1.
 * Uses direct DOM manipulation for zero-overhead updates.
 */
export function PhaseCorrelationMeter() {
  const fillRef = useRef<HTMLDivElement>(null);
  const indicatorRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const unsub = useMeterStore.subscribe((state) => {
      const correlation = state.phaseCorrelation;
      const pct = ((correlation + 1) / 2) * 100;

      // Color based on correlation value
      let color: string;
      if (correlation > 0.5) color = 'rgb(34 197 94)';      // green
      else if (correlation > 0) color = 'rgb(234 179 8)';    // yellow
      else color = 'rgb(239 68 68)';                          // red

      if (fillRef.current) {
        fillRef.current.style.width = `${pct}%`;
        fillRef.current.style.backgroundColor = color;
      }
      if (indicatorRef.current) {
        indicatorRef.current.style.left = `${pct}%`;
        indicatorRef.current.style.backgroundColor = color;
      }
    });
    return unsub;
  }, []);

  return (
    <div className={wrapper}>
      <span className={label}>PHASE</span>
      <div className={phaseTrack}>
        {/* Scale marks */}
        <div className={scaleMarkVert} style={{ left: '100%' }} />
        <div className={scaleMarkVert} style={{ left: '50%' }} />
        <div className={scaleMarkVert} style={{ left: '0%' }} />
        {/* Fill bar — updated via ref */}
        <div ref={fillRef} className={phaseFill} />
        {/* Indicator line — updated via ref */}
        <div ref={indicatorRef} className={phaseIndicator} />
      </div>
      <div className={phaseLabels}>
        <span>−1</span>
        <span>0</span>
        <span>+1</span>
      </div>
    </div>
  );
}

/**
 * SpectrumAnalyzer — 64-band EQ visualizer using DOM manipulation.
 */
export function SpectrumAnalyzer() {
  const barsRef = useRef<(SVGRectElement | null)[]>([]);

  useEffect(() => {
    const unsub = useMeterStore.subscribe((state) => {
      const spectrum = state.spectrum;
      if (!spectrum || spectrum.length === 0) return;

      for (let i = 0; i < spectrum.length; i++) {
        const bar = barsRef.current[i];
        if (bar) {
          // sqrt-scale so quiet signals are visible, cap at full height
          const val = Math.sqrt(Math.max(0, spectrum[i]));
          const h = Math.max(1, val * 40);
          bar.setAttribute('height', String(h));
          bar.setAttribute('y', String(40 - h));
          // Solid green, red only at full clip (val >= 0.95)
          const color = val >= 0.95 ? 'rgb(239,68,68)' : 'rgb(52,211,153)';
          bar.setAttribute('fill', color);
        }
      }
    });
    return unsub;
  }, []);

  return (
    <div className={wrapper}>
      <span className={label}>SPECTRUM</span>
      <div className="w-[128px] h-[40px] bg-[hsl(var(--card))] border border-[hsl(var(--border))] rounded flex items-end overflow-hidden">
        <svg width="128" height="40" viewBox="0 0 128 40" className="w-full h-full">
          {Array.from({ length: 64 }).map((_, i) => (
            <rect
              key={i}
              ref={(el) => { barsRef.current[i] = el; }}
              x={i * 2}
              y="39"
              width="1.5"
              height="1"
              fill="hsl(var(--muted-foreground))"
              opacity="0.8"
            />
          ))}
        </svg>
      </div>
    </div>
  );
}

const wrapper = `flex flex-col items-center gap-1`;
const label = `text-[9px] uppercase tracking-wider text-[hsl(var(--muted-foreground))] font-medium`;

const phaseTrack = `
  relative w-16 h-2 bg-[hsl(var(--card))] rounded-full overflow-hidden
  border border-[hsl(var(--border))]`;
const scaleMarkVert = `absolute h-full w-px bg-[hsl(var(--muted-foreground))]/20`;
const phaseFill = `absolute left-0 h-full rounded-full opacity-60 transition-all duration-75`;
const phaseIndicator = `absolute h-full w-0.5 rounded-full transition-all duration-75`;
const phaseLabels = `
  flex justify-between w-16
  text-[7px] text-[hsl(var(--muted-foreground))] leading-none`;
