// --- VST3 Plugin Catalog ---

export type PluginCategory = 'instrument' | 'channel-strip';

export interface PluginInfo {
  id: string;
  name: string;
  vendor: string;
  category: PluginCategory;
}

// Arturia Instruments
export const ARTURIA_INSTRUMENTS: PluginInfo[] = [
  { id: 'arturia.analog-lab-v',  name: 'Analog Lab V',  vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.pigments',      name: 'Pigments',      vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.mini-v',        name: 'Mini V',        vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.prophet-v',     name: 'Prophet V',     vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.jup-8-v',       name: 'Jup-8 V',       vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.cs-80-v',       name: 'CS-80 V',       vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.sem-v',         name: 'SEM V',          vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.wurli-v',       name: 'Wurli V',        vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.piano-v',       name: 'Piano V',        vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.dx7-v',         name: 'DX7 V',          vendor: 'Arturia', category: 'instrument' },
  { id: 'arturia.b-3-v',         name: 'B-3 V',          vendor: 'Arturia', category: 'instrument' },
];

// Channel Strips
export const CHANNEL_STRIPS: PluginInfo[] = [
  { id: 'softube.console-1',     name: 'Console 1',      vendor: 'Softube', category: 'channel-strip' },
];

// Combined lookups
export const ALL_INSTRUMENTS = ARTURIA_INSTRUMENTS;
export const ALL_CHANNEL_STRIPS = CHANNEL_STRIPS;

export function getPluginById(id: string): PluginInfo | undefined {
  return [...ALL_INSTRUMENTS, ...ALL_CHANNEL_STRIPS].find((p) => p.id === id);
}
