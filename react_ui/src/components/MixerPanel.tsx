import { useAppStore } from '@/data/store';

export function MixerPanel() {
  const { tracks, mixerOpen } = useAppStore();

  return (
    <div
      className={`bg-zinc-900 border-t border-zinc-700 transition-all duration-300 ease-in-out overflow-hidden ${
        mixerOpen ? 'h-56' : 'h-0'
      }`}
    >
      <div className="h-56 flex">
        {/* Master channel */}
        <div className="w-20 shrink-0 border-r border-zinc-800 flex flex-col items-center py-2 bg-zinc-900">
          <span className="text-[9px] text-zinc-500 uppercase tracking-widest mb-2">Master</span>
          <div className="flex-1 flex items-center justify-center">
            <div className="relative w-2 h-28 bg-zinc-800 rounded-full">
              <div
                className="absolute bottom-0 w-full rounded-full bg-gradient-to-t from-emerald-500 to-emerald-400"
                style={{ height: '75%' }}
              />
              {/* Meter segments */}
              {Array.from({ length: 12 }, (_, i) => (
                <div
                  key={i}
                  className="absolute w-full h-px bg-zinc-900"
                  style={{ bottom: `${(i / 12) * 100}%` }}
                />
              ))}
            </div>
          </div>
          <div className="text-[10px] font-mono text-zinc-400 mt-1">0.0</div>
        </div>

        {/* Track channels */}
        <div className="flex-1 flex overflow-x-auto">
          {tracks.map((track) => (
            <div
              key={track.id}
              className="w-24 shrink-0 border-r border-zinc-800/50 flex flex-col items-center py-2 hover:bg-zinc-850 transition-colors"
            >
              {/* Track name */}
              <div className="flex items-center gap-1 mb-1.5">
                <div
                  className="w-2 h-2 rounded-full"
                  style={{ backgroundColor: track.color }}
                />
                <span className="text-[10px] text-zinc-400 font-medium truncate max-w-[60px]">
                  {track.name}
                </span>
              </div>

              {/* Mute / Solo */}
              <div className="flex gap-1 mb-2">
                <button
                  onClick={() => useAppStore.getState().toggleMute(track.id)}
                  className={`w-6 h-4 rounded text-[8px] font-bold flex items-center justify-center transition-colors ${
                    track.muted ? 'bg-amber-600 text-white' : 'bg-zinc-800 text-zinc-600 hover:text-zinc-400'
                  }`}
                >
                  M
                </button>
                <button
                  onClick={() => useAppStore.getState().toggleSolo(track.id)}
                  className={`w-6 h-4 rounded text-[8px] font-bold flex items-center justify-center transition-colors ${
                    track.solo ? 'bg-yellow-500 text-black' : 'bg-zinc-800 text-zinc-600 hover:text-zinc-400'
                  }`}
                >
                  S
                </button>
              </div>

              {/* Fader + Meter */}
              <div className="flex-1 flex items-center gap-1.5">
                {/* Level meter */}
                <div className="relative w-1.5 h-24 bg-zinc-800 rounded-full overflow-hidden">
                  <div
                    className="absolute bottom-0 w-full rounded-full transition-all"
                    style={{
                      height: `${track.muted ? 0 : track.volume}%`,
                      background: `linear-gradient(to top, ${track.color}88, ${track.color})`,
                    }}
                  />
                </div>

                {/* Volume fader */}
                <input
                  type="range"
                  min="0"
                  max="127"
                  value={track.volume}
                  onChange={(e) =>
                    useAppStore.getState().setVolume(track.id, Number(e.target.value))
                  }
                  className="h-24 appearance-none cursor-pointer"
                  style={{
                    writingMode: 'vertical-lr',
                    direction: 'rtl',
                    width: '14px',
                    accentColor: track.color,
                  }}
                />
              </div>

              {/* Pan knob label */}
              <div className="flex items-center gap-1 mt-1">
                <span className="text-[8px] text-zinc-600">L</span>
                <input
                  type="range"
                  min="-64"
                  max="63"
                  value={track.pan}
                  onChange={(e) =>
                    useAppStore.getState().setPan(track.id, Number(e.target.value))
                  }
                  className="w-12 h-1 appearance-none cursor-pointer rounded-full bg-zinc-700"
                  style={{ accentColor: track.color }}
                />
                <span className="text-[8px] text-zinc-600">R</span>
              </div>

              {/* Volume readout */}
              <div className="text-[9px] font-mono text-zinc-500 mt-0.5">
                {track.volume}
              </div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
