import type { StateCreator } from 'zustand';
import { nativeFunction } from '@/data/bridge';

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

export interface LyriaTrackConfig {
  prompts: LyriaPrompt[];
  config: LyriaConfigState;
  quantizeBars: number;  // 0 = off
}

export interface LyriaState {
  apiKey: string;
  connected: boolean;
  buffering: boolean;
  playing: boolean;

  // Global (legacy) prompts/config — applied to all tracks unless overridden
  prompts: LyriaPrompt[];
  config: LyriaConfigState;

  // Per-track configs (keyed by trackId)
  perTrackConfigs: Record<number, LyriaTrackConfig>;

  // Actions
  setApiKey: (key: string) => void;
  setConnected: (connected: boolean) => void;
  setBuffering: (buffering: boolean) => void;
  setPlaying: (playing: boolean) => void;
  setPrompts: (prompts: LyriaPrompt[]) => void;
  addPrompt: (text: string, weight?: number) => void;
  updateConfig: (config: Partial<LyriaConfigState>) => void;

  // Per-track actions
  setTrackLyriaConfig: (trackId: number, config: Partial<LyriaConfigState>) => void;
  setTrackLyriaPrompts: (trackId: number, prompts: LyriaPrompt[]) => void;
  setTrackLyriaQuantize: (trackId: number, bars: number) => void;
  getTrackLyriaConfig: (trackId: number) => LyriaTrackConfig;
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

export const useLyriaSlice: StateCreator<LyriaState> = (set, get) => ({
  apiKey: '',
  connected: false,
  buffering: false,
  playing: false,

  prompts: [],
  config: { ...defaultConfig },
  perTrackConfigs: {},

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

  // Per-track config actions — call through to C++ bridge
  setTrackLyriaConfig: (trackId, config) => {
    set((s) => {
      const existing = s.perTrackConfigs[trackId] ?? {
        prompts: [],
        config: { ...defaultConfig },
        quantizeBars: 0,
      };
      const updated: LyriaTrackConfig = {
        ...existing,
        config: { ...existing.config, ...config },
      };
      return { perTrackConfigs: { ...s.perTrackConfigs, [trackId]: updated } };
    });
    const full = get().perTrackConfigs[trackId]?.config ?? defaultConfig;
    nativeFunction('setLyriaTrackConfig')(trackId, JSON.stringify(full));
  },

  setTrackLyriaPrompts: (trackId, prompts) => {
    set((s) => {
      const existing = s.perTrackConfigs[trackId] ?? {
        prompts: [],
        config: { ...defaultConfig },
        quantizeBars: 0,
      };
      return { perTrackConfigs: { ...s.perTrackConfigs, [trackId]: { ...existing, prompts } } };
    });
    nativeFunction('setLyriaTrackPrompts')(trackId, JSON.stringify(prompts));
  },

  setTrackLyriaQuantize: (trackId, bars) => {
    set((s) => {
      const existing = s.perTrackConfigs[trackId] ?? {
        prompts: [],
        config: { ...defaultConfig },
        quantizeBars: 0,
      };
      return { perTrackConfigs: { ...s.perTrackConfigs, [trackId]: { ...existing, quantizeBars: bars } } };
    });
    nativeFunction('setLyriaQuantize')(trackId, bars);
  },

  getTrackLyriaConfig: (trackId) => {
    return get().perTrackConfigs[trackId] ?? {
      prompts: [],
      config: { ...defaultConfig },
      quantizeBars: 0,
    };
  },
});

export const LyriaStateID = 'songbird-lyria';
