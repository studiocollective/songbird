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

  toggleChat: () => void;
  setChatInput: (input: string) => void;
  addMessage: (role: 'user' | 'assistant', content: string) => void;
  clearMessages: () => void;
}

export const useChatSlice: StateCreator<ChatState> = (set) => ({
  initialized: false,
  initialize: () => set({ initialized: true }),

  chatOpen: true,
  chatMessages: [],
  chatInput: '',

  toggleChat: () => set((s) => ({ chatOpen: !s.chatOpen })),
  setChatInput: (chatInput) => set({ chatInput }),
  addMessage: (role, content) =>
    set((s) => ({
      chatMessages: [...s.chatMessages, { role, content }],
    })),
  clearMessages: () => set({ chatMessages: [] }),
});

export const ChatStateID = 'songbird-chat';
