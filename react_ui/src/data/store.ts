import { create } from 'zustand';

interface Track {
  id: number;
  name: string;
  color: string;
  muted: boolean;
  solo: boolean;
  volume: number;
  pan: number;
}

interface AppState {
  // Panels
  mixerOpen: boolean;
  chatOpen: boolean;
  toggleMixer: () => void;
  toggleChat: () => void;

  // Transport
  playing: boolean;
  bpm: number;
  currentBar: number;
  currentSection: string;
  setPlaying: (playing: boolean) => void;
  setBpm: (bpm: number) => void;

  // Tracks
  tracks: Track[];
  toggleMute: (id: number) => void;
  toggleSolo: (id: number) => void;
  setVolume: (id: number, volume: number) => void;
  setPan: (id: number, pan: number) => void;

  // Chat
  chatMessages: { role: 'user' | 'assistant'; content: string }[];
  chatInput: string;
  setChatInput: (input: string) => void;
  addMessage: (role: 'user' | 'assistant', content: string) => void;
}

const TRACK_COLORS = [
  '#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4',
  '#FFEAA7', '#DDA0DD', '#98D8C8', '#F7DC6F',
];

const defaultTracks: Track[] = Array.from({ length: 8 }, (_, i) => ({
  id: i + 1,
  name: `Track ${i + 1}`,
  color: TRACK_COLORS[i],
  muted: false,
  solo: false,
  volume: 80,
  pan: 0,
}));

export const useAppStore = create<AppState>((set) => ({
  mixerOpen: false,
  chatOpen: false,
  toggleMixer: () => set((s) => ({ mixerOpen: !s.mixerOpen })),
  toggleChat: () => set((s) => ({ chatOpen: !s.chatOpen })),

  playing: false,
  bpm: 120,
  currentBar: 1,
  currentSection: 'verse',
  setPlaying: (playing) => set({ playing }),
  setBpm: (bpm) => set({ bpm }),

  tracks: defaultTracks,
  toggleMute: (id) =>
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, muted: !t.muted } : t
      ),
    })),
  toggleSolo: (id) =>
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, solo: !t.solo } : t
      ),
    })),
  setVolume: (id, volume) =>
    set((s) => ({
      tracks: s.tracks.map((t) =>
        t.id === id ? { ...t, volume } : t
      ),
    })),
  setPan: (id, pan) =>
    set((s) => ({
      tracks: s.tracks.map((t) => (t.id === id ? { ...t, pan } : t)),
    })),

  chatMessages: [],
  chatInput: '',
  setChatInput: (chatInput) => set({ chatInput }),
  addMessage: (role, content) =>
    set((s) => ({
      chatMessages: [...s.chatMessages, { role, content }],
    })),
}));
