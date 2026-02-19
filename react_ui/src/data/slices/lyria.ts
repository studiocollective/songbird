import type { StateCreator } from 'zustand';

// --- Lyria State Slice ---

export interface LyriaPrompt {
  text: string;
  weight: number;
}

export interface LyriaConfigState {
  bpm: number;
  useBpm: boolean;
  temperature: number;
  topK: number;
  guidance: number;
  density: number;
  useDensity: boolean;
  brightness: number;
  useBrightness: boolean;
  muteBass: boolean;
  muteDrums: boolean;
  muteOther: boolean;
}

export interface LyriaState {
  apiKey: string;
  connected: boolean;
  buffering: boolean;
  playing: boolean;

  prompts: LyriaPrompt[];
  config: LyriaConfigState;

  // Actions
  setApiKey: (key: string) => void;
  setConnected: (connected: boolean) => void;
  setBuffering: (buffering: boolean) => void;
  setPlaying: (playing: boolean) => void;
  setPrompts: (prompts: LyriaPrompt[]) => void;
  addPrompt: (text: string, weight?: number) => void;
  updateConfig: (config: Partial<LyriaConfigState>) => void;
}

const defaultConfig: LyriaConfigState = {
  bpm: 120,
  useBpm: false,
  temperature: 1.0,
  topK: 250,
  guidance: 3.0,
  density: 0.5,
  useDensity: false,
  brightness: 0.5,
  useBrightness: false,
  muteBass: false,
  muteDrums: false,
  muteOther: false,
};

export const useLyriaSlice: StateCreator<LyriaState> = (set) => ({
  apiKey: '',
  connected: false,
  buffering: false,
  playing: false,

  prompts: [],
  config: { ...defaultConfig },

  setApiKey: (key) => set({ apiKey: key }),
  setConnected: (connected) => set({ connected }),
  setBuffering: (buffering) => set({ buffering }),
  setPlaying: (playing) => set({ playing }),
  setPrompts: (prompts) => set({ prompts }),
  addPrompt: (text, weight = 1.0) =>
    set((s) => ({
      prompts: [...s.prompts, { text, weight }],
    })),
  updateConfig: (partial) =>
    set((s) => ({
      config: { ...s.config, ...partial },
    })),
});

export const LyriaStateID = 'songbird-lyria';
