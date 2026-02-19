import { useRef, useEffect } from 'react';
import { useMeterStore } from '@/data/meters';

/**
 * StereoWidthMeter — TC-Electronic inspired equilateral triangle visualization.
 * Uses direct DOM manipulation for zero-overhead updates.
 */
export function StereoWidthMeter() {
  const lineRef = useRef<SVGLineElement>(null);
  const dotRef = useRef<SVGCircleElement>(null);

  // Triangle geometry (SVG coords)
  const svgW = 60;
  const svgH = 70;
  const cx = svgW / 2;
  const top = 8;
  const bottom = svgH - 8;
  const halfBase = 22;

  useEffect(() => {
    const unsub = useMeterStore.subscribe((state) => {
      const width = state.stereoWidth;
      const dotY = top + (bottom - top) * width;
      const progress = (dotY - top) / (bottom - top);
      const spreadAtY = halfBase * progress;

      if (lineRef.current) {
        lineRef.current.setAttribute('x1', String(cx - spreadAtY));
        lineRef.current.setAttribute('y1', String(dotY));
        lineRef.current.setAttribute('x2', String(cx + spreadAtY));
        lineRef.current.setAttribute('y2', String(dotY));
      }
      if (dotRef.current) {
        dotRef.current.setAttribute('cy', String(dotY));
      }
    });
    return unsub;
  }, []);

  return (
    <div className={wrapper}>
      <span className={label}>WIDTH</span>
      <svg width={svgW} height={svgH} viewBox={`0 0 ${svgW} ${svgH}`} className="shrink-0">
        {/* Triangle outline */}
        <polygon
          points={`${cx},${top} ${cx - halfBase},${bottom} ${cx + halfBase},${bottom}`}
          fill="none"
          stroke="hsl(var(--muted-foreground))"
          strokeWidth="1"
          opacity="0.3"
        />
        {/* Center line */}
        <line
          x1={cx} y1={top} x2={cx} y2={bottom}
          stroke="hsl(var(--muted-foreground))"
          strokeWidth="0.5"
          opacity="0.2"
        />
        {/* Horizontal grid lines at 25%, 50%, 75% */}
        {[0.25, 0.5, 0.75].map((frac) => {
          const gy = top + (bottom - top) * frac;
          const gs = halfBase * frac;
          return (
            <line
              key={frac}
              x1={cx - gs} y1={gy} x2={cx + gs} y2={gy}
              stroke="hsl(var(--muted-foreground))"
              strokeWidth="0.5"
              opacity="0.15"
            />
          );
        })}
        {/* Active width indicator line — updated via ref */}
        <line
          ref={lineRef}
          x1={cx} y1={top} x2={cx} y2={top}
          stroke="hsl(var(--progress))"
          strokeWidth="2"
          strokeLinecap="round"
          opacity="0.8"
        />
        {/* Center dot — updated via ref */}
        <circle
          ref={dotRef}
          cx={cx} cy={top} r="3"
          fill="hsl(var(--progress))"
          opacity="0.9"
        />
      </svg>
      <div className={scaleLabels}>
        <span>M</span>
        <span>S</span>
      </div>
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
        fillRef.current.style.height = `${pct}%`;
        fillRef.current.style.backgroundColor = color;
      }
      if (indicatorRef.current) {
        indicatorRef.current.style.bottom = `${pct}%`;
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
        <div className={scaleMark} style={{ bottom: '100%' }} />
        <div className={scaleMark} style={{ bottom: '50%' }} />
        <div className={scaleMark} style={{ bottom: '0%' }} />
        {/* Fill bar — updated via ref */}
        <div ref={fillRef} className={phaseFill} />
        {/* Indicator line — updated via ref */}
        <div ref={indicatorRef} className={phaseIndicator} />
      </div>
      <div className={phaseLabels}>
        <span>+1</span>
        <span>0</span>
        <span>−1</span>
      </div>
    </div>
  );
}

const wrapper = `flex flex-col items-center gap-1`;
const label = `text-[9px] uppercase tracking-wider text-[hsl(var(--muted-foreground))] font-medium`;
const scaleLabels = `
  flex justify-between w-full px-1
  text-[8px] text-[hsl(var(--muted-foreground))]`;
const phaseTrack = `
  relative w-2 h-16 bg-[hsl(var(--card))] rounded-full overflow-hidden
  border border-[hsl(var(--border))]`;
const scaleMark = `absolute w-full h-px bg-[hsl(var(--muted-foreground))]/20`;
const phaseFill = `absolute bottom-0 w-full rounded-full opacity-60`;
const phaseIndicator = `absolute w-full h-0.5 rounded-full`;
const phaseLabels = `
  flex flex-col justify-between h-16
  text-[7px] text-[hsl(var(--muted-foreground))] leading-none`;
