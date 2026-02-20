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
  /** Stereo analysis */
  stereoWidth: number;        // 0–1 (mono → wide)
  phaseCorrelation: number;   // -1 → +1 (out of phase → mono)
  /** Spectrum Analysis */
  spectrum: number[];         // 0-1 values mapped roughly to dB
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
}));

// Listen for high-frequency level updates from C++
addStateListener('audioLevels', (data: unknown) => {
  try {
    const raw: number[][] = typeof data === 'string' ? JSON.parse(data) : data as number[][];
    if (!raw || !raw.length) return;

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

let _debugFrame = 0;
// Listen for stereo analysis data from C++
addStateListener('stereoAnalysis', (data: unknown) => {
  try {
    const d = typeof data === 'string' ? JSON.parse(data) : data as { width: number; correlation: number; spectrum?: number[] };
    useMeterStore.setState({
      stereoWidth: d.width,
      phaseCorrelation: d.correlation,
      spectrum: d.spectrum || useMeterStore.getState().spectrum,
    });
    // Debug: log every ~30 events (~1 second at 30Hz)
    if (++_debugFrame % 30 === 0) {
      const s = d.spectrum ?? [];
      console.log('[stereoAnalysis] width:', d.width.toFixed(3),
        '| correlation:', d.correlation.toFixed(3),
        '| spectrum[0..7]:', s.slice(0, 8).map((v: number) => v.toFixed(3)).join(', '));
    }
  } catch {
    // ignore
  }
});
