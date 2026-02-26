import { useState, useEffect, useRef } from 'react';
import { Transport } from '@/components/Transport';
import { ArrangementView } from '@/components/ArrangementView';
import { MixerPanel } from '@/components/MixerPanel';
import { ChatPanel } from '@/components/ChatPanel';
import { HistoryPanel } from '@/components/HistoryPanel';
import { DebugPanel } from '@/components/DebugPanel';
import { ExportProgressModal } from '@/components/organisms/ExportProgressModal';
import { LoadingScreen } from '@/components/organisms/LoadingScreen';
import { Juce, isPlugin } from '@/lib';
import { addStateListener } from '@/data/bridge';

const setZoom = isPlugin ? Juce.getNativeFunction('setZoom') : null;
const uiReady = isPlugin ? Juce.getNativeFunction('uiReady') : null;

interface ProgressPayload {
  message: string;
  progress: number;
}

function App() {
  const zoomRef = useRef(1.0);
  const [engineReady, setEngineReady] = useState(false);
  const [loadingMsg, setLoadingMsg] = useState('Initializing workspace...');
  const [loadingProgress, setLoadingProgress] = useState(0);

  useEffect(() => {
    const undo = isPlugin ? Juce.getNativeFunction('undo') : null;
    const redo = isPlugin ? Juce.getNativeFunction('redo') : null;

    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.metaKey || e.ctrlKey) {
        // --- Undo / Redo ---
        if (e.key === 'z' && !e.shiftKey) {
          e.preventDefault();
          undo?.();
          return;
        }
        if ((e.key === 'z' && e.shiftKey) || e.key === 'y') {
          e.preventDefault();
          redo?.();
          return;
        }

        // --- Zoom ---
        let newZoom = zoomRef.current;
        if (e.key === '=' || e.key === '+') {
          e.preventDefault();
          newZoom = Math.min(zoomRef.current + 0.1, 3);
        } else if (e.key === '-') {
          e.preventDefault();
          newZoom = Math.max(zoomRef.current - 0.1, 0.3);
        } else if (e.key === '0') {
          e.preventDefault();
          newZoom = 1;
        } else {
          return;
        }
        zoomRef.current = newZoom;
        setZoom?.(newZoom);
      }
    };

    window.addEventListener('keydown', handleKeyDown);

    // Listen for C++ loading progress
    const unsubProgress = addStateListener('loadingProgress', (data: unknown) => {
      const payload = data as ProgressPayload;
      if (payload.message === 'done') {
        setEngineReady(true);
      } else {
        setLoadingMsg(payload.message);
        setLoadingProgress(payload.progress);
      }
    });

    // Tell C++ the UI is ready so it can start heavy loading and sending progress events
    setTimeout(() => uiReady?.(), 100);

    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      unsubProgress();
    };
  }, []);


  if (!engineReady) {
    return <LoadingScreen message={loadingMsg} progress={loadingProgress} />;
  }

  return (
    <div className={shell}>
      <Transport />
      <div className={middle}>
        <ArrangementView />
        <ChatPanel />
      </div>
      <MixerPanel />
      <HistoryPanel />
      <DebugPanel />
      <ExportProgressModal />
    </div>
  );
}

const shell = `h-screen w-screen flex flex-col bg-[hsl(var(--background))] text-[hsl(var(--foreground))] overflow-hidden`;
const middle = `flex-1 flex overflow-hidden`;

export default App;

