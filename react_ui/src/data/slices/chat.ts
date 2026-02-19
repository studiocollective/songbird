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

  toggleChat: () => void;
  setChatInput: (input: string) => void;
  setApiKey: (key: string) => void;
  setSelectedModel: (model: string) => void;
  addMessage: (role: 'user' | 'assistant', content: string) => void;
  updateLastMessage: (content: string) => void;
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

  toggleChat: () => set((s) => ({ chatOpen: !s.chatOpen })),
  setChatInput: (chatInput) => set({ chatInput }),
  setApiKey: (apiKey) => set({ apiKey }),
  setSelectedModel: (selectedModel) => set({ selectedModel }),
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
  clearMessages: () => set({ chatMessages: [] }),
});

export const ChatStateID = 'songbird-chat';
