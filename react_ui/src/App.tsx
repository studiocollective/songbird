import { Transport } from '@/components/Transport';
import { ArrangementView } from '@/components/ArrangementView';
import { MixerPanel } from '@/components/MixerPanel';
import { ChatPanel } from '@/components/ChatPanel';
import { DebugPanel } from '@/components/DebugPanel';

function App() {
  return (
    <div className={shell}>
      <Transport />
      <div className={middle}>
        <ArrangementView />
        <ChatPanel />
      </div>
      <MixerPanel />
      <DebugPanel />
    </div>
  );
}

const shell = `h-screen w-screen flex flex-col bg-[hsl(var(--background))] text-[hsl(var(--foreground))] overflow-hidden`;
const middle = `flex-1 flex overflow-hidden`;

export default App;

