// @ts-ignore - JUCE bridge is not a standard module
import * as Juce from './juce/juce.js';
export { Juce };
export const isPlugin = typeof window !== 'undefined' && !!window.__JUCE__;

// Re-export theme utilities
export { initTheme, applyTheme, loadTheme, getResolvedTheme } from './theme';
export type { Theme } from './theme';
