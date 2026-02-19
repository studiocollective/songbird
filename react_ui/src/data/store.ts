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
      version: 1,
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
addStateListener('trackNotes', (jsonStr: string) => {
  try {
    const trackData = JSON.parse(jsonStr) as Array<{
      id: number; name: string;
      plugin?: { pluginId: string; pluginName: string };
      channelStrip?: { pluginId: string; pluginName: string };
      notes: Array<{ pitch: number; beat: number; duration: number; velocity: number }>;
    }>;
    const TRACK_COLORS = ['#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4', '#FFEAA7', '#DDA0DD', '#98D8C8', '#F7DC6F'];
    const emptySlot = { pluginId: null, pluginName: null, bypassed: false };
    const tracks = trackData.map((t, i) => ({
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
    useMixerStore.setState({ tracks });
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
        if (jsonStr && jsonStr !== '[]') {
          const trackData = JSON.parse(jsonStr) as Array<{
            id: number; name: string;
            plugin?: { pluginId: string; pluginName: string };
            channelStrip?: { pluginId: string; pluginName: string };
            notes: Array<{ pitch: number; beat: number; duration: number; velocity: number }>;
          }>;
          const TRACK_COLORS = ['#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4', '#FFEAA7', '#DDA0DD', '#98D8C8', '#F7DC6F'];
          const emptySlot = { pluginId: null, pluginName: null, bypassed: false };
          const tracks = trackData.map((t, i) => ({
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
          useMixerStore.setState({ tracks });
          console.log('[trackNotes] Loaded', tracks.length, 'tracks from C++');
        }
      } catch (e) {
        console.error('[trackNotes] Initial fetch failed:', e);
      }
    }, 500);
  });
}

