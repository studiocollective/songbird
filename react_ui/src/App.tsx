import { useEffect, useRef } from 'react';
import { Transport } from '@/components/Transport';
import { ArrangementView } from '@/components/ArrangementView';
import { MixerPanel } from '@/components/MixerPanel';
import { ChatPanel } from '@/components/ChatPanel';
import { DebugPanel } from '@/components/DebugPanel';
import { ExportProgressModal } from '@/components/organisms/ExportProgressModal';
import { Juce, isPlugin } from '@/lib';

const setZoom = isPlugin ? Juce.getNativeFunction('setZoom') : null;

function App() {
  const zoomRef = useRef(1.0);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.metaKey || e.ctrlKey) {
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
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);


  return (
    <div className={shell}>
      <Transport />
      <div className={middle}>
        <ArrangementView />
        <ChatPanel />
      </div>
      <MixerPanel />
      <DebugPanel />
      <ExportProgressModal />
    </div>
  );
}

const shell = `h-screen w-screen flex flex-col bg-[hsl(var(--background))] text-[hsl(var(--foreground))] overflow-hidden`;
const middle = `flex-1 flex overflow-hidden`;

export default App;

