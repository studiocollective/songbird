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
import './debug.css';

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
    <div className="debug-store">
      <div className="debug-store-header" onClick={() => setOpen(!open)}>
        <span className={`debug-store-chevron ${open ? 'open' : ''}`}>▶</span>
        <span className="debug-store-name">{label}</span>
      </div>
      {open && (
        <pre className="debug-json">{JSON.stringify(cleaned, null, 2)}</pre>
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
    dir === '→C++' ? 'to-cpp' : dir === '←C++' ? 'from-cpp' : 'local-dir';

  return (
    <>
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 8 }}>
        <span style={{ color: '#888' }}>{log.length} entries</span>
        <button className="debug-clear-btn" onClick={clearBridgeLog}>
          Clear
        </button>
      </div>
      {log.length === 0 && (
        <div className="debug-log-empty">No bridge events yet</div>
      )}
      <div style={{ display: 'flex', flexDirection: 'column-reverse' }}>
        {log.map((e, i) => (
          <div className="debug-log-entry" key={i}>
            <span className="debug-log-time">{fmtTime(e.timestamp)}</span>
            <span className={`debug-log-dir ${dirClass(e.direction)}`}>
              {e.direction}
            </span>
            <span className="debug-log-method">{e.method}</span>
            <span className="debug-log-store">{e.storeName}</span>
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
      <div className="debug-toggle-row">
        <span className="debug-toggle-label">Debug outlines</span>
        <button
          className={`debug-toggle-switch ${outlines ? 'on' : ''}`}
          onClick={toggleOutlines}
        />
      </div>
      <div className="debug-toggle-row">
        <span className="debug-toggle-label">Grid overlay</span>
        <button
          className={`debug-toggle-switch ${grid ? 'on' : ''}`}
          onClick={toggleGrid}
        />
      </div>
      <div style={{ marginTop: 16 }}>
        <div className="debug-toggle-label" style={{ marginBottom: 8 }}>
          Theme
        </div>
        <div className="debug-theme-group">
          {(['light', 'dark', 'system'] as const).map((t) => (
            <button
              key={t}
              className={`debug-theme-btn ${theme === t ? 'active' : ''}`}
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
        <div className="debug-info-row" key={label}>
          <span className="debug-info-label">{label}</span>
          <span className="debug-info-value" title={value}>
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
    <div className={`debug-panel ${open ? 'open' : ''}`}>
      <div className="debug-panel-header">
        <h2>🛠 Debug</h2>
        <button className="debug-panel-close" onClick={() => setOpen(false)}>
          ESC
        </button>
      </div>
      <div className="debug-tabs">
        {TABS.map((t) => (
          <button
            key={t}
            className={`debug-tab ${tab === t ? 'active' : ''}`}
            onClick={() => setTab(t)}
          >
            {t}
          </button>
        ))}
      </div>
      <div className="debug-content">
        {tab === 'State' && <StateTab />}
        {tab === 'Bridge' && <BridgeTab />}
        {tab === 'Visual' && <VisualTab />}
        {tab === 'Info' && <InfoTab />}
      </div>
    </div>
  );
}
