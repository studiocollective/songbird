import type { StateCreator } from 'zustand';
import { nativeFunction } from '@/data/bridge';

// --- Note data from C++ ---
export interface NoteData {
  pitch: number;
  beat: number;
  duration: number;
  velocity: number;
}

export type TrackType = 'midi' | 'audio';

// MIDI input: 'all' = all devices, 'computer-keyboard' = keyboard as MIDI, or device name string
export type MidiInput = 'all' | 'computer-keyboard' | string;

export interface AutomationPoint {
  time: number;
  value: number;
  shape: number; // 0=Step, 1=Linear, 2=Exponential, 3=Logarithmic, 4=Smooth
}

export interface AutomationCurve {
  macro: string;
  points: AutomationPoint[];
}

export interface Section {
  name: string;
  start: number;   // bar offset
  length: number;  // bars
  color: string;
}

export interface PluginSlot {
  pluginId: string | null;
  pluginName: string | null;
  bypassed: boolean;
}

export interface AudioSource {
  type: 'hardware' | 'loopback';
  deviceName?: string;
  sourceTrackId?: number;
}

export interface AudioClip {
  id: string;
  filePath: string;       // relative to project samples/ dir
  startBeat: number;      // position on timeline
  duration: number;       // duration in beats
  cropStart: number;      // sample crop start (seconds)
  cropEnd: number;        // sample crop end (seconds)
  looping: boolean;
  quantized: boolean;
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
  fx: PluginSlot;           // Insert effect (e.g. reverb, delay)
  channelStrip: PluginSlot;
  notes: NoteData[];
  loopLengthBeats?: number;
  automation?: AutomationCurve[];
  sends?: number[];
  isReturn?: boolean;
  isMaster?: boolean;
  sidechainTrackId?: number | null;
  sidechainSensitivity?: number;
  // Recording state
  recordArmed?: boolean;
  hasRecordedData?: boolean;
  audioSource?: AudioSource | null;
  midiInput?: MidiInput | null;
  inputMonitoring?: boolean;
  // Audio clips (for audio tracks)
  audioClips?: AudioClip[];
}

const defaultTracks: Track[] = [];
// Tracks are loaded dynamically from the .bird file via trackState events

// --- Mixer State Slice ---
export interface SelectedClip {
  trackId: number;
  sectionIndex: number;  // which section occurrence in the arrangement
}

export interface MixerState {
  initialized: boolean;
  initialize: () => void;

  tracks: Track[];
  sections: Section[];
  totalBars: number;
  mixerOpen: boolean;
  returnsOpen: boolean;
  recordStripOpen: boolean;
  keyboardMidiMode: boolean;

  // MIDI Editor
  midiEditorOpen: boolean;
  selectedClip: SelectedClip | null;
  midiEditorGridDiv: number;  // grid subdivision: 4=quarter, 8=eighth, 16=sixteenth
  openMidiEditor: (trackId: number, sectionIndex: number) => void;
  closeMidiEditor: () => void;
  setMidiEditorGridDiv: (div: number) => void;

  // Sample Editor
  sampleEditorOpen: boolean;
  selectedAudioClip: { trackId: number; clipId: string } | null;
  openSampleEditor: (trackId: number, clipId: string) => void;
  closeSampleEditor: () => void;

  toggleMixer: () => void;
  toggleReturns: () => void;
  toggleRecordStrip: () => void;
  toggleKeyboardMidiMode: () => void;
  toggleMute: (id: number) => void;
  toggleSolo: (id: number) => void;
  setVolume: (id: number, volume: number) => void;
  setPan: (id: number, pan: number) => void;
  setSendLevel: (id: number, bus: number, level: number) => void;
  setTrackName: (id: number, name: string) => void;
  setSidechainSource: (destId: number, sourceId: number | null) => void;

  // Recording actions
  setMidiRecordArm: (id: number, armed: boolean) => void;
  setAudioRecordArm: (id: number, armed: boolean) => void;
  setAudioSource: (id: number, source: AudioSource | null) => void;
  setMidiInput: (id: number, input: MidiInput | null) => void;
  toggleInputMonitoring: (id: number) => void;
  addAudioTrack: () => Promise<number>;
  addMidiTrack: () => Promise<number>;
  removeAudioTrack: (id: number) => void;
  clearRecordedData: (id: number) => void;
  setSidechainSensitivity: (destId: number, value: number) => void;

  // Plugin actions
  setInstrument: (id: number, pluginId: string | null, pluginName: string | null) => void;
  setFx: (id: number, pluginId: string | null, pluginName: string | null) => void;
  setChannelStrip: (id: number, pluginId: string | null, pluginName: string | null) => void;
  toggleInstrumentBypass: (id: number) => void;
  toggleFxBypass: (id: number) => void;
  toggleChannelStripBypass: (id: number) => void;
  openPlugin: (trackId: number, slotType: 'instrument' | 'fx' | 'channelStrip') => void;

  // Dynamic Plugin List
  availableInstruments: { id: string; name: string; vendor: string; category: string }[];
  availableChannelStrips: { id: string; name: string; vendor: string; category: string }[];
  availableFx: { id: string; name: string; vendor: string; category: string }[];
  fetchAvailablePlugins: () => Promise<void>;

  // Track data from bird file
  setTracks: (tracks: Track[]) => void;
  setTrackNotes: (trackId: number, notes: NoteData[]) => void;
  setSections: (sections: Section[], totalBars: number) => void;
}

export const useMixerSlice: StateCreator<MixerState> = (set, get) => ({
  initialized: false,
  initialize: () => set({ initialized: true }),

  tracks: defaultTracks,
  sections: [],
  totalBars: 1,
  mixerOpen: true,
  returnsOpen: false,
  recordStripOpen: false,
  keyboardMidiMode: false,

  // MIDI Editor
  midiEditorOpen: false,
  selectedClip: null,
  midiEditorGridDiv: 8,
  openMidiEditor: (trackId, sectionIndex) => set({
    midiEditorOpen: true,
    selectedClip: { trackId, sectionIndex },
    sampleEditorOpen: false,
    selectedAudioClip: null,
  }),
  closeMidiEditor: () => set({
    midiEditorOpen: false,
    selectedClip: null,
  }),
  setMidiEditorGridDiv: (div) => set({ midiEditorGridDiv: div }),

  // Sample Editor
  sampleEditorOpen: false,
  selectedAudioClip: null,
  openSampleEditor: (trackId, clipId) => set({
    sampleEditorOpen: true,
    selectedAudioClip: { trackId, clipId },
    midiEditorOpen: false,
    selectedClip: null,
  }),
  closeSampleEditor: () => set({
    sampleEditorOpen: false,
    selectedAudioClip: null,
  }),

  availableInstruments: [],
  availableChannelStrips: [],
  availableFx: [],
  fetchAvailablePlugins: async () => {
      try {
          const result = await nativeFunction('getAvailablePlugins')();
          const data = typeof result === 'string' ? JSON.parse(result) : result;
          if (data && data.instruments) {
              set({ 
                  availableInstruments: data.instruments,
                  availableChannelStrips: data.effects,
                  availableFx: data.fx || []
              });
              console.log(`[Mixer] Loaded ${data.instruments.length} instruments, ${data.effects.length} effects, and ${(data.fx || []).length} fx`);
          }
      } catch (e) {
          console.error("Failed to fetch available plugins", e);
      }
  },

  toggleMixer: () => set((s) => ({ mixerOpen: !s.mixerOpen })),
  toggleReturns: () => set((s) => ({ returnsOpen: !s.returnsOpen })),
  toggleRecordStrip: () => set((s) => ({ recordStripOpen: !s.recordStripOpen })),
  toggleKeyboardMidiMode: () => set((s) => ({ keyboardMidiMode: !s.keyboardMidiMode })),
  toggleMute: (id) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, muted: !t.muted } : t)),
    })),
  toggleSolo: (id) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, solo: !t.solo } : t)),
    })),
  setVolume: (id, volume) =>
    set((s) => {
      const v = Math.round(volume);
      return {
        tracks: s.tracks.map((t) => (t.id === id ? { ...t, volume: v } : t)),
      };
    }),
  setPan: (id, pan) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, pan: Math.round(pan) } : t)),
    })),
  setSendLevel: (id, bus, level) =>
    set((s) => ({
      tracks: s.tracks.map((t) => {
        if (t.id === id) {
          const sends = [...(t.sends || [0, 0, 0, 0])];
          sends[bus] = level;
          return { ...t, sends };
        }
        return t;
      }),
    })),
  setTrackName: (id, name) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, name } : t)),
    })),
  setSidechainSource: (destId, sourceId) => {
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === destId ? { ...t, sidechainTrackId: sourceId } : t
      ),
    }));
    nativeFunction('setSidechainSource')(destId, sourceId ?? -1);
  },
  setSidechainSensitivity: (destId, value) => {
    const clamped = Math.max(0, Math.min(1, value));
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === destId ? { ...t, sidechainSensitivity: clamped } : t
      ),
    }));
    // Console 1 param 29: "Compression" = threshold/amount (higher = more aggressive)
    nativeFunction('setPluginParam')(destId, 'Compression', clamped);
  },

  // Plugin actions
  setInstrument: (id, pluginId, pluginName) => {
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, instrument: { pluginId, pluginName, bypassed: false } } : t
      ),
    }));
    nativeFunction('changePlugin')(id, 'instrument', pluginName ?? '');
  },
  setFx: (id, pluginId, pluginName) => {
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, fx: { pluginId, pluginName, bypassed: false } } : t
      ),
    }));
    nativeFunction('changePlugin')(id, 'fx', pluginName ?? '');
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
  toggleFxBypass: (id) =>
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, fx: { ...t.fx, bypassed: !t.fx.bypassed } } : t
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
  setSections: (sections, totalBars) => set({ sections, totalBars }),

  // --- Recording ---
  setMidiRecordArm: (id, armed) => {
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, recordArmed: armed } : t)),
    }));
    nativeFunction('setMidiRecordArm')(id, armed);
  },
  setAudioRecordArm: (id, armed) => {
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, recordArmed: armed } : t)),
    }));
    nativeFunction('setAudioRecordArm')(id, armed);
  },
  setAudioSource: (id, source) => {
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, audioSource: source } : t)),
    }));
    if (source?.type === 'hardware')
      nativeFunction('setAudioRecordSource')(id, 'hardware', source.deviceName ?? '');
    else if (source?.type === 'loopback')
      nativeFunction('setAudioRecordSource')(id, 'loopback', source.sourceTrackId ?? -1);
  },
  setMidiInput: (id, input) => {
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, midiInput: input } : t)),
    }));
    nativeFunction('setMidiInput')(id, input ?? '');
  },
  toggleInputMonitoring: (id) => {
    const track = get().tracks.find((t) => t.id === id);
    const newVal = !(track?.inputMonitoring ?? false);
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, inputMonitoring: newVal } : t)),
    }));
    nativeFunction('setInputMonitoring')(id, newVal);
  },
  addAudioTrack: async () => {
    const result = await nativeFunction('addAudioTrack')();
    const data = typeof result === 'string' ? JSON.parse(result) : result;
    if (!data?.success) return -1;

    const TRACK_COLORS = ['#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4', '#FFEAA7', '#DDA0DD', '#98D8C8', '#F7DC6F'];
    const emptySlot = { pluginId: null, pluginName: null, bypassed: false };
    const existing = get().tracks;
    const id = data.trackId as number;

    const newTrack: Track = {
      id,
      name: data.name ?? `Audio ${id + 1}`,
      type: 'audio',
      color: TRACK_COLORS[existing.filter(t => !t.isReturn && !t.isMaster).length % TRACK_COLORS.length],
      muted: false,
      solo: false,
      volume: data.volume ?? 80,
      pan: data.pan ?? 0,
      instrument: emptySlot,
      fx: emptySlot,
      channelStrip: emptySlot,
      notes: [],
      sends: [0, 0, 0, 0],
    };

    set({ tracks: [...existing, newTrack] });
    
    // Auto-assign default channel strip (Console 1)
    const availableEffects = get().availableChannelStrips;
    if (availableEffects.length > 0) {
      const console1 = availableEffects.find(fx => fx.name.includes('Console 1')) || availableEffects[0];
      setTimeout(() => get().setChannelStrip(id, console1.id, console1.name), 50);
    }
    
    return id;
  },
  addMidiTrack: async () => {
    const result = await nativeFunction('addMidiTrack')();
    const data = typeof result === 'string' ? JSON.parse(result) : result;
    if (!data?.success) return -1;

    const TRACK_COLORS = ['#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4', '#FFEAA7', '#DDA0DD', '#98D8C8', '#F7DC6F'];
    const emptySlot = { pluginId: null, pluginName: null, bypassed: false };
    const existing = get().tracks;
    const id = data.trackId as number;

    const newTrack: Track = {
      id,
      name: data.name ?? `Track ${id + 1}`,
      type: 'midi',
      color: TRACK_COLORS[existing.filter(t => !t.isReturn && !t.isMaster).length % TRACK_COLORS.length],
      muted: false,
      solo: false,
      volume: data.volume ?? 80,
      pan: data.pan ?? 0,
      instrument: emptySlot,
      fx: emptySlot,
      channelStrip: emptySlot,
      notes: [],
      sends: [0, 0, 0, 0],
    };

    set({ tracks: [...existing, newTrack] });

    // Auto-assign default channel strip (Console 1)
    const availableEffects = get().availableChannelStrips;
    if (availableEffects.length > 0) {
      const console1 = availableEffects.find(fx => fx.name.includes('Console 1')) || availableEffects[0];
      setTimeout(() => get().setChannelStrip(id, console1.id, console1.name), 50);
    }

    return id;
  },
  removeAudioTrack: (id) => {
    nativeFunction('removeAudioTrack')(id);
    set((s) => ({ tracks: s.tracks.filter((t) => t.id !== id) }));
  },
  clearRecordedData: (id) => {
    const track = get().tracks.find((t) => t.id === id);
    if (!track) return;
    if (track.type === 'midi') nativeFunction('clearRecordedMidi')(id);
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, hasRecordedData: false, recordArmed: false } : t
      ),
    }));
  },
});


export const MixerStateID = 'songbird-mixer';
