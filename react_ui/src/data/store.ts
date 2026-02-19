import { create } from 'zustand';
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

// --- Transport position updates (high-frequency from C++ audio thread) ---
addStateListener('transportPosition', (data: { position: number; bar: number }) => {
  useTransportStore.setState({
    position: data.position,
    currentBar: data.bar,
  });
});

// --- Lyria status updates (from LyriaPlugin's onStatusChange callback) ---
if (typeof window !== 'undefined') {
  window.addEventListener('lyria-status', ((e: CustomEvent) => {
    const { connected, buffering } = e.detail;
    useLyriaStore.setState({ connected, buffering });
  }) as EventListener);
}

