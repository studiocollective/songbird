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
export async function applyTheme(theme: Theme) {
  let resolved = getResolvedTheme(theme);

  // Inside the JUCE WebView, matchMedia may not reflect the real OS theme.
  // Query the native C++ side for the actual appearance.
  if (theme === 'system' && isPlugin) {
    try {
      const getSystemTheme = Juce.getNativeFunction('getSystemTheme');
      const nativeTheme = await getSystemTheme();
      if (nativeTheme === 'dark' || nativeTheme === 'light') {
        resolved = nativeTheme;
      }
    } catch { /* fall back to matchMedia result */ }
  }

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

  await applyTheme(cachedTheme);

  // Listen for OS theme changes when using 'system'
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
    const current = loadTheme();
    if (current === 'system') {
      applyTheme('system');
    }
  });
}

