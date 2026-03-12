import { useState, useRef, useEffect } from 'react';
import { useMixerStore } from '@/data/store';
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/atoms/tabs';

export function GenerateModal() {
  const activeTrackId = useMixerStore((s) => s.activeGenerationTrackId);
  const closeGenerationUI = useMixerStore((s) => s.closeGenerationUI);

  const [position, setPosition] = useState({ x: 200, y: 150 });
  const isDragging = useRef(false);
  const dragStart = useRef({ x: 0, y: 0, initialX: 0, initialY: 0 });

  useEffect(() => {
    const handlePointerMove = (e: PointerEvent) => {
      if (!isDragging.current) return;
      
      const dx = e.clientX - dragStart.current.x;
      const dy = e.clientY - dragStart.current.y;
      
      setPosition({
        x: dragStart.current.initialX + dx,
        y: Math.max(0, dragStart.current.initialY + dy), // Prevent dragging completely off top
      });
    };

    const handlePointerUp = () => {
      if (isDragging.current) {
        isDragging.current = false;
        document.body.style.userSelect = '';
      }
    };

    window.addEventListener('pointermove', handlePointerMove);
    window.addEventListener('pointerup', handlePointerUp);

    return () => {
      window.removeEventListener('pointermove', handlePointerMove);
      window.removeEventListener('pointerup', handlePointerUp);
    };
  }, []);

  if (activeTrackId === null) return null;

  return (
    <div
      className={modalContainer}
      style={{
        transform: `translate(${position.x}px, ${position.y}px)`,
        position: 'absolute',
        top: 0,
        left: 0,
        zIndex: 1000,
      }}
    >
      {/* Title / Drag Bar */}
      <div
        className={titleBar}
        onPointerDown={(e) => {
          isDragging.current = true;
          dragStart.current = {
            x: e.clientX,
            y: e.clientY,
            initialX: position.x,
            initialY: position.y,
          };
          document.body.style.userSelect = 'none'; // Prevent text selection during drag
        }}
      >
        <div className="flex items-center gap-2 px-4 py-2 w-full select-none cursor-move">
          <span className="font-bold cursor-default tracking-wide text-[hsla(var(--foreground),0.9)]">The Infinite Crate</span>
          <span className="text-xs font-medium bg-indigo-500/20 text-indigo-300 px-1.5 py-0.5 rounded cursor-default border border-indigo-500/30">OSS</span>
          
          <div className="flex-1" />
          
          <button
            onClick={(e) => {
              e.stopPropagation();
              closeGenerationUI();
            }}
            className="w-5 h-5 flex items-center justify-center rounded-full hover:bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))] hover:text-red-400 transition-colors cursor-pointer"
            title="Close"
          >
            ✕
          </button>
        </div>
      </div>

      {/* Content Area */}
      <div className={contentArea}>
        <Tabs defaultValue="lyria" className="w-full h-full flex flex-col">
          <div className="flex justify-center w-full pt-4 pb-2 border-b border-[hsl(var(--border))]">
            <TabsList className="bg-[hsl(var(--muted))] h-8">
              <TabsTrigger value="lyria" className="px-6 text-xs data-[state=active]:bg-[hsl(var(--background))]">Lyria Model</TabsTrigger>
              <TabsTrigger value="suggestions" className="px-6 text-xs data-[state=active]:bg-[hsl(var(--background))]">Suggestions</TabsTrigger>
            </TabsList>
          </div>
          
          <div className="flex-1 overflow-auto p-6 flex flex-col items-center">
            <TabsContent value="lyria" className="w-full max-w-md space-y-4 m-0">
              <div className="text-center space-y-2 mt-4">
                <div className="mx-auto w-12 h-12 rounded-xl bg-indigo-500/20 flex items-center justify-center text-indigo-400 text-2xl shadow-inner border border-indigo-500/30 mb-6">
                  ✨
                </div>
                <h3 className="font-medium text-lg">Generate Audio</h3>
                <p className="text-sm text-[hsl(var(--muted-foreground))]">
                  Type a prompt below to generate new audio on Track {activeTrackId + 1}.
                </p>
              </div>
              
              <div className="mt-8 relative">
                <textarea 
                  placeholder="Describe the sound you want..." 
                  className="w-full bg-[hsl(var(--card))] border border-[hsl(var(--border))] rounded-lg p-3 text-sm min-h-[100px] resize-none focus:outline-none focus:border-indigo-500/50"
                />
                <button className="absolute bottom-3 right-3 bg-indigo-500 hover:bg-indigo-400 text-white px-4 py-1.5 rounded-md text-xs font-medium transition-colors cursor-pointer disabled:opacity-50">
                  Generate
                </button>
              </div>
            </TabsContent>
            
            <TabsContent value="suggestions" className="w-full flex-1 flex items-center justify-center m-0">
              <div className="text-center text-[hsl(var(--muted-foreground))]">
                <p className="text-sm">No suggestions available.</p>
                <p className="text-xs mt-1 opacity-70">Analyze the track first to get context-aware prompts.</p>
              </div>
            </TabsContent>
          </div>
        </Tabs>
      </div>
    </div>
  );
}

// Styling classes
const modalContainer = `
  w-[640px] h-[480px]
  bg-[hsl(var(--popover))]
  border border-[hsl(var(--border))]
  rounded-xl shadow-2xl
  flex flex-col overflow-hidden
  backdrop-blur-xl bg-opacity-95
`;

const titleBar = `
  flex-none
  border-b border-[hsl(var(--border))]
  bg-[hsl(var(--card))]/50
`;

const contentArea = `
  flex-1
  flex flex-col bg-gradient-to-b from-transparent to-[hsl(var(--background))]/30
`;
