import { create } from 'zustand';
import { addStateListener } from '@/data/bridge';

/** Per-channel stereo level in dB, converted to 0-100 for display */
export interface ChannelLevel {
  left: number;   // 0–100
  right: number;  // 0–100
}

export interface CpuData {
  cpu: number;
  bufferSize: number;
  sampleRate: number;
}

interface MeterState {
  /** Indexed by track position (0-based). Last entry = master. */
  levels: ChannelLevel[];
  master: ChannelLevel;
  /** Stereo analysis */
  stereoWidth: number;        // 0–1 (mono → wide)
  phaseCorrelation: number;   // -1 → +1 (out of phase → mono)
  /** Spectrum Analysis */
  spectrum: number[];         // 0-1 values mapped roughly to dB
  /** Transport position (high-frequency from C++) */
  position: number;
  currentBar: number;
  looping: boolean;
  loopLength: number;
  loopBars: number;
  loopStartBar: number;
  lastPositionUpdate: number;
  /** CPU stats */
  cpuData: CpuData;
}

/** Convert dB (-100…0) to a 0–100 display percentage */
function dbToPercent(db: number): number {
  if (db <= -60) return 0;
  if (db >= 0) return 100;
  return Math.round(((db + 60) / 60) * 100);
}

export const useMeterStore = create<MeterState>(() => ({
  levels: [],
  master: { left: 0, right: 0 },
  stereoWidth: 0,
  phaseCorrelation: 1,
  spectrum: new Array(64).fill(0),
  position: 0,
  currentBar: 1,
  looping: true,
  loopLength: 0,
  loopBars: 0,
  loopStartBar: 0,
  lastPositionUpdate: 0,
  cpuData: { cpu: 0, bufferSize: 0, sampleRate: 0 },
}));

// ─── Mutable real-time buffer (written every frame at 60Hz, NO Zustand notifications) ───
// Direct-DOM consumers subscribe to this for zero-overhead updates.
export interface RtBufferData {
  levels: ChannelLevel[];
  master: ChannelLevel;
  stereoWidth: number;
  phaseCorrelation: number;
  balance: number;
  spectrum: number[];
  position: number;
  currentBar: number;
  looping: boolean;
  loopLength: number;
  loopBars: number;
  loopStartBar: number;
  lastPositionUpdate: number;
  cpuData: CpuData;
}

const rtBuffer: RtBufferData = {
  levels: [],
  master: { left: 0, right: 0 },
  stereoWidth: 0,
  phaseCorrelation: 1,
  balance: 0,
  spectrum: new Array(16).fill(0),
  position: 0,
  currentBar: 1,
  looping: true,
  loopLength: 0,
  loopBars: 0,
  loopStartBar: 0,
  lastPositionUpdate: 0,
  cpuData: { cpu: 0, bufferSize: 0, sampleRate: 0 },
};

/** Get current RT buffer snapshot (no copy, direct reference — read-only by convention) */
export function getRtBuffer(): Readonly<RtBufferData> { return rtBuffer; }

// Lightweight subscriber list for direct-DOM consumers
type RtSubscriber = (buf: Readonly<RtBufferData>) => void;
const rtSubscribers: Set<RtSubscriber> = new Set();

// rAF-driven render loop — updates are synced with browser paint timing.
// C++ data writes to rtBuffer; this loop reads it and drives all DOM updates.
let rafId: number | null = null;
let hasNewData = false;

function rtRenderLoop() {
  if (hasNewData) {
    hasNewData = false;
    for (const fn of rtSubscribers) fn(rtBuffer);
  }
  rafId = requestAnimationFrame(rtRenderLoop);
}

function ensureRafRunning() {
  if (rafId === null) {
    rafId = requestAnimationFrame(rtRenderLoop);
  }
}

/** Mark that new data is available — the rAF loop will pick it up on next paint */
function markNewData() {
  hasNewData = true;
  ensureRafRunning();
}

/**
 * Subscribe to rAF-synced RT data. The callback fires once per browser paint frame.
 * Use this instead of `useMeterStore.subscribe()` for direct-DOM updates.
 * Returns an unsubscribe function.
 */
export function subscribeRtBuffer(fn: RtSubscriber): () => void {
  rtSubscribers.add(fn);
  ensureRafRunning();
  return () => {
    rtSubscribers.delete(fn);
    if (rtSubscribers.size === 0 && rafId !== null) {
      cancelAnimationFrame(rafId);
      rafId = null;
    }
  };
}

// ─── Single batched event from C++ (replaces audioLevels + transportPosition + stereoAnalysis + cpuStats) ───
// We lazily import useTransportStore to also sync transport position there,
// since Playhead components in ArrangementView/MidiEditor read from it via getState().
type TransportSetter = (state: Record<string, unknown>) => void;
let syncTransport: TransportSetter | null = null;

// Throttle Zustand setState to every 3rd frame (~20Hz at 60Hz input)
let frameCounter = 0;
const ZUSTAND_THROTTLE = 3;

addStateListener('rtFrame', (data: unknown) => {
  try {
    const d = typeof data === 'string' ? JSON.parse(data) : data as {
      levels: number[][];
      transport: { position: number; bar: number; looping: boolean; loopLength: number; loopBars: number; loopStartBar: number };
      stereo: { width: number; correlation: number; balance: number; spectrum: number[] };
      cpu: { cpu: number; bufferSize: number; sampleRate: number };
    };

    // Unpack audio levels into the mutable buffer
    const raw = d.levels;
    const masterRaw = raw[raw.length - 1];
    const trackLevels = raw.slice(0, -1);

    const levels: ChannelLevel[] = trackLevels.map(([l, r]: [number, number]) => ({
      left: dbToPercent(l),
      right: dbToPercent(r),
    }));

    const master: ChannelLevel = {
      left: dbToPercent(masterRaw[0]),
      right: dbToPercent(masterRaw[1]),
    };

    const now = performance.now();

    // Write to mutable buffer (zero-cost, no notifications)
    rtBuffer.levels = levels;
    rtBuffer.master = master;
    rtBuffer.stereoWidth = d.stereo.width;
    rtBuffer.phaseCorrelation = d.stereo.correlation;
    rtBuffer.balance = d.stereo.balance;
    rtBuffer.spectrum = d.stereo.spectrum || rtBuffer.spectrum;
    rtBuffer.position = d.transport.position;
    rtBuffer.currentBar = d.transport.bar;
    if (d.transport.looping !== undefined) rtBuffer.looping = d.transport.looping;
    if (d.transport.loopLength !== undefined) rtBuffer.loopLength = d.transport.loopLength;
    if (d.transport.loopBars !== undefined) rtBuffer.loopBars = d.transport.loopBars;
    if (d.transport.loopStartBar !== undefined) rtBuffer.loopStartBar = d.transport.loopStartBar;
    rtBuffer.lastPositionUpdate = now;
    rtBuffer.cpuData = d.cpu;

    // Signal the rAF render loop that new data is available
    markNewData();

    // Throttled Zustand setState for remaining React consumers (~20Hz)
    frameCounter++;
    if (frameCounter >= ZUSTAND_THROTTLE) {
      frameCounter = 0;
      useMeterStore.setState({
        levels,
        master,
        stereoWidth: d.stereo.width,
        phaseCorrelation: d.stereo.correlation,
        spectrum: d.stereo.spectrum || useMeterStore.getState().spectrum,
        position: d.transport.position,
        currentBar: d.transport.bar,
        ...(d.transport.looping !== undefined && { looping: d.transport.looping }),
        ...(d.transport.loopLength !== undefined && { loopLength: d.transport.loopLength }),
        ...(d.transport.loopBars !== undefined && { loopBars: d.transport.loopBars }),
        ...(d.transport.loopStartBar !== undefined && { loopStartBar: d.transport.loopStartBar }),
        lastPositionUpdate: now,
        cpuData: d.cpu,
      });
    }

    // Sync transport position at full 60Hz (playheads use getState(), non-reactive)
    const transportUpdate = {
      position: d.transport.position,
      currentBar: d.transport.bar,
      ...(d.transport.looping !== undefined && { looping: d.transport.looping }),
      ...(d.transport.loopLength !== undefined && { loopLength: d.transport.loopLength }),
      ...(d.transport.loopBars !== undefined && { loopBars: d.transport.loopBars }),
      ...(d.transport.loopStartBar !== undefined && { loopStartBar: d.transport.loopStartBar }),
      lastPositionUpdate: now,
    };

    if (!syncTransport) {
      import('@/data/store').then((mod) => {
        syncTransport = mod.useTransportStore.setState;
        syncTransport!(transportUpdate);
      });
    } else {
      syncTransport(transportUpdate);
    }
  } catch {
    // ignore parse errors on high-frequency data
  }
});

