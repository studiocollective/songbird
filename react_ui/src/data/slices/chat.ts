import type { StateCreator } from 'zustand';
import { Juce, isPlugin } from '@/lib';

export interface ChatMessage {
  role: 'user' | 'assistant';
  content: string;
  thinking?: string;
}

export interface ChatThread {
  id: string;
  title: string;
  messages: ChatMessage[];
  createdAt: number;
  summary?: string;
}

// --- C++ file-based persistence (replaces localStorage to avoid WebKit SQLite stalls) ---

const saveChatHistoryNative = isPlugin ? Juce.getNativeFunction('saveChatHistory') : () => Promise.resolve(null);
const loadChatHistoryNative = isPlugin ? Juce.getNativeFunction('loadChatHistory') : () => Promise.resolve('null');

// Debounce timer for save — coalesce writes to at most once per 2s
let saveTimer: ReturnType<typeof setTimeout> | null = null;

function generateId(): string {
  return Date.now().toString(36) + Math.random().toString(36).slice(2, 7);
}

// In-memory cache (loaded from C++ on init, written back debounced)
let cachedThreads: ChatThread[] = [];
let cachedActiveId: string | null = null;

function saveThreads(threads: ChatThread[]) {
  cachedThreads = threads;
  // Debounce: coalesce writes
  if (saveTimer) clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    const data = JSON.stringify({ threads, activeThreadId: cachedActiveId });
    saveChatHistoryNative(data);
  }, 2000);
}

function saveActiveThreadId(id: string) {
  cachedActiveId = id;
  // Will be saved along with threads in the next debounced write
  if (saveTimer) clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    const data = JSON.stringify({ threads: cachedThreads, activeThreadId: id });
    saveChatHistoryNative(data);
  }, 2000);
}

// --- Chat State Slice ---
export interface ChatState {
  initialized: boolean;
  initialize: () => void;

  chatOpen: boolean;
  rightPanel: 'chat' | 'history' | 'bird' | null;
  chatMessages: ChatMessage[];
  chatInput: string;
  apiKey: string | null;
  selectedModel: string;
  isThinking: boolean;
  isStreaming: boolean;
  thinkingText: string;
  toolUseLabel: string | null;

  // Thread management
  activeThreadId: string;
  threads: ChatThread[];
  threadMenuOpen: boolean;

  toggleChat: () => void;
  setRightPanel: (panel: 'chat' | 'history' | 'bird' | null) => void;
  setChatInput: (input: string) => void;
  setApiKey: (key: string) => void;
  setSelectedModel: (model: string) => void;
  setThinking: (v: boolean) => void;
  setStreaming: (v: boolean) => void;
  setThinkingText: (text: string) => void;
  setToolUseLabel: (label: string | null) => void;
  addMessage: (role: 'user' | 'assistant', content: string, thinking?: string) => void;
  updateLastMessage: (content: string) => void;
  setLastMessageThinking: (thinking: string) => void;
  removeLastMessage: () => void;
  clearMessages: () => void;

  // Thread actions
  newThread: () => void;
  switchThread: (id: string) => void;
  deleteThread: (id: string) => void;
  setThreadTitle: (title: string) => void;
  setThreadSummary: (threadId: string, summary: string) => void;
  toggleThreadMenu: () => void;
  persistCurrentThread: () => void;
  getRecentSummaries: () => string[];
}

function createThread(): ChatThread {
  return {
    id: generateId(),
    title: 'New Chat',
    messages: [],
    createdAt: Date.now(),
  };
}

export const useChatSlice: StateCreator<ChatState> = (set, get) => {
  // Start with an empty thread — real history loads async in initialize()
  const initialThread = createThread();

  return {
    initialized: false,
    initialize: () => {
      // Async-load chat history from C++ file on first call
      loadChatHistoryNative().then((raw: unknown) => {
        try {
          const data = typeof raw === 'string' ? JSON.parse(raw) : null;
          if (data && Array.isArray(data.threads) && data.threads.length > 0) {
            cachedThreads = data.threads;
            cachedActiveId = data.activeThreadId || data.threads[0].id;
            const active = cachedThreads.find(t => t.id === cachedActiveId) || cachedThreads[0];
            set({
              initialized: true,
              threads: cachedThreads,
              activeThreadId: active.id,
              chatMessages: active.messages,
            });
            return;
          }
        } catch { /* ignore parse errors */ }
        // No saved history — keep the initial empty thread
        cachedThreads = [initialThread];
        cachedActiveId = initialThread.id;
        set({ initialized: true });
      });
    },

    chatOpen: true,
    rightPanel: 'chat' as 'chat' | 'history' | 'bird' | null,
    chatMessages: [],
    chatInput: '',
    apiKey: null,
    selectedModel: 'gemini-3.1-pro-preview',
    isThinking: false,
    isStreaming: false,
    thinkingText: '',
    toolUseLabel: null,

    activeThreadId: initialThread.id,
    threads: [initialThread],
    threadMenuOpen: false,

    toggleChat: () => set((s) => ({ chatOpen: !s.chatOpen })),
    setRightPanel: (panel) => set((s) => ({
      rightPanel: s.rightPanel === panel ? null : panel,
      chatOpen: panel === 'chat' ? true : panel === null ? false : s.chatOpen,
    })),
    setChatInput: (chatInput) => set({ chatInput }),
    setApiKey: (apiKey) => set({ apiKey }),
    setSelectedModel: (selectedModel) => set({ selectedModel }),
    setThinking: (isThinking) => set({ isThinking }),
    setStreaming: (isStreaming) => set({ isStreaming }),
    setThinkingText: (thinkingText) => set({ thinkingText }),
    setToolUseLabel: (toolUseLabel) => set({ toolUseLabel }),

    addMessage: (role, content, thinking) => {
      set((s) => ({ chatMessages: [...s.chatMessages, { role, content, thinking }] }));
      // Auto-persist to thread storage
      setTimeout(() => get().persistCurrentThread(), 0);
    },

    updateLastMessage: (content) => {
      set((s) => {
        const msgs = [...s.chatMessages];
        if (msgs.length > 0) {
          msgs[msgs.length - 1] = { ...msgs[msgs.length - 1], content };
        }
        return { chatMessages: msgs };
      });
      // Debounced persist (don't persist every streaming chunk)
    },

    setLastMessageThinking: (thinking) => {
      set((s) => {
        const msgs = [...s.chatMessages];
        if (msgs.length > 0) {
          msgs[msgs.length - 1] = { ...msgs[msgs.length - 1], thinking };
        }
        return { chatMessages: msgs };
      });
    },

    removeLastMessage: () => {
      set((s) => ({ chatMessages: s.chatMessages.slice(0, -1) }));
      setTimeout(() => get().persistCurrentThread(), 0);
    },

    clearMessages: () => {
      set({ chatMessages: [] });
      setTimeout(() => get().persistCurrentThread(), 0);
    },

    // --- Thread actions ---

    newThread: () => {
      const state = get();
      // Persist current thread before switching
      state.persistCurrentThread();

      // Auto-title the old thread from first user message if still "New Chat"
      const threads = [...state.threads];
      const oldIdx = threads.findIndex(t => t.id === state.activeThreadId);
      if (oldIdx >= 0 && threads[oldIdx].title === 'New Chat') {
        const firstUserMsg = threads[oldIdx].messages.find(m => m.role === 'user');
        if (firstUserMsg) {
          threads[oldIdx].title = firstUserMsg.content.slice(0, 40) + (firstUserMsg.content.length > 40 ? '…' : '');
        }
      }

      const thread = createThread();
      threads.unshift(thread);

      // Keep max 20 threads
      const trimmed = threads.slice(0, 20);

      saveThreads(trimmed);
      saveActiveThreadId(thread.id);

      set({
        activeThreadId: thread.id,
        threads: trimmed,
        chatMessages: [],
        threadMenuOpen: false,
      });
    },

    switchThread: (id) => {
      const state = get();
      state.persistCurrentThread();

      const thread = state.threads.find(t => t.id === id);
      if (!thread) return;

      saveActiveThreadId(id);
      set({
        activeThreadId: id,
        chatMessages: thread.messages,
        threadMenuOpen: false,
      });
    },

    deleteThread: (id) => {
      const state = get();
      const threads = state.threads.filter(t => t.id !== id);

      if (id === state.activeThreadId) {
        // Switch to another thread or create new
        if (threads.length > 0) {
          saveActiveThreadId(threads[0].id);
          set({
            activeThreadId: threads[0].id,
            chatMessages: threads[0].messages,
            threads,
          });
        } else {
          const thread = createThread();
          threads.unshift(thread);
          saveActiveThreadId(thread.id);
          set({
            activeThreadId: thread.id,
            chatMessages: [],
            threads,
          });
        }
      } else {
        set({ threads });
      }

      saveThreads(threads);
    },

    setThreadTitle: (title) => {
      set((s) => {
        const threads = s.threads.map(t =>
          t.id === s.activeThreadId ? { ...t, title } : t
        );
        saveThreads(threads);
        return { threads };
      });
    },

    setThreadSummary: (threadId, summary) => {
      set((s) => {
        const threads = s.threads.map(t =>
          t.id === threadId ? { ...t, summary } : t
        );
        saveThreads(threads);
        return { threads };
      });
    },

    toggleThreadMenu: () => set((s) => ({ threadMenuOpen: !s.threadMenuOpen })),

    persistCurrentThread: () => {
      const state = get();
      const threads = state.threads.map(t =>
        t.id === state.activeThreadId
          ? { ...t, messages: state.chatMessages }
          : t
      );
      saveThreads(threads);
      set({ threads });
    },

    getRecentSummaries: () => {
      const state = get();
      return state.threads
        .filter(t => t.id !== state.activeThreadId && t.summary)
        .slice(0, 5)
        .map(t => `- "${t.title}": ${t.summary}`);
    },
  };
};

export const ChatStateID = 'songbird-chat';
