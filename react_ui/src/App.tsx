import { useState, useEffect, useRef, useCallback } from 'react';
import {
  Transport,
  ArrangementView,
  MixerPanel,
  MidiEditor,
  SampleEditor,
  ChatPanel,
  HistoryPanel,
  BirdFilePanel,
  DebugPanel,
  SettingsPanel,
} from '@/components/panels';
import { ExportProgressModal } from '@/components/organisms/ExportProgressModal';
import { LoadingScreen } from '@/components/organisms/LoadingScreen';
import { GenerateModal } from '@/components/organisms/GenerateModal';
import { Juce, isPlugin } from '@/lib';
import { addStateListener } from '@/data/bridge';
import { useChatStore, useMixerStore, useTransportStore } from '@/data/store';

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
  const rightPanel = useChatStore((s) => s.rightPanel);
  const midiEditorOpen = useMixerStore((s) => s.midiEditorOpen);
  const sampleEditorOpen = useMixerStore((s) => s.sampleEditorOpen);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const openSettings = useCallback(() => setSettingsOpen(true), []);

  useEffect(() => {
    const undo = isPlugin ? Juce.getNativeFunction('undo') : null;
    const redo = isPlugin ? Juce.getNativeFunction('redo') : null;

    const sendKeyboardMidi = isPlugin ? Juce.getNativeFunction('sendKeyboardMidi') : null;

    const KEYBOARD_NOTE_MAP: Record<string, number> = {
      // Lower row: C4-B4
      'a': 60, 'w': 61, 's': 62, 'e': 63, 'd': 64,
      'f': 65, 't': 66, 'g': 67, 'y': 68, 'h': 69,
      'u': 70, 'j': 71,
      // Upper octave: C5-B5
      'k': 72, 'o': 73, 'l': 74, 'p': 75, ';': 76,
    };

    const handleKeyDown = (e: KeyboardEvent) => {
      // --- Keyboard MIDI mode: route keys as MIDI notes instead of shortcuts ---
      if (useMixerStore.getState().keyboardMidiMode) {
        const tag = (e.target as HTMLElement)?.tagName;
        if (tag === 'INPUT' || tag === 'TEXTAREA' || (e.target as HTMLElement)?.isContentEditable) return;
        if (e.repeat) return; // Don't retrigger on held keys
        const note = KEYBOARD_NOTE_MAP[e.key.toLowerCase()];
        if (note !== undefined) {
          e.preventDefault();
          sendKeyboardMidi?.(note, 100); // note-on
        }
        return; // Don't process any shortcuts
      }

      // --- Spacebar: play / pause ---
      if (e.key === ' ' || e.code === 'Space') {
        const tag = (e.target as HTMLElement)?.tagName;
        if (tag !== 'INPUT' && tag !== 'TEXTAREA' && !(e.target as HTMLElement)?.isContentEditable) {
          e.preventDefault();
          useTransportStore.getState().togglePlaying();
          return;
        }
      }

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

      // --- Prevent macOS error beep for unhandled keys outside inputs ---
      const tag = (e.target as HTMLElement)?.tagName;
      const isInput = tag === 'INPUT' || tag === 'TEXTAREA' || (e.target as HTMLElement)?.isContentEditable;
      if (!isInput && e.key.length === 1 && !e.metaKey && !e.ctrlKey && !e.altKey) {
        e.preventDefault();
      }
    };

    const handleKeyUp = (e: KeyboardEvent) => {
      if (useMixerStore.getState().keyboardMidiMode) {
        const tag = (e.target as HTMLElement)?.tagName;
        if (tag === 'INPUT' || tag === 'TEXTAREA' || (e.target as HTMLElement)?.isContentEditable) return;
        const note = KEYBOARD_NOTE_MAP[e.key.toLowerCase()];
        if (note !== undefined) {
          e.preventDefault();
          sendKeyboardMidi?.(note, 0); // note-off
        }
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);

    // Listen for C++ loading progress
    const unsubProgress = addStateListener('loadingProgress', (data: unknown) => {
      const payload = data as ProgressPayload;
      if (payload.message === 'done') {
        setEngineReady(true);
      } else {
        setLoadingMsg(payload.message);
        setLoadingProgress((prev) => Math.max(prev, payload.progress));
      }
    });

    // Tell C++ the UI is ready so it can start heavy loading and sending progress events
    setTimeout(() => uiReady?.(), 100);

    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
      unsubProgress();
    };
  }, []);


  if (!engineReady) {
    return <LoadingScreen message={loadingMsg} progress={loadingProgress} />;
  }

  return (
    <div className={shell}>
      <Transport onSettingsOpen={openSettings} />
      <div className={middle}>
        <ArrangementView />
        {rightPanel === 'chat' && <ChatPanel />}
        {rightPanel === 'history' && <HistoryPanel />}
        {rightPanel === 'bird' && <BirdFilePanel />}
      </div>
      {midiEditorOpen && <MidiEditor />}
      {sampleEditorOpen && <SampleEditor />}
      <MixerPanel />
      <DebugPanel />
      <GenerateModal />
      <ExportProgressModal />
      <SettingsPanel open={settingsOpen} onClose={() => setSettingsOpen(false)} />
    </div>
  );
}

const shell = `h-screen w-screen flex flex-col bg-[hsl(var(--background))] text-[hsl(var(--foreground))] overflow-hidden`;
const middle = `flex-1 flex overflow-hidden`;

export default App;

