import type { StateCreator } from 'zustand';

// --- Transport State Slice ---
export interface TransportState {
  initialized: boolean;
  initialize: () => void;

  playing: boolean;
  bpm: number;
  currentBar: number;
  currentSection: string;
  looping: boolean;
  loopLength: number;   // loop length in seconds
  loopBars: number;     // loop end in bars
  loopStartBar: number; // loop start in bars
  position: number;
  lastPositionUpdate: number; // timestamp of last position update (for smooth animation)
  keySignature: string | null;

  play: () => void;
  stop: () => void;
  togglePlaying: () => void;
  setBpm: (bpm: number) => void;
  setPosition: (position: number) => void;
  setCurrentBar: (bar: number) => void;
  setCurrentSection: (section: string) => void;
  toggleLooping: () => void;
  setLoopRange: (startBar: number, endBar: number) => void;
  setKeySignature: (key: string | null) => void;
}

export const useTransportSlice: StateCreator<TransportState> = (set) => ({
  initialized: false,
  initialize: () => set({ initialized: true }),

  playing: false,
  bpm: 120,
  currentBar: 1,
  currentSection: 'verse',
  looping: true,
  loopLength: 0,
  loopBars: 0,
  loopStartBar: 0,
  position: 0,
  lastPositionUpdate: 0,
  keySignature: null,

  play: () => {
    if (typeof window !== 'undefined' && window.__JUCE__) {
      import('@/lib').then(({ Juce }) => Juce.getNativeFunction('transportPlay')?.());
    }
    set({ playing: true, lastPositionUpdate: performance.now() });
  },
  stop: () => {
    if (typeof window !== 'undefined' && window.__JUCE__) {
      import('@/lib').then(({ Juce }) => Juce.getNativeFunction('transportStop')?.());
    }
    set({ playing: false, position: 0, currentBar: 1, lastPositionUpdate: performance.now() });
  },
  togglePlaying: () => {
    set((s) => {
      const next = !s.playing;
      if (typeof window !== 'undefined' && window.__JUCE__) {
        import('@/lib').then(({ Juce }) => {
          if (next) Juce.getNativeFunction('transportPlay')?.();
          else Juce.getNativeFunction('transportStop')?.();
        });
      }
      return { playing: next, ...(next ? {} : { position: 0, currentBar: 1 }), lastPositionUpdate: performance.now() };
    });
  },
  setBpm: (bpm) => set({ bpm }),
  setPosition: (position) => {
    if (typeof window !== 'undefined' && window.__JUCE__) {
      import('@/lib').then(({ Juce }) => Juce.getNativeFunction('transportSeek')?.(position));
    }
    set({ position, lastPositionUpdate: performance.now() });
  },
  setCurrentBar: (bar) => set({ currentBar: bar }),
  setCurrentSection: (section) => set({ currentSection: section }),
  toggleLooping: () => set((s) => ({ looping: !s.looping })),
  setLoopRange: (startBar: number, endBar: number) => {
    if (typeof window !== 'undefined' && window.__JUCE__) {
      import('@/lib').then(({ Juce }) => Juce.getNativeFunction('setLoopRange')?.(startBar, endBar));
    }
    set({ loopStartBar: startBar, loopBars: endBar });
  },
  setKeySignature: (key) => set({ keySignature: key }),
});

export const TransportStateID = 'songbird-transport';
