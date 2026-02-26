import type { StateStorage } from 'zustand/middleware';
import { Juce, isPlugin } from '@/lib';
import { isSliderDragging } from '@/data/sliderDrag';

// --- Debug log (circular buffer, max 200 entries) ---
export interface BridgeLogEntry {
  timestamp: number;
  direction: '→C++' | '←C++' | 'local';
  method: string;
  storeName: string;
  payload?: string;
}

const MAX_LOG = 200;
export const bridgeLog: BridgeLogEntry[] = [];
const logListeners: Set<() => void> = new Set();

function pushLog(entry: Omit<BridgeLogEntry, 'timestamp'>) {
  bridgeLog.push({ ...entry, timestamp: Date.now() });
  if (bridgeLog.length > MAX_LOG) bridgeLog.shift();
  logListeners.forEach((fn) => fn());
}

/** Subscribe to bridge log changes (returns unsubscribe) */
export function onBridgeLog(fn: () => void) {
  logListeners.add(fn);
  return () => { logListeners.delete(fn); };
}

export function clearBridgeLog() {
  bridgeLog.length = 0;
  logListeners.forEach((fn) => fn());
}

// --- Native functions for state persistence ---
const loadState = isPlugin ? Juce.getNativeFunction('loadState') : () => Promise.resolve(null);
const updateState = isPlugin ? Juce.getNativeFunction('updateState') : () => Promise.resolve(null);
const resetState = isPlugin ? Juce.getNativeFunction('resetState') : () => Promise.resolve(null);

// --- Zustand StateStorage that routes to C++ ---
export const juceBridge: StateStorage = {
  setItem: (name: string, value: string) => {
    // During slider drags, suppress mixer persist — audio is handled by setMixerParamRT
    if (name === 'songbird-mixer' && isSliderDragging()) return;
    pushLog({ direction: isPlugin ? '→C++' : 'local', method: 'updateState', storeName: name, payload: value });
    updateState(name, value);
    return true;
  },
  getItem: async (name: string) => {
    pushLog({ direction: isPlugin ? '→C++' : 'local', method: 'loadState', storeName: name });
    const value = await loadState(name);
    if (!value || value === '{"state":null}') {
      return JSON.stringify({ state: { initialized: false } });
    }
    return value as string;
  },
  removeItem: (name: string) => {
    pushLog({ direction: isPlugin ? '→C++' : 'local', method: 'resetState', storeName: name });
    resetState(name);
    return true;
  },
};

// --- Event listener bridge (C++ → JS) ---
export function addStateListener(event: string, callback: (data: unknown) => void) {
  if (!isPlugin) return () => {};
  const jsonCallback = (data: string) => {
    pushLog({ direction: '←C++', method: 'event', storeName: event, payload: data });
    callback(JSON.parse(data));
  };
  window.__JUCE__!.backend.addEventListener(event, jsonCallback);
  return () => window.__JUCE__!.backend.removeEventListener(event, jsonCallback);
}

// --- Direct native function helper ---
export function nativeFunction(name: string) {
  return isPlugin ? Juce.getNativeFunction(name) : () => Promise.resolve(null);
}
