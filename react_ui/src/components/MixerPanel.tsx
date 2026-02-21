import { useRef } from 'react';
import { cn } from '@/lib/utils';
import { useMixerStore } from '@/data/store';
import { MasterChannel, MixerChannel } from '@/components/organisms';
import { SendControl } from '@/components/molecules';
import { StereoWidthMeter, PhaseCorrelationMeter, SpectrumAnalyzer } from '@/components/molecules/StereoMeters';

export function MixerPanel() {
  const { tracks, mixerOpen, returnsOpen, toggleReturns } = useMixerStore();
  const sendScrollRef = useRef<HTMLDivElement>(null);

  const handleScroll = (e: React.UIEvent<HTMLDivElement>) => {
    if (sendScrollRef.current) {
      sendScrollRef.current.scrollLeft = e.currentTarget.scrollLeft;
    }
  };

  const regularTracks = tracks.filter((t) => !t.isReturn && !t.isMaster);
  const returnTracks = tracks.filter((t) => t.isReturn);
  const masterTrack = tracks.find((t) => t.isMaster);

  return (
    <div className={cn(panel, mixerOpen ? 'h-72 overflow-visible' : 'h-0 overflow-hidden')}>
      <div className={panelInner}>
        <MasterChannel />

        <div className="flex flex-col justify-end pb-4 px-2 border-r border-[hsl(var(--border))]/50">
          <button
            onClick={toggleReturns}
            className={cn(
              "w-6 h-6 rounded flex items-center justify-center text-[10px] font-bold transition-colors",
              returnsOpen
                ? "bg-[hsl(var(--primary))] text-primary-foreground"
                : "bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--muted))]/80"
            )}
            title="Toggle Return Tracks & Sends"
          >
            R
          </button>
        </div>

        <div className="flex-1 relative min-w-0 flex flex-col">
          {returnsOpen && (
            <div className="absolute bottom-full left-0 right-0 mb-2 z-50 pointer-events-auto block">
              <div ref={sendScrollRef} className="flex w-full overflow-hidden hide-scrollbar">
                {regularTracks.map((track) => (
                  <div key={track.id} className="w-28 shrink-0 flex items-end justify-center">
                    <div className="bg-[hsl(var(--mixer))] border border-[hsl(var(--border))] rounded-md shadow-xl p-2 w-[85%]">
                      <div className="text-[8px] uppercase tracking-wider text-[hsl(var(--muted-foreground))] mb-2 font-semibold text-center w-full">Sends</div>
                      <div className="grid grid-cols-2 gap-x-2 gap-y-2 place-items-center">
                        {track.sends?.map((s, idx) => (
                          <SendControl key={idx} trackId={track.id} busIndex={idx} value={s} color={track.color} />
                        ))}
                      </div>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}

          <div className={channelsScroll} onScroll={handleScroll}>
            {regularTracks.map((track, index) => (
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
                fx={track.fx}
                channelStrip={track.channelStrip}
                isReturn={track.isReturn}
                sidechainTrackId={track.sidechainTrackId}
                sidechainSensitivity={track.sidechainSensitivity}
                trackList={tracks}
              />
            ))}
          </div>
        </div>

        {/* Anchored Return Tracks - Fixed to the right of Regular Tracks */}
        {returnsOpen && returnTracks.length > 0 && (
          <div className="flex bg-[hsl(var(--accent))]/10 border-l border-[hsl(var(--border))]/50 z-10 shadow-[-4px_0_12px_rgba(0,0,0,0.05)]">
            {returnTracks.map((track) => (
              <MixerChannel
                key={track.id}
                trackId={track.id}
                trackIndex={tracks.indexOf(track)}
                name={track.name}
                trackType={track.type as 'midi' | 'audio'}
                color={track.color}
                muted={track.muted}
                solo={track.solo}
                volume={track.volume}
                pan={track.pan}
                instrument={track.instrument}
                fx={track.fx}
                channelStrip={track.channelStrip}
                isReturn={track.isReturn}
                sidechainTrackId={track.sidechainTrackId}
                sidechainSensitivity={track.sidechainSensitivity}
                trackList={tracks}
              />
            ))}
          </div>
        )}

        {/* Anchored Master Track - Fixed to the far right, shown with returns */}
        {returnsOpen && masterTrack && (
          <div className="flex bg-[hsl(var(--accent))]/5 border-l border-[hsl(var(--border))]/50 z-20 shadow-[-4px_0_12px_rgba(0,0,0,0.08)]">
            <MixerChannel
              key={masterTrack.id}
              trackId={masterTrack.id}
              trackIndex={tracks.indexOf(masterTrack)}
              name={masterTrack.name}
              trackType={masterTrack.type as 'midi' | 'audio'}
              color={masterTrack.color}
              muted={masterTrack.muted}
              solo={masterTrack.solo}
              volume={masterTrack.volume}
              pan={masterTrack.pan}
              instrument={masterTrack.instrument}
              fx={masterTrack.fx}
              channelStrip={masterTrack.channelStrip}
              isReturn={masterTrack.isReturn}
              isMaster={masterTrack.isMaster}
            />
          </div>
        )}

        {/* Stereo analysis meters — far right */}
        <div className={stereoSection}>
          <div className="flex flex-col gap-4 items-center">
            <SpectrumAnalyzer />
            <StereoWidthMeter />
            <PhaseCorrelationMeter />
          </div>
        </div>
      </div>
    </div>
  );
}

// --- Panel layout ---
const panel = `
  bg-[hsl(var(--mixer))] border-t border-[hsl(var(--border))]
  transition-all duration-300 ease-in-out`;
const panelInner = `h-72 flex`;
const channelsScroll = `flex-1 flex overflow-x-auto hide-scrollbar`;
const stereoSection = `
  flex items-center gap-4 px-4
  border-l border-[hsl(var(--border))]`;
