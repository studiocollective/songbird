import { useState } from 'react';
import type { AutomationCurve } from '@/data/slices/mixer';

export function AutomationOverlay({
  automation,
  totalBars,
  color,
}: {
  automation: AutomationCurve[];
  totalBars: number;
  color: string;
}) {
  const [selectedMacro, setSelectedMacro] = useState<string>(automation[0]?.macro || '');

  if (automation.length === 0) return null;

  const curve = automation.find((c) => c.macro === selectedMacro) || automation[0];

  // Build SVG path
  const totalBeats = totalBars * 4;
  let d = '';

  for (let i = 0; i < curve.points.length; i++) {
    const pt = curve.points[i];
    const x = (pt.time / totalBeats) * 100;
    const y = 100 - pt.value * 100;

    if (i === 0) {
      d += `M ${x} ${y} `;
    } else {
      const prevPt = curve.points[i - 1];
      const prevX = (prevPt.time / totalBeats) * 100;
      const prevY = 100 - prevPt.value * 100;

      // SHAPES: 0=Step, 1=Linear, 2=Exponential, 3=Logarithmic, 4=Smooth
      if (prevPt.shape === 0) {
        // Step: Horizontal line to new X, then vertical to new Y
        d += `H ${x} V ${y} `;
      } else if (prevPt.shape === 4) {
        // Smooth: simple S-curve bezier
        const midX = prevX + (x - prevX) / 2;
        d += `C ${midX} ${prevY}, ${midX} ${y}, ${x} ${y} `;
      } else if (prevPt.shape === 2) {
        // Exponential (starts slow, ends fast)
        d += `Q ${prevX + (x - prevX) * 0.8} ${prevY}, ${x} ${y} `;
      } else if (prevPt.shape === 3) {
        // Logarithmic (starts fast, ends slow)
        d += `Q ${prevX + (x - prevX) * 0.2} ${y}, ${x} ${y} `;
      } else {
        // Linear
        d += `L ${x} ${y} `;
      }
    }
  }

  return (
    <div className="absolute inset-x-0 bottom-0 top-0 z-40 pointer-events-auto">
      {/* Dropdown selector */}
      <div className="absolute top-1 right-1 bg-[hsl(var(--background))]/90 border border-[hsl(var(--border))] rounded px-1.5 py-0.5 z-50">
        <select
          value={selectedMacro}
          onChange={(e) => setSelectedMacro(e.target.value)}
          className="bg-transparent text-[10px] text-[hsl(var(--foreground))] outline-none min-w-[80px]"
        >
          {automation.map((c) => (
            <option key={c.macro} value={c.macro}>
              {c.macro}
            </option>
          ))}
        </select>
      </div>

      {/* SVG Canvas for the curve */}
      <svg
        className="w-full h-full overflow-visible drop-shadow-md"
        viewBox="0 0 100 100"
        preserveAspectRatio="none"
      >
        <path
          d={d}
          fill="none"
          stroke={color}
          strokeWidth="2"
          vectorEffect="non-scaling-stroke"
        />
        {/* Draw points */}
        {curve.points.map((pt, i) => (
           <circle
             key={i}
             cx={(pt.time / totalBeats) * 100}
             cy={100 - pt.value * 100}
             r="3.5"
             fill={color}
             vectorEffect="non-scaling-stroke"
             className="cursor-ns-resize hover:r-5 transition-all"
           />
        ))}
      </svg>
    </div>
  );
}
