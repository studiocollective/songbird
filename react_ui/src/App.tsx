import { Transport } from '@/components/Transport';
import { ArrangementView } from '@/components/ArrangementView';
import { MixerPanel } from '@/components/MixerPanel';
import { ChatPanel } from '@/components/ChatPanel';

function App() {
  return (
    <div className="h-screen w-screen flex flex-col bg-zinc-950 text-white overflow-hidden">
      {/* Top: Transport bar */}
      <Transport />

      {/* Middle: Arrangement + Chat */}
      <div className="flex-1 flex overflow-hidden">
        <ArrangementView />
        <ChatPanel />
      </div>

      {/* Bottom: Mixer (slides up) */}
      <MixerPanel />
    </div>
  );
}

export default App;
