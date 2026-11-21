import { findItem, findPlaylist, itemPlayable, playlistPlayable } from './sessionEntities.ts';
import type { EntityAction } from './sessionEntities.ts';
import {
  controllerClear, controllerCompleteCurrentItem, controllerCurrent, controllerPrevious,
  controllerRebase, controllerSetPlaylistMode, controllerSynchronizeItemMode,
} from './sessionPlaybackSequence.ts';
import type { Collections, Position, SequenceMode } from './sessionPlaybackSequence.ts';
import type { SessionItem, SessionProjection } from './sessionTypes.ts';

export type PlaybackAction =
  | { type: 'project-mode'; mode: SequenceMode }
  | { type: 'playlist-item-mode'; id: string; mode: SequenceMode }
  | { type: 'previous' | 'next' | 'toggle-pause' | 'toggle-mute' | 'natural-completion' }
  | { type: 'advance-time'; seconds: number };

/** Latest authored playable-ID snapshot; decoder availability is not eligibility. */
function collectionsFor(state: SessionProjection): Collections {
  const playlists = state.project.playlists.filter(playlistPlayable);
  return {
    playlistIds: playlists.map(playlist => playlist.id),
    itemsByPlaylist: playlists.map(playlist => ({
      playlistId: playlist.id, itemMode: playlist.itemMode,
      itemIds: playlist.items.filter(item => itemPlayable(playlist, item)).map(item => item.id),
    })),
  };
}
function synchronizeSequence(state: SessionProjection, collections: Collections): void {
  if (state.sequence.playlistMode !== state.project.playlistMode) {
    controllerSetPlaylistMode(state.sequence, state.project.playlistMode, collections);
  }
  controllerSynchronizeItemMode(state.sequence, collections);
}

/** Mutating initialization seam for fixtures/already-cloned transition results. */
export function rebasePlaybackToActive(state: SessionProjection): void {
  if (!state.runtime || !state.activePlaylistId || !state.activeItemId
    || !state.runtime.items.some(item => item.id === state.activeItemId)) {
    controllerClear(state.sequence);
    return;
  }
  const collections = collectionsFor(state);
  synchronizeSequence(state, collections);
  controllerRebase(state.sequence, collections, state.activePlaylistId, state.activeItemId);
}
function stop(state: SessionProjection): void {
  state.runtime = null;
  state.activePlaylistId = state.activeItemId = null;
  controllerClear(state.sequence);
}
function normalizedVolume(value: number): number {
  return Number.isFinite(value) ? Math.min(1, Math.max(0, value)) : 1;
}
function outputVolume(state: SessionProjection, item: SessionItem): number {
  const master = normalizedVolume(findPlaylist(state, state.activePlaylistId)?.masterVolume ?? 1);
  const gain = item.kind === 'video' ? normalizedVolume(item.mediaGain) : 1;
  return Math.fround(Math.fround(master) * gain);
}
function warning(state: SessionProjection, message: string): void {
  state.feedback = { level: 'warning', title: 'Unable to play media', message };
}
function projectionValid(items: readonly SessionItem[]): boolean {
  return items.length > 0 && items.every(item => Boolean(item.id && item.source.trim())
    && (item.kind === 'image' ? Number.isFinite(item.duration) && item.duration > 0
      : item.kind === 'video' && Number.isFinite(item.rate) && item.rate >= 0.5 && item.rate <= 2));
}

// Player's within-Playlist switch uses the existing bound projection. Crossing
// Playlists prepares a fresh projection; failed bind may restore the old one.
function transitionToPosition(state: SessionProjection, position: Position): boolean {
  const oldRuntime = state.runtime;
  if (!oldRuntime) return false;
  const target = findItem(state, position.itemId);
  const playlist = findPlaylist(state, position.playlistId);
  if (!target || !playlist || target.playlist.id !== playlist.id) return false;
  const crossesPlaylist = state.activePlaylistId !== playlist.id;
  const fault = state.faults.navigation;
  if (crossesPlaylist) state.timelineDragCancellation = (state.timelineDragCancellation ?? 0) + 1;
  if (crossesPlaylist && (fault === 'prepare' || !projectionValid(playlist.items))) {
    warning(state, 'Unable to create a playback sequence.');
    return false;
  }
  const projectionItems = crossesPlaylist ? structuredClone(playlist.items) : oldRuntime.items;
  const projectedItem = projectionItems.find(item => item.id === position.itemId);
  if (!projectedItem) return false;
  if (fault === 'bind' || fault === 'rollback') {
    if (crossesPlaylist) {
      // startPlaylistPlayback's failed rollback drops the projection without
      // calling stopActivePlayback: Workspace active IDs remain stale. The
      // Rail must use configured runtime existence, not those IDs, as authority.
      if (fault === 'rollback') state.runtime = null;
      else state.runtime = { ...oldRuntime, time: 0, halted: false };
      warning(state, 'Media file could not be opened.');
    }
    // Same-Playlist Engine switch failure has no Player warning or rollback
    // bind: old time/pause/rate/halt remain intact. Never skip this target.
    return false;
  }
  state.activePlaylistId = playlist.id;
  state.activeItemId = position.itemId;
  state.runtime = {
    time: 0, paused: false, halted: false,
    rate: projectedItem.kind === 'video' ? projectedItem.rate : 1,
    volume: outputVolume(state, target.item), items: projectionItems,
  };
  // MainWindow invalidates hover explicitly only after a successful in-Playlist
  // switch. Cross-Playlist starts are reconciled by full source identity.
  if (!crossesPlaylist) state.timelineInvalidation = (state.timelineInvalidation ?? 0) + 1;
  return true;
}

function navigate(state: SessionProjection, previous: boolean): void {
  if (!state.runtime || !state.activePlaylistId || !state.activeItemId) return;
  const collections = collectionsFor(state);
  synchronizeSequence(state, collections);
  if (!controllerCurrent(state.sequence)) rebasePlaybackToActive(state);
  const previousPosition = controllerCurrent(state.sequence);
  if (previous) {
    if (!previousPosition) return;
    const result = controllerPrevious(state.sequence, collections);
    if (result.kind === 'identity' && !transitionToPosition(state, result.identity)) {
      // Failed transition resets invocation history, as Player's rebase does.
      controllerRebase(state.sequence, collections, previousPosition.playlistId, previousPosition.itemId);
    }
    return;
  }
  if (!previousPosition) { stop(state); return; }
  const maximumAttempts = Math.max(1, collections.playlistIds.length * 2);
  for (let attempt = 0; attempt < maximumAttempts; attempt++) {
    const result = controllerCompleteCurrentItem(state.sequence, collections);
    if (result.kind === 'identity') {
      if (!transitionToPosition(state, result.identity)) {
        // Player rebases even if a failed rollback removed its projection.
        controllerRebase(state.sequence, collections, previousPosition.playlistId, previousPosition.itemId);
      }
      return;
    }
    if (result.kind === 'no-target') { stop(state); return; }
  }
  stop(state);
}

function isManualPlayAttempt(state: SessionProjection, action: EntityAction): boolean {
  if (action.type === 'play-playlist') return playlistPlayable(findPlaylist(state, action.id));
  if (action.type !== 'play-item') return false;
  const target = findItem(state, action.id);
  return Boolean(target && target.playlist.id === state.selectedPlaylistId && itemPlayable(target.playlist, target.item));
}

/** Reconcile accepted Phase A commands with this single shared runtime. */
export function afterEntityPlayback(before: SessionProjection, next: SessionProjection, action: EntityAction): SessionProjection {
  if (before.closed || next === before) return next;
  const result = structuredClone(next);
  if (!result.runtime) {
    if (!result.activePlaylistId && !result.activeItemId) controllerClear(result.sequence);
    return result;
  }
  if (isManualPlayAttempt(before, action)) {
    // A successful old-project rollback is a new Engine bind too: halt clears.
    result.runtime.halted = false;
    if (before.faults.playback !== 'start-bind') rebasePlaybackToActive(result);
  } else if (action.type === 'delete-item' && before.activeItemId === action.id
    && result.activeItemId !== before.activeItemId) {
    rebasePlaybackToActive(result);
  } else if ((action.type === 'add-media' && before.selectedPlaylistId === before.activePlaylistId
      && result.project.playlists.find(playlist => playlist.id === before.activePlaylistId)?.items.length
        !== before.project.playlists.find(playlist => playlist.id === before.activePlaylistId)?.items.length
    || action.type === 'delete-item' && findItem(before, action.id)?.playlist.id === before.activePlaylistId)
    && before.faults.playback !== 'rebuild-prepare') {
    // Restore-current rebuild preserves time/pause/rate but bind clears halt.
    result.runtime.halted = false;
  }
  return result;
}

/** UI-only Player runtime projection. No decoder, audio DSP, or persisted state. */
export function playbackTransition(state: SessionProjection, action: PlaybackAction): SessionProjection {
  if (state.closed) return state;
  if (action.type === 'project-mode') {
    if (!['list-loop', 'random', 'repeat-current'].includes(action.mode) || action.mode === state.project.playlistMode) return state;
    const next = structuredClone(state);
    next.project.playlistMode = action.mode;
    next.document.revision++;
    return next;
  }
  if (action.type === 'playlist-item-mode') {
    const playlist = findPlaylist(state, action.id);
    if (!playlist || !['list-loop', 'random', 'repeat-current'].includes(action.mode) || playlist.itemMode === action.mode) return state;
    const next = structuredClone(state);
    findPlaylist(next, action.id)!.itemMode = action.mode;
    next.document.revision++;
    return next;
  }
  if (action.type === 'toggle-mute') return { ...state, outputMuted: !state.outputMuted };
  if (!state.runtime || !state.activePlaylistId || !state.activeItemId) return state;
  if (action.type === 'toggle-pause' && state.runtime.halted) return state;
  if (action.type === 'natural-completion' && state.runtime.halted) return state;
  if (action.type === 'advance-time' && (state.runtime.paused || state.runtime.halted
    || !Number.isFinite(action.seconds) || action.seconds <= 0
    || !Number.isFinite(Math.fround(action.seconds)))) return state;
  const next = structuredClone(state);
  switch (action.type) {
    case 'toggle-pause': next.runtime!.paused = !next.runtime!.paused; break;
    case 'previous': navigate(next, true); break;
    case 'next': navigate(next, false); break;
    case 'natural-completion':
      next.runtime!.paused = next.runtime!.halted = true;
      navigate(next, false);
      break;
    case 'advance-time': {
      const current = next.runtime!;
      if (!Number.isFinite(current.rate) || current.rate <= 0) return state;
      const maximumFloat = 3.4028234663852886e38;
      current.time = Math.fround(Math.min(maximumFloat, current.time + Math.fround(action.seconds) * Math.fround(current.rate)));
      const duration = current.items.find(item => item.id === next.activeItemId)?.duration ?? 0;
      if (Number.isFinite(duration) && duration > 0 && current.time >= duration) {
        current.paused = current.halted = true;
        navigate(next, false);
      }
      break;
    }
  }
  return next;
}

export { formatRuntimeTime } from './sessionTime.ts';
