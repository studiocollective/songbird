/**
 * Per-fader persist suppression for slider drags.
 *
 * While any slider is being dragged, Zustand persist for the mixer store
 * is suppressed (the bridge's setItem becomes a no-op for 'songbird-mixer').
 * Real-time audio feedback is provided via a lightweight native function
 * that bypasses the state-cache / commit path.
 *
 * On slider release, the flag is cleared and a single persist + commit fires.
 */

let activeSliderDrags = 0;

/** Call on pointer-down before a slider drag begins. */
export function beginSliderDrag(): void {
  activeSliderDrags++;
}

/**
 * Call on pointer-up / onValueCommit when a slider drag ends.
 * Returns true if all drags have ended (persist should resume).
 */
export function endSliderDrag(): boolean {
  activeSliderDrags = Math.max(0, activeSliderDrags - 1);
  return activeSliderDrags === 0;
}

/** True while any slider is being dragged (persist suppressed). */
export function isSliderDragging(): boolean {
  return activeSliderDrags > 0;
}
