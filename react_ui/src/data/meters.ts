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

// ─── Ballistic smoothing engine ───
// C++ sends data at ~30Hz. Smoothing + subscriber notification fires only when
// new data arrives, batched to the next paint via a single rAF.
// Between data frames the WebView is completely IDLE — no canvas draws, no compositor work.

// Smoothing constants (tuned for ~30fps data rate)
const LEVEL_RELEASE = 0.78;     // ~300ms decay at 30fps
const SPECTRUM_RELEASE = 0.68;  // ~200ms decay at 30fps
const STEREO_SMOOTH = 0.72;     // ~250ms smoothing at 30fps
const CPU_SMOOTH = 0.90;        // ~600ms smoothing at 30fps

// The "display buffer" holds smoothed values that subscribers see
const displayBuffer: RtBufferData = {
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

type RtSubscriber = (buf: Readonly<RtBufferData>) => void;
const rtSubscribers: Set<RtSubscriber> = new Set();

let pendingRaf: number | null = null;

function ballisticSmooth(current: number, target: number, release: number): number {
  if (target > current) return target;
  return current * release + target * (1 - release);
}

function applySmoothing() {
  const raw = rtBuffer;

  // Level meters
  while (displayBuffer.levels.length < raw.levels.length)
    displayBuffer.levels.push({ left: 0, right: 0 });
  for (let i = 0; i < raw.levels.length; i++) {
    displayBuffer.levels[i].left = ballisticSmooth(displayBuffer.levels[i].left, raw.levels[i].left, LEVEL_RELEASE);
    displayBuffer.levels[i].right = ballisticSmooth(displayBuffer.levels[i].right, raw.levels[i].right, LEVEL_RELEASE);
  }

  // Master
  displayBuffer.master.left = ballisticSmooth(displayBuffer.master.left, raw.master.left, LEVEL_RELEASE);
  displayBuffer.master.right = ballisticSmooth(displayBuffer.master.right, raw.master.right, LEVEL_RELEASE);

  // Spectrum
  for (let i = 0; i < raw.spectrum.length; i++) {
    if (i >= displayBuffer.spectrum.length) displayBuffer.spectrum.push(0);
    displayBuffer.spectrum[i] = ballisticSmooth(displayBuffer.spectrum[i], raw.spectrum[i], SPECTRUM_RELEASE);
  }

  // Stereo
  displayBuffer.stereoWidth = displayBuffer.stereoWidth * STEREO_SMOOTH + raw.stereoWidth * (1 - STEREO_SMOOTH);
  displayBuffer.phaseCorrelation = displayBuffer.phaseCorrelation * STEREO_SMOOTH + raw.phaseCorrelation * (1 - STEREO_SMOOTH);
  displayBuffer.balance = displayBuffer.balance * STEREO_SMOOTH + raw.balance * (1 - STEREO_SMOOTH);

  // CPU
  displayBuffer.cpuData = {
    cpu: displayBuffer.cpuData.cpu * CPU_SMOOTH + raw.cpuData.cpu * (1 - CPU_SMOOTH),
    bufferSize: raw.cpuData.bufferSize,
    sampleRate: raw.cpuData.sampleRate,
  };

  // Transport — pass through
  displayBuffer.position = raw.position;
  displayBuffer.currentBar = raw.currentBar;
  displayBuffer.looping = raw.looping;
  displayBuffer.loopLength = raw.loopLength;
  displayBuffer.loopBars = raw.loopBars;
  displayBuffer.loopStartBar = raw.loopStartBar;
  displayBuffer.lastPositionUpdate = raw.lastPositionUpdate;

  // Schedule ONE rAF to batch canvas draws with the next browser paint
  if (pendingRaf === null) {
    pendingRaf = requestAnimationFrame(() => {
      pendingRaf = null;
      for (const fn of rtSubscribers) fn(displayBuffer);
    });
  }
}

/** Called when C++ data arrives */
function markNewData() {
  applySmoothing();
}

/**
 * Subscribe to smoothed RT data. Callback fires once per C++ data frame (~30Hz),
 * batched to the next browser paint via a single rAF.
 */
export function subscribeRtBuffer(fn: RtSubscriber): () => void {
  rtSubscribers.add(fn);
  return () => {
    rtSubscribers.delete(fn);
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

