import '../debug.css';
import { useState, useEffect, useSyncExternalStore, useCallback } from 'react';
import {
  bridgeLog,
  onBridgeLog,
  clearBridgeLog,
  type BridgeLogEntry,
} from '@/data/bridge';
import {
  useTransportStore,
  useMixerStore,
  useChatStore,
  useLyriaStore,
} from '@/data/store';
import { isPlugin, loadTheme, applyTheme } from '@/lib';
import type { Theme } from '@/lib';
import { cn } from '@/lib/utils';

// ─── Hook: subscribe to bridge log ───
let cachedLogSnapshot: BridgeLogEntry[] = [];
function getLogSnapshot() { return cachedLogSnapshot; }
// Update snapshot only when the log changes
onBridgeLog(() => { cachedLogSnapshot = [...bridgeLog]; });

function useBridgeLog(): BridgeLogEntry[] {
  return useSyncExternalStore(onBridgeLog, getLogSnapshot, () => []);
}

// ─── Collapsible JSON viewer ───
function JsonTree({ label, data }: { label: string; data: unknown }) {
  const [open, setOpen] = useState(false);
  // Strip functions from state for display
  const cleaned =
    typeof data === 'object' && data !== null
      ? Object.fromEntries(
          Object.entries(data as Record<string, unknown>).filter(
            ([, v]) => typeof v !== 'function',
          ),
        )
      : data;

  return (
    <div className={storeCls}>
      <div className={storeHeader} onClick={() => setOpen(!open)}>
        <span className={cn(storeChevron, open && storeChevronOpen)}>▶</span>
        <span className={storeNameCls}>{label}</span>
      </div>
      {open && (
        <pre className={jsonCls}>{JSON.stringify(cleaned, null, 2)}</pre>
      )}
    </div>
  );
}

// ─── Tab: State Inspector ───
function StateTab() {
  const transport = useTransportStore();
  const mixer = useMixerStore();
  const chat = useChatStore();
  const lyria = useLyriaStore();

  return (
    <>
      <JsonTree label="songbird-transport" data={transport} />
      <JsonTree label="songbird-mixer" data={mixer} />
      <JsonTree label="songbird-chat" data={chat} />
      <JsonTree label="songbird-lyria" data={lyria} />
    </>
  );
}

// ─── Tab: Bridge Log ───
function BridgeTab() {
  const log = useBridgeLog();

  const fmtTime = (ts: number) => {
    const d = new Date(ts);
    return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')}.${d.getMilliseconds().toString().padStart(3, '0')}`;
  };

  const dirClass = (dir: string) =>
    dir === '→C++' ? logDirToCpp : dir === '←C++' ? logDirFromCpp : logDirLocal;

  return (
    <>
      <div className="flex justify-between mb-2">
        <span className="text-[#888]">{log.length} entries</span>
        <button className={clearBtn} onClick={clearBridgeLog}>
          Clear
        </button>
      </div>
      {log.length === 0 && (
        <div className={logEmpty}>No bridge events yet</div>
      )}
      <div className="flex flex-col-reverse">
        {log.map((e, i) => (
          <div className={logEntry} key={i}>
            <span className={logTime}>{fmtTime(e.timestamp)}</span>
            <span className={cn(logDir, dirClass(e.direction))}>
              {e.direction}
            </span>
            <span className={logMethod}>{e.method}</span>
            <span className={logStore}>{e.storeName}</span>
          </div>
        ))}
      </div>
    </>
  );
}

// ─── Tab: Visual Debug ───
function VisualTab() {
  const [outlines, setOutlines] = useState(false);
  const [grid, setGrid] = useState(false);
  const [theme, setTheme] = useState<Theme>(loadTheme());

  const toggleOutlines = useCallback(() => {
    setOutlines((v) => {
      document.body.classList.toggle('debug-outlines', !v);
      return !v;
    });
  }, []);

  const toggleGrid = useCallback(() => {
    setGrid((v) => {
      document.body.classList.toggle('debug-grid', !v);
      return !v;
    });
  }, []);

  const changeTheme = useCallback((t: Theme) => {
    setTheme(t);
    applyTheme(t);
  }, []);

  return (
    <>
      <div className={toggleRow}>
        <span className={toggleLabel}>Debug outlines</span>
        <button
          className={cn(toggleSwitch, outlines && toggleSwitchOn)}
          onClick={toggleOutlines}
        />
      </div>
      <div className={toggleRow}>
        <span className={toggleLabel}>Grid overlay</span>
        <button
          className={cn(toggleSwitch, grid && toggleSwitchOn)}
          onClick={toggleGrid}
        />
      </div>
      <div className="mt-4">
        <div className={cn(toggleLabel, 'mb-2')}>
          Theme
        </div>
        <div className={themeGroup}>
          {(['light', 'dark', 'system'] as const).map((t) => (
            <button
              key={t}
              className={cn(themeBtn, theme === t && themeBtnActive)}
              onClick={() => changeTheme(t)}
            >
              {t}
            </button>
          ))}
        </div>
      </div>
    </>
  );
}

// ─── Tab: Info ───
function InfoTab() {
  const [size, setSize] = useState({ w: window.innerWidth, h: window.innerHeight });

  useEffect(() => {
    const handler = () => setSize({ w: window.innerWidth, h: window.innerHeight });
    window.addEventListener('resize', handler);
    return () => window.removeEventListener('resize', handler);
  }, []);

  const rows: [string, string][] = [
    ['isPlugin', String(isPlugin)],
    ['Window', `${size.w} × ${size.h}`],
    ['Pixel ratio', String(window.devicePixelRatio)],
    ['Mode', import.meta.env.MODE],
    ['React', '19'],
    ['User Agent', navigator.userAgent],
  ];

  return (
    <>
      {rows.map(([label, value]) => (
        <div className={infoRow} key={label}>
          <span className={infoLabel}>{label}</span>
          <span className={infoValue} title={value}>
            {value}
          </span>
        </div>
      ))}
    </>
  );
}

// ─── Tabs ───
const TABS = ['State', 'Bridge', 'Visual', 'Info'] as const;
type TabId = (typeof TABS)[number];

// ─── Main Debug Panel ───
export function DebugPanel() {
  const [open, setOpen] = useState(false);
  const [tab, setTab] = useState<TabId>('State');

  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.shiftKey && e.key === 'D') {
        e.preventDefault();
        setOpen((v) => !v);
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, []);

  return (
    <div className={cn(panelCls, open && panelOpen)}>
      <div className={panelHeader}>
        <h2 className="m-0 text-[13px] font-semibold text-white tracking-wide">🛠 Debug</h2>
        <button className={panelClose} onClick={() => setOpen(false)}>
          ESC
        </button>
      </div>
      <div className={tabBar}>
        {TABS.map((t) => (
          <button
            key={t}
            className={cn(tabCls, tab === t && tabActive)}
            onClick={() => setTab(t)}
          >
            {t}
          </button>
        ))}
      </div>
      <div className={contentCls}>
        {tab === 'State' && <StateTab />}
        {tab === 'Bridge' && <BridgeTab />}
        {tab === 'Visual' && <VisualTab />}
        {tab === 'Info' && <InfoTab />}
      </div>
    </div>
  );
}

// ═══════════════════════════════════════
// Tailwind class constants
// ═══════════════════════════════════════

// --- Panel shell ---
const panelCls = `
  fixed top-0 right-0 w-[420px] h-screen z-[99999]
  flex flex-col
  bg-[rgba(15,15,20,0.92)] backdrop-blur-[16px]
  border-l border-white/[0.08]
  text-[#e0e0e0] font-mono text-[11px]
  translate-x-full transition-transform duration-250 ease-[cubic-bezier(0.4,0,0.2,1)]`;
const panelOpen = `translate-x-0 shadow-[-4px_0_24px_rgba(0,0,0,0.5)]`;

// --- Header ---
const panelHeader = `
  flex items-center justify-between px-3.5 py-2.5
  border-b border-white/[0.08] shrink-0`;
const panelClose = `
  bg-transparent border border-white/10 text-[#888]
  cursor-pointer px-2 py-0.5 rounded text-[11px] font-mono
  transition-all duration-150
  hover:text-white hover:border-white/25 hover:bg-white/5`;

// --- Tabs ---
const tabBar = `flex border-b border-white/[0.08] shrink-0`;
const tabCls = `
  flex-1 py-2 text-center bg-transparent border-none
  border-b-2 border-b-transparent
  text-[#666] cursor-pointer text-[11px] font-mono font-medium
  transition-all duration-150
  hover:text-[#aaa] hover:bg-white/[0.02]`;
const tabActive = `!text-[#6ecfff] !border-b-[#6ecfff]`;

// --- Content ---
const contentCls = `
  flex-1 overflow-y-auto px-3.5 py-3
  [&::-webkit-scrollbar]:w-[5px]
  [&::-webkit-scrollbar-track]:bg-transparent
  [&::-webkit-scrollbar-thumb]:bg-white/10 [&::-webkit-scrollbar-thumb]:rounded`;

// --- Store / JSON tree ---
const storeCls = `mb-3`;
const storeHeader = `flex items-center gap-1.5 cursor-pointer py-1 select-none hover:text-white`;
const storeNameCls = `font-semibold text-[#6ecfff] text-[11px]`;
const storeChevron = `text-[9px] text-[#555] transition-transform duration-150`;
const storeChevronOpen = `rotate-90`;
const jsonCls = `
  bg-black/30 border border-white/5 rounded
  px-2.5 py-2 mt-1 overflow-x-auto whitespace-pre
  text-[10px] leading-relaxed text-[#b0b0b0]
  max-h-60 overflow-y-auto`;

// --- Bridge log ---
const logEntry = `flex gap-2 py-1 border-b border-white/[0.03] items-start`;
const logTime = `text-[#555] shrink-0 min-w-[65px]`;
const logDir = `shrink-0 min-w-9 font-semibold`;
const logDirToCpp = `text-[#ff9f43]`;
const logDirFromCpp = `text-[#54e89d]`;
const logDirLocal = `text-[#888]`;
const logMethod = `text-[#c89fff] shrink-0 min-w-20`;
const logStore = `text-[#6ecfff] shrink-0`;
const logEmpty = `text-[#555] text-center py-6`;
const clearBtn = `
  bg-[rgba(255,100,100,0.1)] border border-[rgba(255,100,100,0.2)]
  text-[#ff6b6b] px-3 py-1 rounded cursor-pointer font-mono text-[11px]
  transition-all duration-150
  hover:bg-[rgba(255,100,100,0.2)] hover:border-[rgba(255,100,100,0.4)]`;

// --- Visual tab ---
const toggleRow = `flex items-center justify-between py-2 border-b border-white/[0.04]`;
const toggleLabel = `text-[#ccc]`;
const toggleSwitch = `
  relative w-9 h-5 bg-white/10 rounded-[10px] border-none
  cursor-pointer transition-colors duration-200 p-0
  after:content-[''] after:absolute after:top-0.5 after:left-0.5
  after:w-4 after:h-4 after:bg-white after:rounded-full
  after:transition-transform after:duration-200`;
const toggleSwitchOn = `!bg-[#6ecfff] after:translate-x-4`;

// --- Theme selector ---
const themeGroup = `flex gap-1.5 mt-1`;
const themeBtn = `
  px-3 py-1.5 border border-white/10 rounded
  bg-white/[0.03] text-[#888] cursor-pointer font-mono text-[11px]
  transition-all duration-150
  hover:border-white/20 hover:text-[#ccc]`;
const themeBtnActive = `!bg-[rgba(110,207,255,0.15)] !border-[#6ecfff] !text-[#6ecfff]`;

// --- Info tab ---
const infoRow = `flex justify-between py-1.5 border-b border-white/[0.04]`;
const infoLabel = `text-[#888]`;
const infoValue = `text-[#ccc] text-right max-w-[260px] overflow-hidden text-ellipsis whitespace-nowrap`;
