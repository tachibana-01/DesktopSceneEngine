import type { MockSource, SessionItem, SessionPlaylist, SessionProjection } from './sessionTypes.ts';
import { defaultPresentation } from './sessionInspectorDefaults.ts';

export type EntityAction =
  | { type: 'select-playlist' | 'select-item' | 'play-playlist' | 'play-item' | 'delete-playlist' | 'delete-item'; id: string }
  | { type: 'add-playlist' | 'toggle-pause' }
  | { type: 'add-media'; sources: readonly MockSource[] }
  | { type: 'set-playlist-enabled' | 'set-item-enabled'; id: string; enabled: boolean }
  | { type: 'move-playlist' | 'move-item'; id: string; direction: -1 | 1 }
  | { type: 'rename-playlist' | 'rename-item'; id: string; alias: string };

export function findPlaylist(state: SessionProjection, id: string | null): SessionPlaylist | undefined {
  return state.project.playlists.find(playlist => playlist.id === id);
}
export function findItem(state: SessionProjection, id: string | null): { playlist: SessionPlaylist; item: SessionItem } | undefined {
  for (const playlist of state.project.playlists) {
    const item = playlist.items.find(candidate => candidate.id === id);
    if (item) return { playlist, item };
  }
  return undefined;
}
export function itemPlayable(playlist: SessionPlaylist | undefined, item: SessionItem | undefined): boolean {
  return Boolean(playlist?.enabled && item?.enabled && playlist.items.some(candidate => candidate.id === item.id));
}
export function playlistPlayable(playlist: SessionPlaylist | undefined): boolean {
  return Boolean(playlist?.enabled && playlist.items.some(item => item.enabled));
}
function stop(state: SessionProjection): void {
  state.activePlaylistId = state.activeItemId = null;
  state.runtime = null;
}
function bind(state: SessionProjection, playlist: SessionPlaylist, item: SessionItem): void {
  state.activePlaylistId = playlist.id;
  state.activeItemId = item.id;
  state.runtime = { time: 0, paused: false, halted: false, rate: item.kind === 'video' ? item.rate : 1,
    volume: Math.fround(Math.fround(playlist.masterVolume) * (item.kind === 'video' ? item.mediaGain : 1)), items: structuredClone(playlist.items) };
}
function warn(state: SessionProjection, title: string, message: string): void {
  state.feedback = { level: 'warning', title, message };
}
function synchronizeWarning(state: SessionProjection, operation: 'add' | 'remove', stopped = false): void {
  warn(state, 'Unable to synchronize playback', stopped
    ? 'Playback was stopped because the updated playlist could not be synchronized.'
    : operation === 'add'
      ? 'Newly added media could not be synchronized with active playback. It will be available the next time the playlist is played.'
      : 'Playlist changes could not be synchronized with active playback. They will be applied the next time the playlist is played.');
}
// Models MainWindow::rebuildActiveProjection(RestoreCurrentItem). A failed
// synchronization never rolls back the authored mutation that preceded it.
function restoreCurrent(state: SessionProjection, operation: 'add' | 'remove'): void {
  if (!state.runtime || !state.activeItemId) return;
  const current = findItem(state, state.activeItemId);
  if (!current) { stop(state); synchronizeWarning(state, operation, true); return; }
  const fault = state.faults.playback;
  if (fault === 'rebuild-prepare' || fault === 'rebuild-bind' || fault === 'rollback-seek') {
    // Prepare failure retains the old projection. Bind failure rolls back to
    // that old projection, retaining its complete (possibly stale) scene set.
    // A failed rollback seek leaves it at zero; Player still reports Unchanged
    // so the caller emits its synchronization warning, not a position warning.
    if (fault === 'rollback-seek') state.runtime.time = 0;
    synchronizeWarning(state, operation);
    return;
  }
  if (fault === 'rollback-bind') {
    stop(state); synchronizeWarning(state, operation); return;
  }
  state.runtime = { ...state.runtime, items: structuredClone(current.playlist.items) };
  if (fault === 'restore-seek') {
    state.runtime.time = 0;
    warn(state, 'Playlist updated', 'Playlist updated, but the previous playback position could not be restored.');
  }
}
function freshId(state: SessionProjection, kind: 'playlist' | 'item'): string {
  let candidate: string;
  do { candidate = `mock-${kind}-${state.nextEntityOrdinal++}`; }
  while (findPlaylist(state, candidate) || findItem(state, candidate));
  return candidate;
}
function spreadsheetSuffix(value: string): string {
  let ordinal = BigInt(value);
  let suffix = '';
  do { suffix = String.fromCharCode(65 + Number(ordinal % 26n)) + suffix; ordinal = ordinal / 26n - 1n; }
  while (ordinal >= 0n);
  return suffix;
}
function start(state: SessionProjection, playlist: SessionPlaylist, item: SessionItem): void {
  if (state.faults.playback === 'start-bind') {
    // The existing manual Play rollback restores pause/rate, but does not seek
    // to the previous position. Do not promise stronger atomicity in the UI.
    if (state.runtime) state.runtime.time = 0;
    warn(state, 'Unable to play media', 'Media file could not be opened.');
    return;
  }
  bind(state, playlist, item);
  state.selectedPlaylistId = playlist.id;
  state.selectedItemId = item.id;
}

/** UI-only projection of existing Player commands, not a new product domain. */
export function entityTransition(state: SessionProjection, action: EntityAction): SessionProjection {
  if (state.closed) return state;
  const next = structuredClone(state);
  switch (action.type) {
    case 'move-playlist':
    case 'move-item': {
      if (action.direction !== -1 && action.direction !== 1) return state;
      const entries = action.type === 'move-playlist' ? next.project.playlists : findItem(next, action.id)?.playlist.items;
      const index = entries?.findIndex(entry => entry.id === action.id) ?? -1;
      if (!entries || index < 0 || index + action.direction < 0 || index + action.direction >= entries.length) return state;
      // WorkspaceModel swaps authored entries only. Existing Engine projection
      // and sequence history are retained; later navigation resolves stable IDs
      // against the latest authored order.
      const destination = index + action.direction;
      [entries[index], entries[destination]] = [entries[destination], entries[index]];
      next.document.revision++;
      return next;
    }
    case 'select-playlist': {
      if (!findPlaylist(next, action.id)) return state;
      next.selectedPlaylistId = action.id;
      next.selectedItemId = null;
      return next;
    }
    case 'select-item': {
      const target = findItem(next, action.id);
      if (!target) return state;
      next.selectedPlaylistId = target.playlist.id;
      next.selectedItemId = target.item.id;
      return next;
    }
    case 'add-playlist': {
      const playlist: SessionPlaylist = {
        id: freshId(next, 'playlist'), alias: `Playlist ${spreadsheetSuffix(next.nextPlaylistOrdinal)}`,
        enabled: true, masterVolume: 1, itemMode: 'list-loop', items: [],
      };
      next.nextPlaylistOrdinal = BigInt.asUintN(64, BigInt(next.nextPlaylistOrdinal) + 1n).toString();
      next.project.playlists.push(playlist);
      next.selectedPlaylistId = playlist.id;
      next.selectedItemId = null;
      next.document.revision++;
      return next;
    }
    case 'add-media': {
      const playlist = findPlaylist(next, next.selectedPlaylistId);
      if (action.sources.length === 0) return state;
      let added = false;
      const unsupported: string[] = [];
      for (const source of action.sources) {
        if (!source.kind) {
          unsupported.push(`Unsupported media file: ${source.source}`);
          continue;
        }
        // MainWindow validates the format before attempting addItem(parent).
        // A missing selected parent rejects supported additions, but cannot
        // suppress an unsupported-file warning from that earlier validation.
        if (!playlist || !source.source.trim()) continue;
        const filename = source.source.replaceAll('\\', '/').split('/').at(-1) ?? source.source;
        const stem = filename.replace(/\.[^.]*$/, '') || filename;
        playlist.items.push({
          id: freshId(next, 'item'), alias: stem, source: source.source, kind: source.kind,
          enabled: true, duration: source.kind === 'image' ? 10 : source.duration, rate: 1, mediaGain: 1, presentation: defaultPresentation(),
        });
        next.document.revision++;
        added = true;
      }
      if (unsupported.length) warn(next, 'Unsupported Media', unsupported.join('\n'));
      const importFeedback = next.feedback;
      const warnings: SessionProjection['feedbackQueue'] = unsupported.map(message=>({level:'warning',title:'Unsupported Media',message}));
      if (added && playlist && next.runtime && next.activePlaylistId === playlist.id) restoreCurrent(next, 'add');
      if(next.feedback&&next.feedback!==importFeedback)warnings.push({...next.feedback});
      if(warnings.length)next.feedbackQueue=warnings;
      if (unsupported.length && next.feedback && next.feedback !== importFeedback) {
        // Preserve the accepted aggregate diagnostic for callers. UI delivery
        // uses feedbackQueue in production order, one acknowledgement at a time.
        next.feedback = { ...next.feedback, message: `Unsupported Media\n${unsupported.join('\n')}\n\n${next.feedback.message}` };
      }
      return added || unsupported.length ? next : state;
    }
    case 'play-playlist': {
      const playlist = findPlaylist(next, action.id);
      if (!playlist) return state;
      if (playlist.items.length === 0) {
        next.feedback = { level: 'info', title: 'Empty Playlist', message: 'This playlist has no items to play.' };
        return next;
      }
      if (!playlistPlayable(playlist)) return state;
      const item = playlist.items.find(candidate => candidate.enabled)!;
      start(next, playlist, item);
      return next;
    }
    case 'play-item': {
      const target = findItem(next, action.id);
      if (!target || target.playlist.id !== next.selectedPlaylistId || !itemPlayable(target.playlist, target.item)) return state;
      start(next, target.playlist, target.item);
      return next;
    }
    case 'set-playlist-enabled': {
      const playlist = findPlaylist(next, action.id);
      if (!playlist || playlist.enabled === action.enabled) return state;
      if (!action.enabled && next.activePlaylistId === playlist.id) stop(next);
      playlist.enabled = action.enabled;
      next.document.revision++;
      return next;
    }
    case 'set-item-enabled': {
      const target = findItem(next, action.id);
      // The existing entity UI disables this command when its parent is disabled.
      if (!target || !target.playlist.enabled || target.item.enabled === action.enabled) return state;
      if (!action.enabled && next.activeItemId === target.item.id) stop(next);
      target.item.enabled = action.enabled;
      next.document.revision++;
      return next;
    }
    case 'rename-playlist':
    case 'rename-item': {
      const target = action.type === 'rename-playlist' ? findPlaylist(next, action.id) : findItem(next, action.id)?.item;
      if (!target) return state;
      // Player validates using trimmed text, but stores the original alias.
      if (!action.alias.trim()) { warn(next, 'Invalid Name', 'Name cannot be empty.'); return next; }
      if (target.alias === action.alias) return state;
      target.alias = action.alias;
      next.document.revision++;
      return next;
    }
    case 'delete-playlist': {
      const index = next.project.playlists.findIndex(playlist => playlist.id === action.id);
      if (index < 0) return state;
      const replacement = next.project.playlists[index + 1] ?? next.project.playlists[index - 1];
      if (next.activePlaylistId === action.id) stop(next);
      if (next.selectedPlaylistId === action.id) {
        next.selectedPlaylistId = replacement?.id ?? null;
        next.selectedItemId = null;
      }
      next.project.playlists.splice(index, 1);
      if (!next.project.playlists.length) {
        next.selectedPlaylistId = next.selectedItemId = null;
        stop(next);
      }
      next.document.revision++;
      return next;
    }
    case 'delete-item': {
      const target = findItem(next, action.id);
      if (!target) return state;
      const synchronizes = Boolean(next.runtime && next.activePlaylistId === target.playlist.id);
      const removesCurrent = synchronizes && next.activeItemId === action.id;
      let successor: SessionItem | undefined;
      if (removesCurrent && target.playlist.enabled) {
        const eligible = target.playlist.items.filter(item => item.enabled);
        const index = eligible.findIndex(item => item.id === action.id);
        if (index >= 0 && eligible.length > 1) successor = eligible[(index + 1) % eligible.length];
      }
      target.playlist.items.splice(target.playlist.items.findIndex(item => item.id === action.id), 1);
      if (next.selectedItemId === action.id) next.selectedItemId = null;
      next.document.revision++;
      if (!synchronizes) return next;
      if (removesCurrent) {
        if (!successor || !itemPlayable(target.playlist, successor)) stop(next);
        else if (['successor-bind', 'rebuild-prepare', 'rebuild-bind', 'rollback-bind'].includes(next.faults.playback)) {
          stop(next); synchronizeWarning(next, 'remove', true);
        } else bind(next, target.playlist, successor);
      } else if (!target.playlist.items.length) stop(next);
      else restoreCurrent(next, 'remove');
      return next;
    }
    case 'toggle-pause':
      if (!next.runtime || next.runtime.halted) return state;
      next.runtime.paused = !next.runtime.paused;
      return next;
  }
}
