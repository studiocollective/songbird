import { create } from 'zustand';
// Side-effect import: registers audioLevels listener
import '@/data/meters';
import { persist, createJSONStorage } from 'zustand/middleware';
import { juceBridge, addStateListener, nativeFunction } from './bridge';
import type { TransportState, MixerState, ChatState, LyriaState, TrackType, NoteData } from '@/data/slices';
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

// --- Hydration tracking: signal C++ when all stores are ready ---
const TOTAL_STORES = 4;
let hydratedCount = 0;
function onStoreHydrated() {
  hydratedCount++;
  if (hydratedCount >= TOTAL_STORES) {
    console.log('[Store] All stores hydrated — signaling reactReady');
    nativeFunction('reactReady')();
  }
}

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
      partialize: (state) => {
        // Exclude properties that shouldn't echo back to C++
        // eslint-disable-next-line @typescript-eslint/no-unused-vars
        const { position, playing, currentBar, currentSection, lastPositionUpdate, loopLength, loopBars, loopStartBar, initialized, keySignature, scale, bpm, looping, ...rest } = state;
        return rest;
      },
      onRehydrateStorage: () => () => onStoreHydrated(),
    }
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
      onRehydrateStorage: () => () => onStoreHydrated(),
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
      version: 3,
      partialize: (state) => {
        // Exclude: apiKey (security), threads/threadMenu (C++ file-based),
        // transient UI state (thinking, streaming, toolUse)
        /* eslint-disable @typescript-eslint/no-unused-vars */
        const { apiKey: _a, threads: _t, threadMenuOpen: _tm, activeThreadId: _at,
                isThinking: _th, isStreaming: _st, thinkingText: _tt,
                toolUseLabel: _tl, ...rest } = state;
        /* eslint-enable @typescript-eslint/no-unused-vars */
        return rest;
      },
      onRehydrateStorage: () => () => onStoreHydrated(),
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
      onRehydrateStorage: () => () => onStoreHydrated(),
    },
  ),
);

// --- C++ → JS state listeners (partial updates from engine) ---

addStateListener(TransportStateID, (data: unknown) => {
  const partialState = data as Partial<TransportState>;
  useTransportStore.setState((prev) => ({
    ...prev,
    ...partialState,
  }));
});

addStateListener(MixerStateID, (data: unknown) => {
  const partialState = data as Partial<MixerState>;
  useMixerStore.setState((prev) => ({
    ...prev,
    ...partialState,
  }));
});

addStateListener(ChatStateID, (data: unknown) => {
  const partialState = data as Partial<ChatState>;
  useChatStore.setState((prev) => ({
    ...prev,
    ...partialState,
  }));
});

addStateListener(LyriaStateID, (data: unknown) => {
  const partialState = data as Partial<LyriaState>;
  useLyriaStore.setState((prev) => ({
    ...prev,
    ...partialState,
  }));
});

// Transport position is now part of the batched rtFrame event (see meters.ts)

// --- Track notes updates (from BirdLoader after .bird file load) ---
const SECTION_COLORS = [
  'bg-blue-500/5', 'bg-purple-500/5', 'bg-emerald-500/5', 'bg-amber-500/5',
  'bg-rose-500/5', 'bg-cyan-500/5', 'bg-indigo-500/5', 'bg-teal-500/5',
];

function processTrackNotes(data: string | object) {
  // Handle both string (from initial getTrackState) and pre-parsed object (from event listener)
  const raw = typeof data === 'string' ? JSON.parse(data) : data;

  // Support both old array format and new object format
  const trackData = Array.isArray(raw) ? raw : raw.tracks;
  const sectionsData = Array.isArray(raw) ? [] : (raw.sections ?? []);
  const totalBars = Array.isArray(raw) ? 1 : (raw.totalBars ?? 1);
  const keySignature = Array.isArray(raw) ? null : (raw.keySignature ?? null);
  const scale = Array.isArray(raw) ? null : (raw.scale ?? null);
  const bpm = Array.isArray(raw) ? null : (raw.bpm ?? null);

  const TRACK_COLORS = ['#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4', '#FFEAA7', '#DDA0DD', '#98D8C8', '#F7DC6F'];
  const emptySlot = { pluginId: null, pluginName: null, bypassed: false };
  
  const existingTracks = useMixerStore.getState().tracks || [];

  // Update transport state from bird file data in a single batch
  // (avoids triggering persist middleware multiple times during load)
  const transportUpdate: Record<string, unknown> = {};
  if (keySignature != null) transportUpdate.keySignature = keySignature;
  else transportUpdate.keySignature = null;
  if (scale != null) transportUpdate.scale = scale;
  else transportUpdate.scale = null;
  if (bpm != null && bpm > 0) transportUpdate.bpm = bpm;
  useTransportStore.setState(transportUpdate);

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const tracks = trackData.map((t: any, i: number) => {
    const existing = existingTracks.find(x => x.name === t.name);

    return {
      id: t.id,
      name: t.name,
      type: (() => {
        const raw = t.trackType ?? existing?.type ?? 'midi';
        if (raw === 'gen-midi') return 'midi' as TrackType;
        if (raw === 'gen-audio') return 'audio' as TrackType;
        return raw as TrackType;
      })(),
      color: existing ? existing.color : TRACK_COLORS[i % TRACK_COLORS.length],
      muted: t.muted ?? existing?.muted ?? false,
      solo: t.solo ?? existing?.solo ?? false,
      volume: t.volume ?? existing?.volume ?? 80,
      pan: t.pan ?? existing?.pan ?? 0,
      instrument: t.plugin
        ? { pluginId: t.plugin.pluginId, pluginName: t.plugin.pluginName, bypassed: existing?.instrument?.bypassed ?? false }
        : emptySlot,
      fx: t.fx
        ? { pluginId: t.fx.pluginId, pluginName: t.fx.pluginName, bypassed: existing?.fx?.bypassed ?? false }
        : emptySlot,
      channelStrip: t.channelStrip
        ? { pluginId: t.channelStrip.pluginId, pluginName: t.channelStrip.pluginName, bypassed: existing?.channelStrip?.bypassed ?? false }
        : emptySlot,
      notes: t.notes,
      loopLengthBeats: t.loopLengthBeats ?? 0,
      isReturn: t.isReturn ?? false,
      isMaster: t.isMaster ?? false,
      sends: t.sends ?? existing?.sends ?? [0, 0, 0, 0],
      sidechainTrackId: existing ? (existing.sidechainTrackId ?? null) : null,
      sidechainSensitivity: existing ? (existing.sidechainSensitivity ?? 0.6) : 0.6,
    };
  });

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
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

addStateListener('trackState', (data: unknown) => {
  // Defer so JUCE's evaluateJavascript returns immediately — avoids deadlock
  // when Zustand persist calls updateState back into C++.
  const captured = data;
  setTimeout(() => {
    try {
      processTrackNotes(captured as string | object);
    } catch (e) {
      console.error('[trackState] Failed to parse:', e);
    }
  }, 0);
});

// Lightweight note update from MIDI editing (replaces expensive full trackState for note edits)
addStateListener('notesChanged', (data: unknown) => {
  // Defer so evaluateJavascript returns immediately — avoids deadlock
  // when setState triggers persist → updateState native call.
  const captured = data;
  setTimeout(() => {
    try {
      const raw = typeof captured === 'string' ? JSON.parse(captured) : captured;
      const { trackId, notes, loopLengthBeats } = raw as { trackId: number; notes: NoteData[]; loopLengthBeats?: number };
      if (loopLengthBeats !== undefined) {
        useMixerStore.setState((s) => ({
          tracks: s.tracks.map((t) => (t.id === trackId ? { ...t, notes, loopLengthBeats } : t)),
        }));
      } else {
        useMixerStore.getState().setTrackNotes(trackId, notes);
      }
    } catch (e) {
      console.error('[notesChanged] Failed to parse:', e);
    }
  }, 0);
});

// Per-track mixer updates from C++ (reactive ValueTree listeners)
addStateListener('trackMixerUpdate', (data: unknown) => {
  try {
    const raw = typeof data === 'string' ? JSON.parse(data) : data;
    const { trackIndex, volume, pan, muted, solo } = raw as {
      trackIndex: number;
      volume: number;
      pan: number;
      muted: boolean;
      solo: boolean;
    };
    
    const tracks = useMixerStore.getState().tracks;
    if (trackIndex >= 0 && trackIndex < tracks.length) {
      const prev = tracks[trackIndex];
      const changes: string[] = [];
      if (prev.volume !== volume) changes.push(`vol:${prev.volume}→${volume}`);
      if (prev.pan !== pan) changes.push(`pan:${prev.pan}→${pan}`);
      if (prev.muted !== muted) changes.push(`mute:${prev.muted}→${muted}`);
      if (prev.solo !== solo) changes.push(`solo:${prev.solo}→${solo}`);
      if (changes.length > 0) {
        console.log(`[trackMixerUpdate] track[${trackIndex}] '${prev.name}': ${changes.join(', ')}`);
      }
      useMixerStore.setState({
        tracks: tracks.map((t, i) =>
          i === trackIndex ? { ...t, volume, pan, muted, solo } : t
        ),
      });
    }
  } catch (e) {
    console.error('[trackMixerUpdate] Failed:', e);
  }
});

// --- C++ debug logs forwarded to browser console ---
addStateListener('cppLog', (data: unknown) => {
  const d = data as { message: string };
  if (d?.message) console.log(d.message);
});

// --- Audio dropout detector events ---
addStateListener('dropoutDetected', (data: unknown) => {
  const d = data as { timingGaps: number; worstGapMs: number; expectedMs: number; xruns: number; message: string };
  console.warn(`🔴 AUDIO DROPOUT: ${d.timingGaps} gap(s), worst=${d.worstGapMs?.toFixed(1)}ms (expected=${d.expectedMs?.toFixed(1)}ms), xruns=${d.xruns}`);
});

// --- Fetch available plugins on startup (track state is pushed by C++ via chunked events) ---
if (typeof window !== 'undefined' && window.__JUCE__) {
  import('@/lib').then(() => {
    setTimeout(() => {
      useMixerStore.getState().fetchAvailablePlugins();
    }, 500);
  });
}
