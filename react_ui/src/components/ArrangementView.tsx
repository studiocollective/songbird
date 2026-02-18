import { useAppStore } from '@/data/store';

export function ArrangementView() {
  const { tracks } = useAppStore();
  const totalBars = 32;
  const sections = [
    { name: 'Verse', start: 0, length: 8, color: 'bg-indigo-900/40' },
    { name: 'Chorus', start: 8, length: 8, color: 'bg-rose-900/40' },
    { name: 'Verse', start: 16, length: 8, color: 'bg-indigo-900/40' },
    { name: 'Chorus', start: 24, length: 8, color: 'bg-rose-900/40' },
  ];

  return (
    <div className="flex-1 flex flex-col overflow-hidden bg-zinc-950">
      {/* Timeline ruler */}
      <div className="flex shrink-0">
        <div className="w-44 shrink-0 bg-zinc-900 border-b border-r border-zinc-800" />
        <div className="flex-1 h-10 bg-zinc-900 border-b border-zinc-800 flex items-end relative overflow-hidden">
          {/* Section labels */}
          {sections.map((sec, i) => (
            <div
              key={i}
              className="absolute top-0 h-5 flex items-center justify-center text-[10px] font-medium text-zinc-400 border-x border-zinc-700/50"
              style={{
                left: `${(sec.start / totalBars) * 100}%`,
                width: `${(sec.length / totalBars) * 100}%`,
              }}
            >
              {sec.name}
            </div>
          ))}
          {/* Bar numbers */}
          {Array.from({ length: totalBars }, (_, i) => (
            <div
              key={i}
              className="absolute bottom-0 h-5 flex items-center justify-center text-[9px] font-mono text-zinc-600 border-r border-zinc-800/50"
              style={{
                left: `${(i / totalBars) * 100}%`,
                width: `${(1 / totalBars) * 100}%`,
              }}
            >
              {i + 1}
            </div>
          ))}
        </div>
      </div>

      {/* Track lanes */}
      <div className="flex-1 overflow-y-auto">
        {tracks.map((track) => (
          <div key={track.id} className="flex h-16 border-b border-zinc-800/50 group">
            {/* Track header */}
            <div className="w-44 shrink-0 bg-zinc-900/80 border-r border-zinc-800 flex items-center px-3 gap-2">
              <div
                className="w-2.5 h-2.5 rounded-full shrink-0"
                style={{ backgroundColor: track.color }}
              />
              <span className="text-xs text-zinc-300 font-medium truncate flex-1">
                {track.name}
              </span>
              <div className="flex gap-0.5 opacity-0 group-hover:opacity-100 transition-opacity">
                <button
                  onClick={() => useAppStore.getState().toggleMute(track.id)}
                  className={`w-5 h-5 rounded text-[9px] font-bold flex items-center justify-center transition-colors ${
                    track.muted ? 'bg-amber-600 text-white' : 'text-zinc-600 hover:text-zinc-400'
                  }`}
                >
                  M
                </button>
                <button
                  onClick={() => useAppStore.getState().toggleSolo(track.id)}
                  className={`w-5 h-5 rounded text-[9px] font-bold flex items-center justify-center transition-colors ${
                    track.solo ? 'bg-yellow-500 text-black' : 'text-zinc-600 hover:text-zinc-400'
                  }`}
                >
                  S
                </button>
              </div>
            </div>

            {/* Track content area */}
            <div className="flex-1 relative">
              {/* Section backgrounds */}
              {sections.map((sec, i) => (
                <div
                  key={i}
                  className={`absolute inset-y-0 ${sec.color} border-r border-zinc-800/30`}
                  style={{
                    left: `${(sec.start / totalBars) * 100}%`,
                    width: `${(sec.length / totalBars) * 100}%`,
                  }}
                />
              ))}
              {/* MIDI clip placeholder */}
              <div
                className="absolute top-1.5 bottom-1.5 rounded-sm opacity-60"
                style={{
                  left: `${((track.id % 3) * 2 / totalBars) * 100}%`,
                  width: `${((8 - (track.id % 3)) / totalBars) * 100}%`,
                  backgroundColor: track.color,
                }}
              >
                <div className="px-1.5 py-0.5 text-[9px] font-medium text-white/80 truncate">
                  {track.name}
                </div>
                {/* Mini note representation */}
                <div className="px-1.5 flex gap-px flex-wrap">
                  {Array.from({ length: 6 + (track.id % 4) }, (_, j) => (
                    <div
                      key={j}
                      className="h-0.5 rounded-full bg-white/40"
                      style={{ width: `${4 + (j % 3) * 3}px`, marginTop: `${(j * 2) % 5}px` }}
                    />
                  ))}
                </div>
              </div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
