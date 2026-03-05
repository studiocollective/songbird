import { useState, useEffect, useRef, useCallback } from 'react';
import { Juce, isPlugin } from '@/lib';

export function BirdFilePanel() {
  // `content` = last saved/loaded version; `editedContent` = live textarea value
  const [content, setContent] = useState<string>('');
  const [editedContent, setEditedContent] = useState<string>('');
  const [error, setError] = useState<string>('');
  const didFetch = useRef<boolean | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  const isDirty = editedContent !== content;

  const refresh = useCallback(() => {
    if (!isPlugin) return;
    const fn = Juce.getNativeFunction('readBird');
    fn().then((raw: unknown) => {
      const text = (raw as string) || '';
      setContent(text);
      setEditedContent(text);
      setError('');
    }).catch(() => {});
  }, []);

  // Auto-fetch once on mount
  if (didFetch.current == null) {
    didFetch.current = true;
    setTimeout(refresh, 50);
  }

  // Auto-refresh when bird file changes (undo/redo, AI edit) — but only if not dirty
  useEffect(() => {
    if (!isPlugin) return;
    const handler = () => {
      // Don't clobber in-progress edits
      if (editedContent === content) {
        setTimeout(refresh, 100);
      }
    };
    window.__JUCE__!.backend.addEventListener('historyChanged', handler);
    window.__JUCE__!.backend.addEventListener('birdContentChanged', handler);
    return () => {
      window.__JUCE__!.backend.removeEventListener('historyChanged', handler);
      window.__JUCE__!.backend.removeEventListener('birdContentChanged', handler);
    };
  }, [refresh, editedContent, content]);

  // Save handler — called by ⌘S and save button
  const handleSave = useCallback(() => {
    if (!isPlugin) return;
    const fn = Juce.getNativeFunction('writeBirdUser');
    fn(editedContent).then((raw: unknown) => {
      try {
        const resp = JSON.parse(raw as string);
        if (resp.success) {
          setContent(editedContent); // clear dirty state
          setError('');
        } else {
          setError(resp.error || 'Unknown error');
        }
      } catch {
        // Fallback if response isn't JSON
        setContent(editedContent);
        setError('');
      }
    }).catch(() => {});
  }, [editedContent]);

  // ⌘S save — uses window.addEventListener like undo/redo in App.tsx
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key === 's') {
        e.preventDefault();
        handleSave();
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [handleSave]);

  // Tab key support — insert 2 spaces instead of changing focus
  const handleTab = useCallback((e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Tab') {
      e.preventDefault();
      const ta = e.currentTarget;
      const start = ta.selectionStart;
      const end = ta.selectionEnd;
      const val = ta.value;
      const newVal = val.substring(0, start) + '  ' + val.substring(end);
      setEditedContent(newVal);
      // Restore cursor position after React re-render
      requestAnimationFrame(() => {
        ta.selectionStart = ta.selectionEnd = start + 2;
      });
    }
  }, []);

  return (
    <div className={panel}>
      <div className={panelInner}>
        <div className={header}>
          <span className="text-[10px]">🐦</span>
          <span className="text-xs font-medium text-[hsl(var(--foreground))]">
            Bird File
            {isDirty && <span className={dirtyDot} title="Unsaved changes">●</span>}
          </span>
          {error && (
            <span className={errorText} title={error}>
              ⚠ {error.length > 30 ? error.slice(0, 30) + '…' : error}
            </span>
          )}
          <button
            onClick={handleSave}
            className={`${saveBtn} ${isDirty ? saveBtnActive : ''}`}
            title="Save (⌘S)"
          >
            {isDirty ? '⌘S Save' : '⌘S to save'}
          </button>
          <button onClick={refresh} className={refreshBtn} title="Refresh">↻</button>
        </div>

        <div className={editorWrap}>
          {content.length === 0 && !isDirty ? (
            <div className={emptyLine}>No bird file loaded</div>
          ) : (
            <textarea
              ref={textareaRef}
              className={textArea}
              value={editedContent}
              onChange={(e) => {
                setEditedContent(e.target.value);
                if (error) setError(''); // clear error on edit
              }}
              onKeyDown={handleTab}
              spellCheck={false}
              autoComplete="off"
              autoCorrect="off"
              autoCapitalize="off"
            />
          )}
        </div>
      </div>
    </div>
  );
}

const panel = `
  bg-[hsl(var(--background))] border-l border-[hsl(var(--border))]
  w-80 flex flex-col overflow-hidden`;

const panelInner = `w-80 h-full flex flex-col`;

const header = `
  h-10 shrink-0 border-b border-[hsl(var(--border))]
  flex items-center px-3 gap-1.5`;

const refreshBtn = `
  text-sm text-[hsl(var(--muted-foreground))]
  hover:text-[hsl(var(--foreground))] transition-colors
  bg-transparent border-none cursor-pointer`;

const saveBtn = `
  ml-auto text-[10px] text-[hsl(var(--muted-foreground))]
  bg-transparent border-none cursor-pointer
  opacity-50 select-none transition-all
  hover:opacity-80`;

const saveBtnActive = `
  !opacity-100 !text-[hsl(var(--primary))]
  font-medium`;

const dirtyDot = `
  ml-1 text-[hsl(var(--primary))] text-[10px]`;

const errorText = `
  text-[10px] text-red-400
  truncate max-w-[140px]`;

const editorWrap = `flex-1 overflow-hidden`;

const textArea = `
  w-full h-full resize-none border-none outline-none
  bg-transparent text-[hsl(var(--foreground))]
  font-mono text-[11px] leading-snug
  p-2 m-0
  overflow-auto
  placeholder:text-[hsl(var(--muted-foreground))]`;

const emptyLine = `
  px-3 py-4 text-center text-[11px]
  text-[hsl(var(--muted-foreground))] italic`;
