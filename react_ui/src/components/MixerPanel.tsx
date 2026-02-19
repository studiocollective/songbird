import { cn } from '@/lib/utils';
import { useMixerStore } from '@/data/store';
import { MasterChannel, MixerChannel } from '@/components/organisms';
import { StereoWidthMeter, PhaseCorrelationMeter } from '@/components/molecules/StereoMeters';

export function MixerPanel() {
  const { tracks, mixerOpen } = useMixerStore();

  return (
    <div className={cn(panel, mixerOpen ? 'h-72' : 'h-0')}>
      <div className={panelInner}>
        <MasterChannel />

        <div className={channelsScroll}>
          {tracks.map((track, index) => (
            <MixerChannel
              key={track.id}
              trackId={track.id}
              trackIndex={index}
              name={track.name}
              trackType={track.type as 'midi' | 'audio'}
              color={track.color}
              muted={track.muted}
              solo={track.solo}
              volume={track.volume}
              pan={track.pan}
              instrument={track.instrument}
              channelStrip={track.channelStrip}
            />
          ))}
        </div>

        {/* Stereo analysis meters — far right */}
        <div className={stereoSection}>
          <StereoWidthMeter />
          <PhaseCorrelationMeter />
        </div>
      </div>
    </div>
  );
}

// --- Panel layout ---
const panel = `
  bg-[hsl(var(--mixer))] border-t border-[hsl(var(--border))]
  transition-all duration-300 ease-in-out overflow-hidden`;
const panelInner = `h-72 flex`;
const channelsScroll = `flex-1 flex overflow-x-auto`;
const stereoSection = `
  flex items-center gap-4 px-4
  border-l border-[hsl(var(--border))]`;
