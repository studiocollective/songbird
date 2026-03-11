import './light.css';
import './dark.css';

import { isPlugin, Juce } from '@/lib';

export type Theme = 'light' | 'dark' | 'system';

const savePreference = isPlugin ? Juce.getNativeFunction('savePreference') : () => Promise.resolve(null);
const loadPreferenceNative = isPlugin ? Juce.getNativeFunction('loadPreference') : () => Promise.resolve('');

// In-memory cache so synchronous reads work after init
let cachedTheme: Theme = 'system';

/** Get the resolved theme (light or dark) based on current preference */
export function getResolvedTheme(theme: Theme): 'light' | 'dark' {
  if (theme === 'system') {
    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  }
  return theme;
}

/** Apply theme class to document root */
export function applyTheme(theme: Theme) {
  const resolved = getResolvedTheme(theme);
  document.documentElement.classList.toggle('dark', resolved === 'dark');
  cachedTheme = theme;
  savePreference('theme', theme);
}

/** Load saved theme preference (defaults to 'system') */
export function loadTheme(): Theme {
  return cachedTheme;
}

/** Initialize theme on app startup */
export async function initTheme() {
  // Load saved preference from C++ file-based storage
  try {
    const saved = await loadPreferenceNative('theme');
    if (saved === 'light' || saved === 'dark' || saved === 'system') {
      cachedTheme = saved as Theme;
    }
  } catch { /* use default */ }

  const theme = cachedTheme;

  // When running inside JUCE webview with 'system' preference,
  // query the native C++ side for the actual OS appearance as a fallback
  if (theme === 'system' && isPlugin) {
    try {
      const getSystemTheme = Juce.getNativeFunction('getSystemTheme');
      const nativeTheme = await getSystemTheme();
      if (nativeTheme === 'dark' || nativeTheme === 'light') {
        document.documentElement.classList.toggle('dark', nativeTheme === 'dark');
      } else {
        applyTheme(theme);
      }
    } catch {
      applyTheme(theme);
    }
  } else {
    applyTheme(theme);
  }

  // Listen for OS theme changes when using 'system'
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
    const current = loadTheme();
    if (current === 'system') {
      applyTheme('system');
    }
  });
}

