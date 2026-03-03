import type { StateCreator } from 'zustand';

export interface ChatMessage {
  role: 'user' | 'assistant';
  content: string;
}

export interface ChatThread {
  id: string;
  title: string;
  messages: ChatMessage[];
  createdAt: number;
  summary?: string;
}

// --- localStorage helpers ---
const THREADS_KEY = 'songbird-chat-threads';
const ACTIVE_THREAD_KEY = 'songbird-chat-active-thread';

function generateId(): string {
  return Date.now().toString(36) + Math.random().toString(36).slice(2, 7);
}

function loadThreads(): ChatThread[] {
  try {
    const raw = localStorage.getItem(THREADS_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch { return []; }
}

function saveThreads(threads: ChatThread[]) {
  try {
    localStorage.setItem(THREADS_KEY, JSON.stringify(threads));
  } catch { /* ignore quota errors */ }
}

function loadActiveThreadId(): string | null {
  return localStorage.getItem(ACTIVE_THREAD_KEY);
}

function saveActiveThreadId(id: string) {
  localStorage.setItem(ACTIVE_THREAD_KEY, id);
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
  addMessage: (role: 'user' | 'assistant', content: string) => void;
  updateLastMessage: (content: string) => void;
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
  // Load saved threads and active thread on init
  const savedThreads = loadThreads();
  const savedActiveId = loadActiveThreadId();
  let activeThread = savedThreads.find(t => t.id === savedActiveId);
  if (!activeThread) {
    activeThread = createThread();
    savedThreads.unshift(activeThread);
    saveThreads(savedThreads);
    saveActiveThreadId(activeThread.id);
  }

  return {
    initialized: false,
    initialize: () => set({ initialized: true }),

    chatOpen: true,
    rightPanel: 'chat' as 'chat' | 'history' | 'bird' | null,
    chatMessages: activeThread.messages,
    chatInput: '',
    apiKey: null,
    selectedModel: 'gemini-3-flash-preview',
    isThinking: false,
    isStreaming: false,
    thinkingText: '',
    toolUseLabel: null,

    activeThreadId: activeThread.id,
    threads: savedThreads,
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

    addMessage: (role, content) => {
      set((s) => ({ chatMessages: [...s.chatMessages, { role, content }] }));
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
