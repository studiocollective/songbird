import { useState, useEffect, useRef, useCallback } from 'react';
import { cn } from '@/lib/utils';
import { Juce, isPlugin } from '@/lib';
import { loadTheme, applyTheme, type Theme } from '@/lib/theme';
import { useMixerStore } from '@/data/store';

/* ------------------------------------------------------------------ */
/*  Native function helpers                                           */
/* ------------------------------------------------------------------ */
const listMidiInputs = isPlugin ? Juce.getNativeFunction('listMidiInputs') : null;
const getAudioDeviceInfo = isPlugin ? Juce.getNativeFunction('getAudioDeviceInfo') : null;
const setAudioBufferSize = isPlugin ? Juce.getNativeFunction('setAudioBufferSize') : null;
const setAudioOutputDevice = isPlugin ? Juce.getNativeFunction('setAudioOutputDevice') : null;
const setAudioInputDevice = isPlugin ? Juce.getNativeFunction('setAudioInputDevice') : null;
const setAudioSampleRate = isPlugin ? Juce.getNativeFunction('setAudioSampleRate') : null;

/* ------------------------------------------------------------------ */
/*  Types                                                             */
/* ------------------------------------------------------------------ */
interface AudioDeviceInfo {
  deviceName: string;
  deviceType: string;
  sampleRate: number;
  bufferSize: number;
  inputLatency: number;
  outputLatency: number;
  availableBufferSizes: number[];
  availableSampleRates: number[];
  availableOutputDevices: string[];
  availableInputDevices: string[];
  inputDeviceName: string;
  outputDeviceName: string;
  inputChannels: string[];
  outputChannels: string[];
}

type SettingsTab = 'audio' | 'midi' | 'mixer' | 'visual';

const tabs: { id: SettingsTab; label: string; icon: React.ReactNode }[] = [
  { id: 'audio', label: 'Audio', icon: <AudioIcon /> },
  { id: 'midi', label: 'MIDI', icon: <MidiIcon /> },
  { id: 'mixer', label: 'Mixer', icon: <MixerIcon /> },
  { id: 'visual', label: 'Visual', icon: <ThemeIcon /> },
];

/* ------------------------------------------------------------------ */
/*  Component                                                         */
/* ------------------------------------------------------------------ */
export function SettingsPanel({ open, onClose }: { open: boolean; onClose: () => void }) {
  const [theme, setTheme] = useState<Theme>(loadTheme());
  const [midiDevices, setMidiDevices] = useState<string[]>([]);
  const [audioInfo, setAudioInfo] = useState<AudioDeviceInfo | null>(null);
  const [activeTab, setActiveTab] = useState<SettingsTab>('audio');
  const backdropRef = useRef<HTMLDivElement>(null);

  /* Fetch data when opened */
  const fetchData = useCallback(async () => {
    try {
      if (listMidiInputs) {
        const raw = await listMidiInputs();
        const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
        setMidiDevices(Array.isArray(parsed) ? parsed : []);
      }
    } catch { setMidiDevices([]); }

    try {
      if (getAudioDeviceInfo) {
        const raw = await getAudioDeviceInfo();
        const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
        setAudioInfo(parsed);
      }
    } catch { setAudioInfo(null); }
  }, []);

  useEffect(() => {
    if (open) {
      const controller = new AbortController();
      (async () => { await fetchData(); })();
      return () => controller.abort();
    }
  }, [open, fetchData]);

  /* Close on backdrop click */
  const handleBackdrop = (e: React.MouseEvent) => {
    if (e.target === backdropRef.current) onClose();
  };

  /* Theme change */
  const handleTheme = (t: Theme) => {
    setTheme(t);
    applyTheme(t);
  };

  /* Buffer size change */
  const handleBufferSize = async (size: number) => {
    if (!setAudioBufferSize) return;
    await setAudioBufferSize(size);
    fetchData();
  };

  /* Audio output device change */
  const handleOutputDeviceChange = async (name: string) => {
    if (!setAudioOutputDevice) return;
    await setAudioOutputDevice(name);
    fetchData();
  };

  /* Audio input device change */
  const handleInputDeviceChange = async (name: string) => {
    if (!setAudioInputDevice) return;
    await setAudioInputDevice(name);
    fetchData();
  };

  /* Sample rate change */
  const handleSampleRate = async (rate: number) => {
    if (!setAudioSampleRate) return;
    await setAudioSampleRate(rate);
    fetchData();
  };

  /* Latency display */
  const hasInput = audioInfo?.inputDeviceName ? true : false;
  const latencySamples = audioInfo
    ? (hasInput ? audioInfo.inputLatency : 0) + audioInfo.outputLatency + audioInfo.bufferSize
    : 0;
  const latencyMs = audioInfo && audioInfo.sampleRate > 0
    ? ((latencySamples / audioInfo.sampleRate) * 1000).toFixed(1)
    : '—';
  const latencyLabel = hasInput ? 'Round-trip Latency' : 'Output Latency';

  if (!open) return null;

  return (
    <div ref={backdropRef} onClick={handleBackdrop} className={backdrop}>
      <div className={panel}>
        {/* Body: sidebar tabs + content */}
        <div className={body}>
          {/* Left sidebar tabs */}
          <nav className={sidebar}>
            <div className={sidebarHeader}>
              <h2 className={headerTitle}>Settings</h2>
            </div>
            {tabs.map((tab) => (
              <button
                key={tab.id}
                className={cn(sidebarBtn, activeTab === tab.id && sidebarBtnActive)}
                onClick={() => setActiveTab(tab.id)}
                title={tab.label}
              >
                {tab.icon}
                <span>{tab.label}</span>
              </button>
            ))}
          </nav>

          {/* Right content pane */}
          <div className={content}>
            {/* Close button */}
            <button onClick={onClose} className={closeBtn} title="Close">
              <svg width="12" height="12" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round">
                <path d="M1 1l12 12M13 1L1 13" />
              </svg>
            </button>
            {/* ===== Audio ===== */}
            {activeTab === 'audio' && (
              <section className={section}>
                <h3 className={sectionTitle}>Audio Interface</h3>
                {audioInfo ? (
                  <div className={fieldGrid}>
                    <div className={fieldRow}>
                      <span className={fieldLabel}>Output</span>
                      {audioInfo.availableOutputDevices?.length > 1 ? (
                        <select
                          value={audioInfo.outputDeviceName}
                          onChange={(e) => handleOutputDeviceChange(e.target.value)}
                          className={selectInput}
                        >
                          {audioInfo.availableOutputDevices.map((name) => (
                            <option key={name} value={name}>{name}</option>
                          ))}
                        </select>
                      ) : (
                        <span className={fieldValue}>{audioInfo.outputDeviceName || '—'}</span>
                      )}
                    </div>

                    <div className={fieldRow}>
                      <span className={fieldLabel}>Input</span>
                      <select
                        value={audioInfo.inputDeviceName || ''}
                        onChange={(e) => handleInputDeviceChange(e.target.value)}
                        className={selectInput}
                      >
                        <option value="">None</option>
                        {audioInfo.availableInputDevices?.map((name) => (
                          <option key={name} value={name}>{name}</option>
                        ))}
                      </select>
                    </div>

                    <div className={fieldRow}>
                      <span className={fieldLabel}>Sample Rate</span>
                      {audioInfo.availableSampleRates?.length > 1 ? (
                        <select
                          value={audioInfo.sampleRate}
                          onChange={(e) => handleSampleRate(Number(e.target.value))}
                          className={selectInput}
                        >
                          {audioInfo.availableSampleRates.map((sr) => (
                            <option key={sr} value={sr}>{(sr / 1000).toFixed(1)} kHz</option>
                          ))}
                        </select>
                      ) : (
                        <span className={fieldValue}>{audioInfo.sampleRate ? `${(audioInfo.sampleRate / 1000).toFixed(1)} kHz` : '—'}</span>
                      )}
                    </div>

                    <div className={fieldRow}>
                      <span className={fieldLabel}>Buffer Size</span>
                      <select
                        value={audioInfo.bufferSize}
                        onChange={(e) => handleBufferSize(Number(e.target.value))}
                        className={selectInput}
                      >
                        {audioInfo.availableBufferSizes?.map((sz) => (
                          <option key={sz} value={sz}>{sz} samples</option>
                        ))}
                      </select>
                    </div>

                    <div className={fieldRow}>
                      <span className={fieldLabel}>Inputs</span>
                      <span className={fieldValue}>{audioInfo.inputChannels?.length ?? 0} ch</span>
                    </div>

                    <div className={fieldRow}>
                      <span className={fieldLabel}>Outputs</span>
                      <span className={fieldValue}>{audioInfo.outputChannels?.length ?? 0} ch</span>
                    </div>

                    <div className={`${fieldRow} ${latencyRow}`}>
                      <span className={fieldLabel}>{latencyLabel}</span>
                      <span className={fieldValueAccent}>
                        {latencyMs} ms
                        <span className={fieldValueDim}> ({latencySamples} samples)</span>
                      </span>
                    </div>
                  </div>
                ) : (
                  <p className={emptyText}>No audio device detected</p>
                )}
              </section>
            )}

            {/* ===== MIDI ===== */}
            {activeTab === 'midi' && (
              <section className={section}>
                <h3 className={sectionTitle}>MIDI Devices</h3>
                {midiDevices.length > 0 ? (
                  <ul className={deviceList}>
                    {midiDevices.map((name, i) => (
                      <li key={i} className={deviceItem}>
                        <span className={deviceDot} />
                        {name}
                      </li>
                    ))}
                  </ul>
                ) : (
                  <p className={emptyText}>No MIDI devices connected</p>
                )}
              </section>
            )}

            {/* ===== Mixer ===== */}
            {activeTab === 'mixer' && (
              <section className={section}>
                <h3 className={sectionTitle}>Mixer</h3>
                <div className={fieldGrid}>
                  <div className={fieldRow}>
                    <span className={fieldLabel}>Default Channel Strip</span>
                    <DefaultChannelStripSelector />
                  </div>
                </div>
              </section>
            )}

            {/* ===== Visual ===== */}
            {activeTab === 'visual' && (
              <section className={section}>
                <h3 className={sectionTitle}>Appearance</h3>
                <div className={segmented}>
                  {(['system', 'dark', 'light'] as Theme[]).map((t) => (
                    <button
                      key={t}
                      onClick={() => handleTheme(t)}
                      className={cn(segmentBtn, theme === t && segmentBtnActive)}
                    >
                      {t === 'system' ? 'Auto' : t.charAt(0).toUpperCase() + t.slice(1)}
                    </button>
                  ))}
                </div>
              </section>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

/* ------------------------------------------------------------------ */
/*  Default Channel Strip Selector                                    */
/* ------------------------------------------------------------------ */
function DefaultChannelStripSelector() {
  const availableStrips = useMixerStore((s) => s.availableChannelStrips);
  const defaultStrip = useMixerStore((s) => s.defaultChannelStrip);

  return (
    <select
      value={defaultStrip?.id ?? ''}
      onChange={(e) => {
        const selected = availableStrips.find(s => s.id === e.target.value);
        useMixerStore.getState().setDefaultChannelStrip(
          selected ? { id: selected.id, name: selected.name } : null
        );
      }}
      className={selectInput}
    >
      <option value="">First Available</option>
      {availableStrips.map((s) => (
        <option key={s.id} value={s.id}>{s.name}</option>
      ))}
    </select>
  );
}

/* ------------------------------------------------------------------ */
/*  Icons                                                             */
/* ------------------------------------------------------------------ */
function ThemeIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" className="shrink-0">
      <circle cx="8" cy="8" r="3" />
      <path d="M8 1v2M8 13v2M1 8h2M13 8h2M3.05 3.05l1.41 1.41M11.54 11.54l1.41 1.41M3.05 12.95l1.41-1.41M11.54 4.46l1.41-1.41" />
    </svg>
  );
}

function AudioIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" className="shrink-0">
      <path d="M2 6v4M5 4v8M8 2v12M11 4v8M14 6v4" />
    </svg>
  );
}

function MidiIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" className="shrink-0">
      <circle cx="8" cy="8" r="6" />
      <circle cx="6" cy="7" r="1" fill="currentColor" stroke="none" />
      <circle cx="10" cy="7" r="1" fill="currentColor" stroke="none" />
      <circle cx="8" cy="10.5" r="1" fill="currentColor" stroke="none" />
    </svg>
  );
}

function MixerIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" className="shrink-0">
      <line x1="4" y1="2" x2="4" y2="14" />
      <line x1="8" y1="2" x2="8" y2="14" />
      <line x1="12" y1="2" x2="12" y2="14" />
      <circle cx="4" cy="10" r="1.5" fill="currentColor" />
      <circle cx="8" cy="6" r="1.5" fill="currentColor" />
      <circle cx="12" cy="8" r="1.5" fill="currentColor" />
    </svg>
  );
}

/* ------------------------------------------------------------------ */
/*  Styles (Tailwind utility strings)                                 */
/* ------------------------------------------------------------------ */
const backdrop = `
  fixed inset-0 z-[100] flex items-center justify-center
  bg-black/50 backdrop-blur-sm
  animate-in fade-in duration-150`;

const panel = `
  w-[560px] h-[50vh] overflow-hidden
  bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  rounded-xl shadow-2xl
  animate-in zoom-in-95 duration-150
  flex flex-col`;

const sidebarHeader = `
  flex items-center px-3 pt-3 pb-2 mb-1`;

const headerTitle = `text-sm font-semibold text-[hsl(var(--foreground))]`;

const closeBtn = `
  absolute top-3 right-3
  w-6 h-6 rounded-md flex items-center justify-center
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  hover:bg-[hsl(var(--muted))] transition-colors`;

const body = `flex flex-1 min-h-0`;

const sidebar = `
  w-[130px] shrink-0 flex flex-col gap-0.5 p-2
  border-r border-[hsl(var(--border))]
  bg-[hsl(var(--muted))]/10`;

const sidebarBtn = `
  flex items-center gap-2 px-3 py-2 rounded-md text-xs font-medium
  text-[hsl(var(--muted-foreground))] transition-colors
  hover:text-[hsl(var(--foreground))] hover:bg-[hsl(var(--muted))]/50
  cursor-pointer`;

const sidebarBtnActive = `
  bg-[hsl(var(--background))] text-[hsl(var(--foreground))] shadow-sm
  border border-[hsl(var(--border))]
  hover:bg-[hsl(var(--background))]`;

const content = `relative flex-1 p-5 overflow-y-auto flex flex-col gap-5`;

const section = `flex flex-col gap-3`;

const sectionTitle = `
  text-xs font-semibold uppercase tracking-wider
  text-[hsl(var(--muted-foreground))]`;

/* Segmented control for theme */
const segmented = `
  flex rounded-lg bg-[hsl(var(--background))]
  border border-[hsl(var(--border))] p-0.5`;

const segmentBtn = `
  flex-1 px-3 py-1.5 rounded-md text-xs font-medium
  text-[hsl(var(--muted-foreground))] transition-all duration-150
  hover:text-[hsl(var(--foreground))]`;

const segmentBtnActive = `
  bg-[hsl(var(--card))] text-[hsl(var(--foreground))]
  shadow-sm`;

/* Field grid */
const fieldGrid = `flex flex-col gap-0`;

const fieldRow = `
  flex items-center justify-between py-2
  border-b border-[hsl(var(--border)/0.5)]
  last:border-b-0`;

const latencyRow = `
  mt-1 pt-3
  border-t border-[hsl(var(--border))]
  border-b-0`;

const fieldLabel = `text-xs text-[hsl(var(--muted-foreground))]`;

const fieldValue = `text-xs font-mono text-[hsl(var(--foreground))]`;

const fieldValueAccent = `text-xs font-mono text-[hsl(var(--progress))] font-medium`;

const fieldValueDim = `text-[10px] text-[hsl(var(--muted-foreground))] font-normal`;

const selectInput = `
  h-7 px-2 rounded-md text-xs font-mono
  bg-[hsl(var(--background))] border border-[hsl(var(--border))]
  text-[hsl(var(--foreground))]
  focus:outline-none focus:border-[hsl(var(--ring))]
  cursor-pointer`;

/* MIDI device list */
const deviceList = `flex flex-col gap-1`;

const deviceItem = `
  flex items-center gap-2 px-3 py-2 rounded-md
  bg-[hsl(var(--background))] text-xs
  text-[hsl(var(--foreground))]`;

const deviceDot = `w-2 h-2 rounded-full bg-[hsl(var(--progress))] shrink-0`;

const emptyText = `text-xs text-[hsl(var(--muted-foreground))] italic`;
