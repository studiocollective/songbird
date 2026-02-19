import type { StateCreator } from 'zustand';
import { nativeFunction } from '@/data/bridge';

// --- Track Types ---
export type TrackType = 'midi' | 'audio' | 'generated';

export interface PluginSlot {
  pluginId: string | null;
  pluginName: string | null;
  bypassed: boolean;
}

const emptySlot: PluginSlot = { pluginId: null, pluginName: null, bypassed: false };

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
}

const TRACK_COLORS = [
  '#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4',
  '#FFEAA7', '#DDA0DD', '#98D8C8', '#F7DC6F',
];

const defaultTracks: Track[] = [
  // MIDI tracks (1–4) — pre-populated with Arturia instruments + Console 1
  { id: 1, name: 'Keys',    type: 'midi',  color: TRACK_COLORS[0], muted: false, solo: false, volume: 80, pan: 0,
    instrument:   { pluginId: 'arturia.analog-lab-v', pluginName: 'Analog Lab V', bypassed: false },
    channelStrip: { pluginId: 'softube.console-1',    pluginName: 'Console 1',    bypassed: false } },
  { id: 2, name: 'Synth',   type: 'midi',  color: TRACK_COLORS[1], muted: false, solo: false, volume: 80, pan: 0,
    instrument:   { pluginId: 'arturia.pigments',     pluginName: 'Pigments',     bypassed: false },
    channelStrip: { pluginId: 'softube.console-1',    pluginName: 'Console 1',    bypassed: false } },
  { id: 3, name: 'Bass',    type: 'midi',  color: TRACK_COLORS[2], muted: false, solo: false, volume: 80, pan: 0,
    instrument:   { pluginId: 'arturia.mini-v',       pluginName: 'Mini V',       bypassed: false },
    channelStrip: { pluginId: 'softube.console-1',    pluginName: 'Console 1',    bypassed: false } },
  { id: 4, name: 'Pad',     type: 'midi',  color: TRACK_COLORS[3], muted: false, solo: false, volume: 80, pan: 0,
    instrument:   { pluginId: 'arturia.cs-80-v',      pluginName: 'CS-80 V',      bypassed: false },
    channelStrip: { pluginId: 'softube.console-1',    pluginName: 'Console 1',    bypassed: false } },
  // Audio tracks (5–8) — Console 1 only
  { id: 5, name: 'Drums',   type: 'audio', color: TRACK_COLORS[4], muted: false, solo: false, volume: 80, pan: 0,
    instrument:   { ...emptySlot },
    channelStrip: { pluginId: 'softube.console-1',    pluginName: 'Console 1',    bypassed: false } },
  { id: 6, name: 'Guitar',  type: 'audio', color: TRACK_COLORS[5], muted: false, solo: false, volume: 80, pan: 0,
    instrument:   { ...emptySlot },
    channelStrip: { pluginId: 'softube.console-1',    pluginName: 'Console 1',    bypassed: false } },
  { id: 7, name: 'Vocals',  type: 'audio', color: TRACK_COLORS[6], muted: false, solo: false, volume: 80, pan: 0,
    instrument:   { ...emptySlot },
    channelStrip: { pluginId: 'softube.console-1',    pluginName: 'Console 1',    bypassed: false } },
  { id: 8, name: 'FX',      type: 'audio', color: TRACK_COLORS[7], muted: false, solo: false, volume: 80, pan: 0,
    instrument:   { ...emptySlot },
    channelStrip: { pluginId: 'softube.console-1',    pluginName: 'Console 1',    bypassed: false } },
];

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
}

export const useMixerSlice: StateCreator<MixerState> = (set, get) => ({
  initialized: false,
  initialize: () => set({ initialized: true }),

  tracks: defaultTracks,
  mixerOpen: false,

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
  setInstrument: (id, pluginId, pluginName) =>
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, instrument: { pluginId, pluginName, bypassed: false } } : t
      ),
    })),
  setChannelStrip: (id, pluginId, pluginName) =>
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, channelStrip: { pluginId, pluginName, bypassed: false } } : t
      ),
    })),
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
});

export const MixerStateID = 'songbird-mixer';
