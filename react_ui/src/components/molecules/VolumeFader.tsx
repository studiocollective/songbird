import * as SliderPrimitive from '@radix-ui/react-slider';
import { cn } from '@/lib/utils';
import { useMixerStore } from '@/data/store';

interface VolumeFaderProps {
  trackId: number;
  value: number;
  color: string;
  height?: string;
}

const DEFAULT_VOLUME = 80;

export function VolumeFader({ trackId, value, color, height = 'h-24' }: VolumeFaderProps) {
  return (
    <SliderPrimitive.Root
      orientation="vertical"
      min={0}
      max={127}
      step={0.01}
      value={[value]}
      onValueChange={([v]) => useMixerStore.getState().setVolume(trackId, v)}
      onDoubleClick={() => useMixerStore.getState().setVolume(trackId, DEFAULT_VOLUME)}
      className={cn(root, height)}
    >
      <SliderPrimitive.Track className={track}>
        <SliderPrimitive.Range className={range} style={{ background: color }} />
      </SliderPrimitive.Track>
      <SliderPrimitive.Thumb className={thumb} style={{ background: color, borderColor: color }} />
    </SliderPrimitive.Root>
  );
}

const root = `relative flex items-center justify-center touch-none select-none w-[14px]`;
const track = `relative w-1.5 grow overflow-hidden rounded-full bg-[hsl(var(--muted))]`;
const range = `absolute w-full rounded-full`;
const thumb = `
  block h-4 w-4 rounded-full shadow-sm
  focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring
  disabled:pointer-events-none disabled:opacity-50`;
