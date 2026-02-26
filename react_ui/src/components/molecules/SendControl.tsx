import { useRef, useCallback, useEffect } from 'react';
import type { PointerEvent as ReactPointerEvent } from 'react';
import { useMixerStore } from '@/data/store';
import { nativeFunction } from '@/data/bridge';
import { beginSliderDrag, endSliderDrag } from '@/data/sliderDrag';

interface SendControlProps {
  trackId: number;
  busIndex: number;
  value: number; // 0.0 to 1.0
  color: string;
}

const DEFAULT_SEND = 0;
const DRAG_SENSITIVITY = 150;
const setMixerParamRT = nativeFunction('setMixerParamRT');

export function SendControl({ trackId, busIndex, value, color }: SendControlProps) {
  const currentY = useRef<number | undefined>(undefined);
  const currentValue = useRef<number>(value);

  // Keep ref synchronized with prop
  useEffect(() => {
    currentValue.current = value;
  }, [value]);

  const handlePointerMove = useCallback((e: globalThis.PointerEvent) => {
    if (currentY.current === undefined) return;
    const deltaY = currentY.current - e.clientY; // positive = dragged up
    const valueChange = deltaY / DRAG_SENSITIVITY;
    
    let newValue = currentValue.current + valueChange;
    newValue = Math.max(0, Math.min(1, newValue));
    
    currentValue.current = newValue;
    useMixerStore.getState().setSendLevel(trackId, busIndex, newValue);
    setMixerParamRT(trackId, 'send' + busIndex, newValue);
    
    currentY.current = e.clientY;
  }, [trackId, busIndex]);

  const handlePointerUp = useCallback(function onPointerUp() {
    currentY.current = undefined;
    window.removeEventListener('pointermove', handlePointerMove);
    window.removeEventListener('pointerup', onPointerUp);
    // Re-enable persist and flush final state
    endSliderDrag();
    useMixerStore.getState().setSendLevel(trackId, busIndex, currentValue.current);
  }, [handlePointerMove, trackId, busIndex]);

  const handlePointerDown = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return; // Only left-click
    beginSliderDrag();
    currentY.current = e.clientY;
    window.addEventListener('pointermove', handlePointerMove);
    window.addEventListener('pointerup', handlePointerUp);
  };

  const onDoubleClick = () => {
    useMixerStore.getState().setSendLevel(trackId, busIndex, DEFAULT_SEND);
  };

  // calculate angle from -135 to 135 degrees
  const angle = (value * 270) - 135;

  return (
    <div className="flex flex-col items-center gap-0.5 group">
      <div 
        className="relative w-5 h-5 rounded-full bg-[hsl(var(--mixer))] border border-[hsl(var(--border))] cursor-ns-resize shadow-sm active:cursor-grabbing hover:bg-[hsl(var(--accent))] transition-colors"
        onPointerDown={handlePointerDown}
        onDoubleClick={onDoubleClick}
        title={`Send ${busIndex + 1}: ${(value * 100).toFixed(0)}%`}
      >
        <div 
          className="absolute w-[1.5px] h-[8px] rounded-full origin-bottom"
          style={{
            left: 'calc(50% - 0.75px)',
            bottom: '50%',
            transform: `rotate(${angle}deg)`,
            backgroundColor: value > 0 ? color : 'hsl(var(--muted-foreground))'
          }}
        />
      </div>
      <span className="text-[7px] font-mono text-[hsl(var(--muted-foreground))] group-hover:text-[hsl(var(--foreground))] transition-colors">
        {(['Hall', 'Plate', 'Del', 'Sat'])[busIndex] ?? `S${busIndex + 1}`}
      </span>
    </div>
  );
}
