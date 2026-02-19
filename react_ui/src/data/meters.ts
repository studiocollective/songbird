import { create } from 'zustand';
import { addStateListener } from '@/data/bridge';

/** Per-channel stereo level in dB, converted to 0-100 for display */
export interface ChannelLevel {
  left: number;   // 0–100
  right: number;  // 0–100
}

interface MeterState {
  /** Indexed by track position (0-based). Last entry = master. */
  levels: ChannelLevel[];
  master: ChannelLevel;
}

/** Convert dB (-100…0) to a 0–100 display percentage */
function dbToPercent(db: number): number {
  if (db <= -60) return 0;
  if (db >= 0) return 100;
  // Perceptual curve: map -60..0 → 0..100
  return Math.round(((db + 60) / 60) * 100);
}

export const useMeterStore = create<MeterState>(() => ({
  levels: [],
  master: { left: 0, right: 0 },
}));

// Listen for high-frequency level updates from C++
// addStateListener already JSON.parses the data, so we receive the array directly
addStateListener('audioLevels', (data: unknown) => {
  try {
    // data is already parsed — it's either:
    // 1. A string (raw JSON) if emitEvent sends a string that JUCE doesn't auto-parse
    // 2. An array of [dBL, dBR] pairs if already parsed by the bridge
    const raw: number[][] = typeof data === 'string' ? JSON.parse(data) : data as number[][];
    if (!raw || !raw.length) return;

    // Last entry is the master
    const masterRaw = raw[raw.length - 1];
    const trackLevels = raw.slice(0, -1);

    const levels: ChannelLevel[] = trackLevels.map(([l, r]) => ({
      left: dbToPercent(l),
      right: dbToPercent(r),
    }));

    const master: ChannelLevel = {
      left: dbToPercent(masterRaw[0]),
      right: dbToPercent(masterRaw[1]),
    };

    useMeterStore.setState({ levels, master });
  } catch {
    // ignore parse errors on high-frequency data
  }
});
