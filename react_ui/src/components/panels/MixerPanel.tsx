import { useState, useCallback } from 'react';
import { cn } from '@/lib/utils';
import { useMixerStore } from '@/data/store';
import { MasterChannel, MixerChannel } from '@/components/organisms';
import { SendControl } from '@/components/molecules';
import { RecordStrip } from '@/components/molecules/RecordStrip';
import { StereoWidthMeter, PhaseCorrelationMeter, SpectrumAnalyzer } from '@/components/molecules/StereoMeters';
import { TrackCpuPanel } from '@/components/molecules/CpuMeter';

export function MixerPanel() {
  const { tracks, mixerOpen, returnsOpen, toggleReturns, recordStripOpen, toggleRecordStrip } = useMixerStore();
  const [cpuPanelOpen, setCpuPanelOpen] = useState(false);

  const regularTracks = tracks.filter((t) => !t.isReturn && !t.isMaster);
  const returnTracks = tracks.filter((t) => t.isReturn);
  const masterTrack = tracks.find((t) => t.isMaster);

  // Render send controls matching track positions
  const renderSendRow = useCallback(() => (
    <>
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
      {/* Return track send slots for alignment */}
      {returnsOpen && returnTracks.length > 0 && (
        <>
          <div className="w-6 shrink-0" />
          {returnTracks.map((track) => (
            <div key={track.id} className="w-28 shrink-0" />
          ))}
        </>
      )}
    </>
  ), [regularTracks, returnTracks, returnsOpen]);

  // Sync overlay scrolls with main scroll area
  const handleScroll = (e: React.UIEvent<HTMLDivElement>) => {
    const scrollLeft = e.currentTarget.scrollLeft;
    const parent = e.currentTarget.parentElement;
    if (parent) {
      parent.querySelectorAll<HTMLDivElement>('[data-scroll-sync]').forEach(el => {
        el.scrollLeft = scrollLeft;
      });
    }
  };

  return (
    <div className={cn(panel, mixerOpen ? 'h-72 overflow-visible' : 'h-0 overflow-hidden')}>
      <div className={panelInner}>
        {/* Master channel — fixed left */}
        <MasterChannel
          returnsOpen={returnsOpen}
          onToggleReturns={toggleReturns}
          recordStripOpen={recordStripOpen}
          onToggleRecordStrip={toggleRecordStrip}
        />

        {/* Scrollable tracks area (regular + returns) */}
        <div className="flex-1 relative min-w-0 flex flex-col overflow-visible">
          {/* Sends — above record strip */}
          {returnsOpen && (
            <div className="absolute bottom-full left-0 right-0 z-50 pointer-events-auto"
              style={{ marginBottom: recordStripOpen ? '6rem' : '0.5rem' }}
            >
              <div data-scroll-sync className="flex w-full overflow-hidden hide-scrollbar">
                {renderSendRow()}
              </div>
            </div>
          )}

          {/* Record strip — directly above channels */}
          {recordStripOpen && (
            <div className="absolute bottom-full left-0 right-0 mb-2 z-40 pointer-events-auto">
              <div data-scroll-sync className="flex w-full overflow-hidden hide-scrollbar">
                <RecordStrip tracks={regularTracks} />
                {/* Invisible spacers for return track columns */}
                {returnsOpen && returnTracks.length > 0 && (
                  <>
                    <div className="w-6 shrink-0" />
                    {returnTracks.map((track) => (
                      <div key={track.id} className="w-28 shrink-0" />
                    ))}
                  </>
                )}
              </div>
            </div>
          )}

          {/* Main scrollable channel strip */}
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
                audioSource={track.audioSource}
                audioMode={track.audioMode}
                recordArmed={track.recordArmed}
              />
            ))}

            {/* Return tracks — inline, separated by whitespace + left border on first */}
            {returnsOpen && returnTracks.length > 0 && (
              <>
                <div className="w-6 shrink-0" />
                {returnTracks.map((track, i) => (
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
                    isFirstReturn={i === 0}
                  />
                ))}
              </>
            )}
          </div>
        </div>

        {/* Master track — fixed right, always visible when returns open */}
        {returnsOpen && masterTrack && (
          <div className="flex border-l border-[hsl(var(--border))]/50 z-20">
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

        {/* Stereo analysis meters — fixed far right */}
        <div className={stereoSection}>
          <div className="flex flex-col gap-4 items-center">
            <SpectrumAnalyzer />
            <StereoWidthMeter />
            <PhaseCorrelationMeter />
            <button
              onClick={() => setCpuPanelOpen((v) => !v)}
              className={`text-[9px] uppercase tracking-wider font-semibold px-2 py-1 rounded transition-colors ${
                cpuPanelOpen
                  ? 'bg-[hsl(var(--selection))] text-[hsl(var(--primary-foreground))]'
                  : 'text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] hover:bg-[hsl(var(--card))]'
              }`}
              title="Toggle per-track CPU display"
            >
              CPU
            </button>
            {cpuPanelOpen && <TrackCpuPanel open={cpuPanelOpen} />}
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
const channelsScroll = `flex-1 flex overflow-x-auto overflow-y-visible hide-scrollbar`;
const stereoSection = `
  flex items-center gap-4 px-4
  border-l border-[hsl(var(--border))]`;
