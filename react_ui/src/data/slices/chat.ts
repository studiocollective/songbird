import type { StateCreator } from 'zustand';

export interface ChatMessage {
  role: 'user' | 'assistant';
  content: string;
}

// --- Chat State Slice ---
export interface ChatState {
  initialized: boolean;
  initialize: () => void;

  chatOpen: boolean;
  chatMessages: ChatMessage[];
  chatInput: string;
  apiKey: string | null;
  selectedModel: string;
  isThinking: boolean;
  isStreaming: boolean;
  toolUseLabel: string | null;

  toggleChat: () => void;
  setChatInput: (input: string) => void;
  setApiKey: (key: string) => void;
  setSelectedModel: (model: string) => void;
  setThinking: (v: boolean) => void;
  setStreaming: (v: boolean) => void;
  setToolUseLabel: (label: string | null) => void;
  addMessage: (role: 'user' | 'assistant', content: string) => void;
  updateLastMessage: (content: string) => void;
  removeLastMessage: () => void;
  clearMessages: () => void;
}

export const useChatSlice: StateCreator<ChatState> = (set) => ({
  initialized: false,
  initialize: () => set({ initialized: true }),

  chatOpen: true,
  chatMessages: [],
  chatInput: '',
  apiKey: null,
  selectedModel: 'gemini-3-flash-preview',
  isThinking: false,
  isStreaming: false,
  toolUseLabel: null,

  toggleChat: () => set((s) => ({ chatOpen: !s.chatOpen })),
  setChatInput: (chatInput) => set({ chatInput }),
  setApiKey: (apiKey) => set({ apiKey }),
  setSelectedModel: (selectedModel) => set({ selectedModel }),
  setThinking: (isThinking) => set({ isThinking }),
  setStreaming: (isStreaming) => set({ isStreaming }),
  setToolUseLabel: (toolUseLabel) => set({ toolUseLabel }),
  addMessage: (role, content) =>
    set((s) => ({
      chatMessages: [...s.chatMessages, { role, content }],
    })),
  updateLastMessage: (content) =>
    set((s) => {
      const msgs = [...s.chatMessages];
      if (msgs.length > 0) {
        msgs[msgs.length - 1] = { ...msgs[msgs.length - 1], content };
      }
      return { chatMessages: msgs };
    }),
  removeLastMessage: () =>
    set((s) => ({
      chatMessages: s.chatMessages.slice(0, -1),
    })),
  clearMessages: () => set({ chatMessages: [] }),
});

export const ChatStateID = 'songbird-chat';
