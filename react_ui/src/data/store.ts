import { create } from 'zustand';
// Side-effect import: registers audioLevels listener
import '@/data/meters';
import { persist, createJSONStorage } from 'zustand/middleware';
import { juceBridge, addStateListener } from './bridge';
import type { TransportState, MixerState, ChatState, LyriaState } from '@/data/slices';
import {
  useTransportSlice,
  TransportStateID,
  useMixerSlice,
  MixerStateID,
  useChatSlice,
  ChatStateID,
  useLyriaSlice,
  LyriaStateID,
} from '@/data/slices';

// --- Persisted stores (auto-sync with C++ via juceBridge) ---

export const useTransportStore = create<TransportState>()(
  persist(
    (...a) => ({
      ...useTransportSlice(...a),
    }),
    {
      name: TransportStateID,
      storage: createJSONStorage(() => juceBridge),
      version: 1,
    },
  ),
);

export const useMixerStore = create<MixerState>()(
  persist(
    (...a) => ({
      ...useMixerSlice(...a),
    }),
    {
      name: MixerStateID,
      storage: createJSONStorage(() => juceBridge),
      version: 1,
    },
  ),
);

export const useChatStore = create<ChatState>()(
  persist(
    (...a) => ({
      ...useChatSlice(...a),
    }),
    {
      name: ChatStateID,
      storage: createJSONStorage(() => juceBridge),
      version: 2,
      partialize: (state) => {
        // eslint-disable-next-line @typescript-eslint/no-unused-vars
        const { apiKey: _apiKey, ...rest } = state;
        return rest;
      },
    },
  ),
);

export const useLyriaStore = create<LyriaState>()(
  persist(
    (...a) => ({
      ...useLyriaSlice(...a),
    }),
    {
      name: LyriaStateID,
      storage: createJSONStorage(() => juceBridge),
      version: 1,
    },
  ),
);

// --- C++ → JS state listeners (partial updates from engine) ---

addStateListener(TransportStateID, (partialState: Partial<TransportState>) => {
  useTransportStore.setState((prev) => ({
    ...prev,
    ...partialState,
  }));
});

addStateListener(MixerStateID, (partialState: Partial<MixerState>) => {
  useMixerStore.setState((prev) => ({
    ...prev,
    ...partialState,
  }));
});

addStateListener(ChatStateID, (partialState: Partial<ChatState>) => {
  useChatStore.setState((prev) => ({
    ...prev,
    ...partialState,
  }));
});

addStateListener(LyriaStateID, (partialState: Partial<LyriaState>) => {
  useLyriaStore.setState((prev) => ({
    ...prev,
    ...partialState,
  }));
});

// --- Transport position updates (high-frequency from C++ timer) ---
addStateListener('transportPosition', (data: unknown) => {
  const d = data as { position: number; bar: number; looping?: boolean; loopLength?: number; loopBars?: number };
  useTransportStore.setState({
    position: d.position,
    currentBar: d.bar,
    ...(d.looping !== undefined && { looping: d.looping }),
    ...(d.loopLength !== undefined && { loopLength: d.loopLength }),
    ...(d.loopBars !== undefined && { loopBars: d.loopBars }),
    lastPositionUpdate: performance.now(),
  });
});

// --- Track notes updates (from BirdLoader after .bird file load) ---
const SECTION_COLORS = [
  'bg-blue-500/5', 'bg-purple-500/5', 'bg-emerald-500/5', 'bg-amber-500/5',
  'bg-rose-500/5', 'bg-cyan-500/5', 'bg-indigo-500/5', 'bg-teal-500/5',
];

function processTrackNotes(jsonStr: string) {
  const raw = JSON.parse(jsonStr);

  // Support both old array format and new object format
  const trackData = Array.isArray(raw) ? raw : raw.tracks;
  const sectionsData = Array.isArray(raw) ? [] : (raw.sections ?? []);
  const totalBars = Array.isArray(raw) ? 1 : (raw.totalBars ?? 1);

  const TRACK_COLORS = ['#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4', '#FFEAA7', '#DDA0DD', '#98D8C8', '#F7DC6F'];
  const emptySlot = { pluginId: null, pluginName: null, bypassed: false };
  const tracks = trackData.map((t: any, i: number) => ({
    id: t.id,
    name: t.name,
    type: 'midi' as const,
    color: TRACK_COLORS[i % TRACK_COLORS.length],
    muted: false,
    solo: false,
    volume: 80,
    pan: 0,
    instrument: t.plugin
      ? { pluginId: t.plugin.pluginId, pluginName: t.plugin.pluginName, bypassed: false }
      : emptySlot,
    channelStrip: t.channelStrip
      ? { pluginId: t.channelStrip.pluginId, pluginName: t.channelStrip.pluginName, bypassed: false }
      : emptySlot,
    notes: t.notes,
  }));

  const sections = sectionsData.map((s: any, i: number) => ({
    name: s.name,
    start: s.start,
    length: s.length,
    color: SECTION_COLORS[i % SECTION_COLORS.length],
  }));

  useMixerStore.setState({ tracks });
  if (sections.length > 0) {
    useMixerStore.getState().setSections(sections, totalBars);
  }

  return tracks;
}

addStateListener('trackNotes', (jsonStr: string) => {
  try {
    processTrackNotes(jsonStr);
  } catch (e) {
    console.error('[trackNotes] Failed to parse:', e);
  }
});

// --- C++ debug logs forwarded to browser console ---
addStateListener('cppLog', (data: unknown) => {
  const d = data as { message: string };
  if (d?.message) console.log(d.message);
});

// --- Fetch initial track notes on startup ---
if (typeof window !== 'undefined' && window.__JUCE__) {
  import('@/lib').then(({ Juce }) => {
    const getTrackNotes = Juce.getNativeFunction('getTrackNotes');
    // Small delay to let the Edit finish loading
    setTimeout(async () => {
      // Fetch available plugins from C++
      useMixerStore.getState().fetchAvailablePlugins();

      try {
        const jsonStr = await getTrackNotes();
        if (jsonStr && jsonStr !== '[]' && jsonStr !== '{}') {
          const tracks = processTrackNotes(jsonStr);
          console.log('[trackNotes] Loaded', tracks.length, 'tracks from C++');
        }
      } catch (e) {
        console.error('[trackNotes] Initial fetch failed:', e);
      }
    }, 500);
  });
}


