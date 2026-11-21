/** MainWindow.cpp:1101–1111,1120,1543. These are view extents, never document state. */
export const SPLITTER_WIDTH = 5;
export const LIBRARY_MINIMUM = 200;
export const INSPECTOR_MINIMUM = 240;
export const DEFAULT_LAYOUT = Object.freeze({ left: 230, preview: 700, right: 300 });
export type SessionLayoutPreset = 'left-wide' | 'inspector-wide';
export type SessionSplitter = 'left' | 'right';
export interface SessionLayoutState {
  readonly width: number;
  readonly previewMinimum: number;
  readonly preferredLeft: number;
  readonly preferredRight: number;
  readonly left: number;
  readonly preview: number;
  readonly right: number;
  readonly mode: 'three-column' | 'preview-inspector' | 'preview-only';
}

const extent = (value: number) => Number.isFinite(value) && value >= 0 ? Math.min(2147483647, Math.floor(value)) : 0;
const clamp = (value: number, minimum: number, maximum: number) => Math.max(minimum, Math.min(maximum, value));

function project(width: number, previewMinimum: number, preferredLeft: number, preferredRight: number): SessionLayoutState {
  width = extent(width); previewMinimum = Math.max(1, extent(previewMinimum));
  const facts = { width, previewMinimum, preferredLeft, preferredRight };
  // The accepted responsive protections are retained; these are not user-collapse modes.
  if (width <= 760) return { ...facts, left: 0, preview: width, right: 0, mode: 'preview-only' };
  if (width <= 1100) return { ...facts, left: 0, preview: width - 264, right: 264, mode: 'preview-inspector' };
  const available = width - 2 * SPLITTER_WIDTH;
  const minimumPreview = Math.min(previewMinimum, available - LIBRARY_MINIMUM - INSPECTOR_MINIMUM);
  const sideBudget = available - minimumPreview;
  let left = Math.max(LIBRARY_MINIMUM, extent(preferredLeft));
  let right = Math.max(INSPECTOR_MINIMUM, extent(preferredRight));
  // Stretch 0/1/0 keeps the outer extents until Preview reaches its content minimum.
  // If necessary, shrink the sides evenly, stopping at their non-collapsible minima.
  let deficit = Math.max(0, left + right - sideBudget);
  if (deficit) {
    const takeLeft = Math.min(left - LIBRARY_MINIMUM, Math.ceil(deficit / 2));
    left -= takeLeft; deficit -= takeLeft;
    const takeRight = Math.min(right - INSPECTOR_MINIMUM, deficit);
    right -= takeRight; deficit -= takeRight;
    left -= Math.min(left - LIBRARY_MINIMUM, deficit);
  }
  return { ...facts, left, preview: available - left - right, right, mode: 'three-column' };
}

export function createSessionLayout(width = 1240, previewMinimum = 1, preset?: SessionLayoutPreset): SessionLayoutState {
  return project(width, previewMinimum, preset === 'left-wide' ? 390 : DEFAULT_LAYOUT.left, preset === 'inspector-wide' ? 420 : DEFAULT_LAYOUT.right);
}

export function resizeSessionLayout(state: SessionLayoutState, width: number, previewMinimum = state.previewMinimum): SessionLayoutState {
  return project(width, previewMinimum, state.preferredLeft, state.preferredRight);
}

/** Position is the handle's leading edge in logical pixels relative to the workspace. */
export function moveSessionSplitter(state: SessionLayoutState, splitter: SessionSplitter, position: number): SessionLayoutState {
  if (state.mode !== 'three-column' || !Number.isFinite(position)) return state;
  position = Math.floor(position);
  const available = state.width - 2 * SPLITTER_WIDTH;
  const minimumPreview = Math.min(state.previewMinimum, available - LIBRARY_MINIMUM - INSPECTOR_MINIMUM);
  if (splitter === 'left') {
    const left = clamp(position, LIBRARY_MINIMUM, available - minimumPreview - INSPECTOR_MINIMUM);
    const right = Math.min(state.right, available - minimumPreview - left);
    return project(state.width, state.previewMinimum, left, right);
  }
  const right = clamp(state.width - position - SPLITTER_WIDTH, INSPECTOR_MINIMUM, available - minimumPreview - LIBRARY_MINIMUM);
  const left = Math.min(state.left, available - minimumPreview - right);
  return project(state.width, state.previewMinimum, left, right);
}

export function resetSessionLayout(state: SessionLayoutState): SessionLayoutState {
  return project(state.width, state.previewMinimum, DEFAULT_LAYOUT.left, DEFAULT_LAYOUT.right);
}
