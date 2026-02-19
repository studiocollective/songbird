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
  position: number;

  play: () => void;
  stop: () => void;
  togglePlaying: () => void;
  setBpm: (bpm: number) => void;
  setPosition: (position: number) => void;
  setCurrentBar: (bar: number) => void;
  setCurrentSection: (section: string) => void;
  toggleLooping: () => void;
}

export const useTransportSlice: StateCreator<TransportState> = (set) => ({
  initialized: false,
  initialize: () => set({ initialized: true }),

  playing: false,
  bpm: 120,
  currentBar: 1,
  currentSection: 'verse',
  looping: true,
  position: 0,

  play: () => set({ playing: true }),
  stop: () => set({ playing: false, position: 0, currentBar: 1 }),
  togglePlaying: () => set((s) => ({ playing: !s.playing })),
  setBpm: (bpm) => set({ bpm }),
  setPosition: (position) => set({ position }),
  setCurrentBar: (bar) => set({ currentBar: bar }),
  setCurrentSection: (section) => set({ currentSection: section }),
  toggleLooping: () => set((s) => ({ looping: !s.looping })),
});

export const TransportStateID = 'songbird-transport';
