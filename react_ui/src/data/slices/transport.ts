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
  loopBars: number;     // loop length in bars
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
  position: 0,
  lastPositionUpdate: 0,
  keySignature: null,

  play: () => set({ playing: true, lastPositionUpdate: performance.now() }),
  stop: () => set({ playing: false, position: 0, currentBar: 1, lastPositionUpdate: performance.now() }),
  togglePlaying: () => set((s) => ({ playing: !s.playing, lastPositionUpdate: performance.now() })),
  setBpm: (bpm) => set({ bpm }),
  setPosition: (position) => set({ position, lastPositionUpdate: performance.now() }),
  setCurrentBar: (bar) => set({ currentBar: bar }),
  setCurrentSection: (section) => set({ currentSection: section }),
  toggleLooping: () => set((s) => ({ looping: !s.looping })),
  setKeySignature: (key) => set({ keySignature: key }),
});

export const TransportStateID = 'songbird-transport';
