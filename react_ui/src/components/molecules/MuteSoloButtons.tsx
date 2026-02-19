import { cn } from '@/lib/utils';
import { useMixerStore } from '@/data/store';

interface MuteSoloButtonsProps {
  trackId: number;
  muted: boolean;
  solo: boolean;
  size?: 'sm' | 'md';
}

export function MuteSoloButtons({ trackId, muted, solo, size = 'sm' }: MuteSoloButtonsProps) {
  const btnClass = size === 'sm' ? btnSm : btnMd;

  return (
    <div className={cn(wrapper, size === 'sm' ? gapSm : gapMd)}>
      <button
        onClick={() => useMixerStore.getState().toggleMute(trackId)}
        className={cn(btnClass, muted ? muteActive : inactive)}
      >
        M
      </button>
      <button
        onClick={() => useMixerStore.getState().toggleSolo(trackId)}
        className={cn(btnClass, solo ? soloActive : inactive)}
      >
        S
      </button>
    </div>
  );
}

const wrapper = `flex`;
const gapSm = `gap-0.5`;
const gapMd = `gap-1`;
const btnBase = `rounded font-bold flex items-center justify-center transition-colors`;
const btnSm = `${btnBase} w-5 h-5 text-[9px]`;
const btnMd = `${btnBase} w-6 h-4 text-[8px]`;
const muteActive = `bg-[hsl(var(--plugin-bypassed))] text-[hsl(var(--primary-foreground))]`;
const soloActive = `bg-yellow-500 text-black`;
const inactive = `bg-[hsl(var(--card))] text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]`;
