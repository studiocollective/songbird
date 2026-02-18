import { useAppStore } from '@/data/store';

export function Transport() {
  const { playing, setPlaying, bpm, setBpm, currentBar, currentSection, toggleMixer, mixerOpen, toggleChat, chatOpen } = useAppStore();

  return (
    <div className="h-12 bg-zinc-900 border-b border-zinc-800 flex items-center px-4 gap-3 select-none shrink-0">
      {/* Transport controls */}
      <div className="flex items-center gap-1">
        <button
          onClick={() => setPlaying(false)}
          className="w-8 h-8 rounded flex items-center justify-center hover:bg-zinc-800 text-zinc-400 hover:text-white transition-colors"
          title="Stop"
        >
          <svg width="12" height="12" viewBox="0 0 12 12" fill="currentColor">
            <rect width="12" height="12" rx="1" />
          </svg>
        </button>
        <button
          onClick={() => setPlaying(!playing)}
          className={`w-8 h-8 rounded flex items-center justify-center transition-colors ${
            playing ? 'bg-emerald-600 text-white' : 'hover:bg-zinc-800 text-zinc-400 hover:text-white'
          }`}
          title={playing ? 'Pause' : 'Play'}
        >
          {playing ? (
            <svg width="12" height="14" viewBox="0 0 12 14" fill="currentColor">
              <rect width="4" height="14" rx="1" />
              <rect x="8" width="4" height="14" rx="1" />
            </svg>
          ) : (
            <svg width="12" height="14" viewBox="0 0 12 14" fill="currentColor">
              <path d="M0 0L12 7L0 14V0Z" />
            </svg>
          )}
        </button>
        <button
          className="w-8 h-8 rounded flex items-center justify-center hover:bg-zinc-800 text-zinc-400 hover:text-white transition-colors"
          title="Record"
        >
          <div className="w-3 h-3 rounded-full bg-red-500" />
        </button>
      </div>

      {/* Divider */}
      <div className="w-px h-6 bg-zinc-700" />

      {/* BPM */}
      <div className="flex items-center gap-2">
        <span className="text-[10px] text-zinc-500 uppercase tracking-wider">BPM</span>
        <input
          type="number"
          value={bpm}
          onChange={(e) => setBpm(Number(e.target.value))}
          className="w-14 h-7 bg-zinc-800 border border-zinc-700 rounded text-center text-sm text-white font-mono focus:outline-none focus:border-zinc-500"
        />
      </div>

      {/* Divider */}
      <div className="w-px h-6 bg-zinc-700" />

      {/* Position */}
      <div className="flex items-center gap-3">
        <div className="text-sm font-mono text-white">
          <span className="text-zinc-500">Bar </span>
          <span className="text-emerald-400">{currentBar}</span>
        </div>
        <div className="px-2 py-0.5 rounded bg-zinc-800 border border-zinc-700">
          <span className="text-xs text-zinc-400">{currentSection}</span>
        </div>
      </div>

      {/* Spacer */}
      <div className="flex-1" />

      {/* Title */}
      <span className="text-sm font-medium text-zinc-400 tracking-wide">🐦 Songbird Player</span>

      {/* Spacer */}
      <div className="flex-1" />

      {/* Panel toggles */}
      <div className="flex items-center gap-1">
        <button
          onClick={toggleMixer}
          className={`px-3 h-7 rounded text-xs font-medium transition-colors ${
            mixerOpen ? 'bg-zinc-700 text-white' : 'text-zinc-500 hover:text-zinc-300 hover:bg-zinc-800'
          }`}
        >
          Mixer
        </button>
        <button
          onClick={toggleChat}
          className={`px-3 h-7 rounded text-xs font-medium transition-colors ${
            chatOpen ? 'bg-zinc-700 text-white' : 'text-zinc-500 hover:text-zinc-300 hover:bg-zinc-800'
          }`}
        >
          Chat
        </button>
      </div>
    </div>
  );
}
