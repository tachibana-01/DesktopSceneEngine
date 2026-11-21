import type { SessionPresentation, SessionProjection } from './sessionTypes.ts';
import { entityTransition, findItem, findPlaylist } from './sessionEntities.ts';

export type InspectorAction =
  | { type: 'rename-playlist'; id: string; alias: string }
  | { type: 'rename-item'; id: string; alias: string }
  | { type: 'playlist-master' | 'image-duration' | 'video-rate' | 'media-gain'; id: string; value: number }
  | { type: 'presentation'; id: string; value: SessionPresentation };

import { normalizedAspect, positiveNativeInt } from './sessionPresentationValues.ts';
export { normalizedAspect } from './sessionPresentationValues.ts';

function validPresentation(value: SessionPresentation): boolean {
  const { crop, layout, mediaPresentation } = value;
  return [crop.x, crop.y, crop.width, crop.height, layout.positionX, layout.positionY, layout.scaleX, layout.scaleY].every(Number.isFinite)
    && crop.x >= 0 && crop.y >= 0 && crop.width > 0 && crop.height > 0
    // Crop and Layout are float-valued native structures. The caller supplies
    // Math.fround components; C++ also performs each crop addition as float.
    && Math.fround(crop.x + crop.width) <= 1 && Math.fround(crop.y + crop.height) <= 1
    && layout.scaleX > 0 && layout.scaleY > 0
    && ['native', 'auto-fill', 'custom'].includes(mediaPresentation.mode)
    && positiveNativeInt(mediaPresentation.customAspect.numerator)
    && positiveNativeInt(mediaPresentation.customAspect.denominator);
}

function samePresentation(left: SessionPresentation, right: SessionPresentation): boolean {
  return left.crop.x === right.crop.x && left.crop.y === right.crop.y
    && left.crop.width === right.crop.width && left.crop.height === right.crop.height
    && left.layout.positionX === right.layout.positionX && left.layout.positionY === right.layout.positionY
    && left.layout.scaleX === right.layout.scaleX && left.layout.scaleY === right.layout.scaleY
    && left.mediaPresentation.mode === right.mediaPresentation.mode
    && left.mediaPresentation.customAspect.numerator === right.mediaPresentation.customAspect.numerator
    && left.mediaPresentation.customAspect.denominator === right.mediaPresentation.customAspect.denominator;
}

function normalizedVolume(value: number): number {
  return Number.isFinite(value) ? Math.min(1, Math.max(0, value)) : 1;
}

function projectedItemId(state: SessionProjection): string | null {
  return state.runtime && state.activeItemId && state.runtime.items.some(item => item.id === state.activeItemId)
    ? state.activeItemId : null;
}

function updateOutputVolume(state: SessionProjection): void {
  if (!state.runtime) return;
  const master = normalizedVolume(findPlaylist(state, state.activePlaylistId)?.masterVolume ?? 1);
  const activeItem = findItem(state, projectedItemId(state))?.item;
  const gain = activeItem?.kind === 'video' ? normalizedVolume(activeItem.mediaGain) : 1;
  // MainWindow resolves master as float, multiplies by the double Item factor,
  // then passes float gain to Engine::setVolume. This never changes BGM gain.
  state.runtime.volume = Math.fround(Math.fround(master) * gain);
}

function restoreVisualProjection(state: SessionProjection, itemId: string): void {
  if (!state.runtime || projectedItemId(state) !== itemId) return;
  const target = findItem(state, itemId);
  if (!target || target.playlist.id !== state.activePlaylistId) return;
  const fault = state.faults.playback;
  if (fault === 'rebuild-prepare') return;
  if (fault === 'rollback-bind') {
    state.runtime = null;
    state.activePlaylistId = state.activeItemId = null;
    return;
  }
  state.runtime.halted = false;
  // A failed bind can restore the previous complete (possibly stale) scene
  // projection. Inspector ignores the rebuild result; no warning is invented.
  if (fault !== 'rebuild-bind' && fault !== 'rollback-seek') {
    state.runtime.items = structuredClone(target.playlist.items);
  }
  if (fault === 'restore-seek' || fault === 'rollback-seek') state.runtime.time = 0;
  updateOutputVolume(state);
}

/** UI-only Inspector projection of existing Player setters and handlers. */
export function inspectorTransition(state: SessionProjection, action: InspectorAction): SessionProjection {
  if (state.closed) return state;
  if (action.type === 'rename-playlist' || action.type === 'rename-item') {
    const next = entityTransition(state, action);
    // The shared alias mutation is identical; only inline editingFinished has
    // silent restore rather than the context menu's Invalid Name warning.
    return next.feedback?.title === 'Invalid Name'
      ? { ...next, feedback: state.feedback } : next;
  }

  const next = structuredClone(state);
  if (action.type === 'presentation') {
    const target = findItem(next, action.id);
    if (!target || !validPresentation(action.value)) return state;
    if (!samePresentation(target.item.presentation, action.value)) {
      target.item.presentation = structuredClone(action.value);
      next.document.revision++;
    }
    // Model same-value success still reaches MainWindow's runtime handler.
    if (next.runtime && next.activeItemId === action.id) restoreVisualProjection(next, action.id);
    return next;
  }

  if (!Number.isFinite(action.value)) return state;
  if (action.type === 'playlist-master') {
    const target = findPlaylist(next, action.id);
    if (!target) return state;
    const value = normalizedVolume(action.value);
    if (target.masterVolume !== value) {
      target.masterVolume = value;
      next.document.revision++;
    }
    if (next.runtime && next.activePlaylistId === action.id) updateOutputVolume(next);
    return next;
  }

  const target = findItem(next, action.id);
  if (!target) return state;
  switch (action.type) {
    case 'image-duration':
      if (target.item.kind !== 'image' || action.value <= 0) return state;
      if (target.item.duration !== action.value) {
        target.item.duration = action.value;
        next.document.revision++;
      }
      // The production handler authors duration only. Its existing active
      // projection remains unchanged until some later projection rebuild.
      return next;
    case 'video-rate': {
      const stepCount = (action.value - 0.5) / 0.1;
      if (target.item.kind !== 'video' || action.value < 0.5 || action.value > 2
        || Math.abs(stepCount - Math.round(stepCount)) >= 0.000001) return state;
      if (target.item.rate !== action.value) {
        target.item.rate = action.value;
        next.document.revision++;
      }
      if (next.runtime && next.activeItemId === action.id && projectedItemId(next) === action.id
        && !next.faults.rateFails) next.runtime.rate = Math.fround(action.value);
      return next;
    }
    case 'media-gain': {
      if (target.item.kind !== 'video') return state;
      const value = normalizedVolume(action.value);
      if (target.item.mediaGain !== value) {
        target.item.mediaGain = value;
        next.document.revision++;
      }
      if (projectedItemId(next) === action.id) updateOutputVolume(next);
      return next;
    }
  }
}
