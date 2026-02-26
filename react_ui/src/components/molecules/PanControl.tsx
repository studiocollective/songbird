import * as SliderPrimitive from '@radix-ui/react-slider';
import { useMixerStore } from '@/data/store';
import { nativeFunction } from '@/data/bridge';
import { beginSliderDrag, endSliderDrag } from '@/data/sliderDrag';

interface PanControlProps {
  trackId: number;
  value: number;
  color: string;
}

const DEFAULT_PAN = 0;
const setMixerParamRT = nativeFunction('setMixerParamRT');

export function PanControl({ trackId, value, color }: PanControlProps) {
  return (
    <div className={wrapper}>
      <span className={label}>L</span>
      <SliderPrimitive.Root
        min={-64}
        max={63}
        step={0.01}
        value={[value]}
        onPointerDown={() => beginSliderDrag()}
        onValueChange={([v]) => {
          useMixerStore.getState().setPan(trackId, v);
          setMixerParamRT(trackId, 'pan', v);
        }}
        onValueCommit={([v]) => {
          endSliderDrag();
          useMixerStore.getState().setPan(trackId, v);
        }}
        onDoubleClick={() => useMixerStore.getState().setPan(trackId, DEFAULT_PAN)}
        className={root}
      >
        <SliderPrimitive.Track className={track}>
          <SliderPrimitive.Range className={range} />
        </SliderPrimitive.Track>
        <SliderPrimitive.Thumb className={thumb} style={{ background: color, borderColor: color }} />
      </SliderPrimitive.Root>
      <span className={label}>R</span>
    </div>
  );
}

const wrapper = `flex items-center gap-1`;
const label = `text-[8px] text-[hsl(var(--muted-foreground))]`;
const root = `relative flex w-12 touch-none select-none items-center`;
const track = `relative h-1 w-full grow overflow-hidden rounded-full bg-[hsl(var(--muted))]`;
const range = `absolute h-full rounded-full`;
const thumb = `
  block h-4 w-4 rounded-full shadow-sm cursor-pointer
  focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring
  disabled:pointer-events-none disabled:opacity-50`;
