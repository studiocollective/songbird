import type { StateCreator } from 'zustand';
import { nativeFunction } from '@/data/bridge';

// --- Note data from C++ ---
export interface NoteData {
  pitch: number;
  beat: number;
  duration: number;
  velocity: number;
}

// --- Track Types ---
export type TrackType = 'midi' | 'audio' | 'generated';

export interface PluginSlot {
  pluginId: string | null;
  pluginName: string | null;
  bypassed: boolean;
}

export interface Track {
  id: number;
  name: string;
  type: TrackType;
  color: string;
  muted: boolean;
  solo: boolean;
  volume: number;
  pan: number;
  instrument: PluginSlot;   // only used for midi tracks
  channelStrip: PluginSlot;
  notes: NoteData[];
}

const defaultTracks: Track[] = [];
// Tracks are loaded dynamically from the .bird file via trackNotes events

// --- Mixer State Slice ---
export interface MixerState {
  initialized: boolean;
  initialize: () => void;

  tracks: Track[];
  mixerOpen: boolean;

  toggleMixer: () => void;
  toggleMute: (id: number) => void;
  toggleSolo: (id: number) => void;
  setVolume: (id: number, volume: number) => void;
  setPan: (id: number, pan: number) => void;
  setTrackName: (id: number, name: string) => void;

  // Plugin actions
  setInstrument: (id: number, pluginId: string | null, pluginName: string | null) => void;
  setChannelStrip: (id: number, pluginId: string | null, pluginName: string | null) => void;
  toggleInstrumentBypass: (id: number) => void;
  toggleChannelStripBypass: (id: number) => void;
  openPlugin: (trackId: number, slotType: 'instrument' | 'channelStrip') => void;

  // Dynamic Plugin List
  availableInstruments: { id: string; name: string; vendor: string; category: string }[];
  availableEffects: { id: string; name: string; vendor: string; category: string }[];
  fetchAvailablePlugins: () => Promise<void>;

  // Track data from bird file
  setTracks: (tracks: Track[]) => void;
  setTrackNotes: (trackId: number, notes: NoteData[]) => void;
}

export const useMixerSlice: StateCreator<MixerState> = (set, get) => ({
  initialized: false,
  initialize: () => set({ initialized: true }),

  tracks: defaultTracks,
  mixerOpen: true,

  availableInstruments: [],
  availableEffects: [],
  fetchAvailablePlugins: async () => {
      try {
          const result = await nativeFunction('getAvailablePlugins')();
          const data = typeof result === 'string' ? JSON.parse(result) : result;
          if (data && data.instruments) {
              set({ 
                  availableInstruments: data.instruments,
                  availableEffects: data.effects 
              });
              console.log(`[Mixer] Loaded ${data.instruments.length} instruments and ${data.effects.length} effects`);
          }
      } catch (e) {
          console.error("Failed to fetch available plugins", e);
      }
  },

  toggleMixer: () => set((s) => ({ mixerOpen: !s.mixerOpen })),
  toggleMute: (id) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, muted: !t.muted } : t)),
    })),
  toggleSolo: (id) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, solo: !t.solo } : t)),
    })),
  setVolume: (id, volume) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, volume } : t)),
    })),
  setPan: (id, pan) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, pan } : t)),
    })),
  setTrackName: (id, name) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, name } : t)),
    })),

  // Plugin actions
  setInstrument: (id, pluginId, pluginName) => {
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, instrument: { pluginId, pluginName, bypassed: false } } : t
      ),
    }));
    nativeFunction('changePlugin')(id, 'instrument', pluginName ?? '');
  },
  setChannelStrip: (id, pluginId, pluginName) => {
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, channelStrip: { pluginId, pluginName, bypassed: false } } : t
      ),
    }));
    nativeFunction('changePlugin')(id, 'channelStrip', pluginName ?? '');
  },
  toggleInstrumentBypass: (id) =>
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, instrument: { ...t.instrument, bypassed: !t.instrument.bypassed } } : t
      ),
    })),
  toggleChannelStripBypass: (id) =>
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, channelStrip: { ...t.channelStrip, bypassed: !t.channelStrip.bypassed } } : t
      ),
    })),
  openPlugin: (trackId, slotType) => {
    const track = get().tracks.find((t) => t.id === trackId);
    const slot = track?.[slotType];
    if (slot?.pluginId) {
      console.log(`[openPlugin] track=${trackId} slot=${slotType} plugin=${slot.pluginId}`);
      nativeFunction('openPlugin')(trackId, slotType, slot.pluginId);
    }
  },
  setTracks: (tracks) => set({ tracks }),
  setTrackNotes: (trackId, notes) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === trackId ? { ...t, notes } : t)),
    })),
});


export const MixerStateID = 'songbird-mixer';
