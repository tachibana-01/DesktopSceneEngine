import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createSessionFixture, mockSources } from '../src/sessionFixtures.ts';
import { entityTransition as move, findItem, findPlaylist, itemPlayable, playlistPlayable } from '../src/sessionEntities.ts';

const checks = [];
function check(name, test) {
  try { test(); checks.push({ name, pass: true }); }
  catch (error) { checks.push({ name, pass: false, error: error.message }); }
}
const fixture = () => createSessionFixture('normal');
const apply = (state, type, fields = {}) => move(state, { type, ...fields });
const revision = state => state.document.revision;
const identities = state => [state.selectedPlaylistId, state.selectedItemId, state.activePlaylistId, state.activeItemId];
function freeze(value) {
  if (value && typeof value === 'object' && !Object.isFrozen(value)) {
    Object.freeze(value); for (const child of Object.values(value)) freeze(child);
  }
  return value;
}
check('Single selection preserves independent active identities; same-ID model selection clears Item', () => {
  const initial = fixture();
  const selected = apply(initial, 'select-item', { id: 'b-image' });
  assert.deepEqual(identities(selected), ['playlist-b', 'b-image', 'playlist-a', 'a-opening']);
  const parent = apply(selected, 'select-playlist', { id: 'playlist-b' });
  assert.deepEqual(identities(parent), ['playlist-b', null, 'playlist-a', 'a-opening']);
  assert.equal(revision(parent), revision(initial)); assert.deepEqual(parent.runtime, initial.runtime);
});
check('Pure transitions accept frozen inputs and retain independent same-source occurrences', () => {
  const initial = freeze(fixture()); const before = JSON.stringify(initial);
  const next = apply(initial, 'add-media', { sources: [mockSources[0], mockSources[0]] });
  assert.equal(JSON.stringify(initial), before); assert.equal(next.project.playlists[0].items.length, 5);
  const [first, second] = next.project.playlists[0].items.slice(-2);
  assert.equal(first.source, second.source); assert.notEqual(first.id, second.id);
  assert.equal(revision(next), revision(initial) + 2); assert.deepEqual(identities(next), identities(initial));
});
check('Manual play synchronizes four identities and starts fresh with target authored rate', () => {
  const initial = fixture(); initial.runtime.paused = true; initial.runtime.time = 42; initial.runtime.rate = 1.8;
  const next = apply(initial, 'play-item', { id: 'a-scene' });
  assert.deepEqual(identities(next), ['playlist-a', 'a-scene', 'playlist-a', 'a-scene']);
  assert.deepEqual([next.runtime.time, next.runtime.paused, next.runtime.rate], [0, false, 1.2]);
  assert.equal(next.runtime.items.length, 3); assert.equal(revision(next), revision(initial));
  const other = apply(next, 'play-playlist', { id: 'playlist-b' });
  assert.deepEqual(identities(other), ['playlist-b', 'b-opening', 'playlist-b', 'b-opening']);
});
check('Playlist Play skips disabled first Item, while empty has info and unavailable stays inert', () => {
  const initial = fixture(); initial.project.playlists[0].items[0].enabled = false;
  const next = apply(initial, 'play-playlist', { id: 'playlist-a' }); assert.equal(next.activeItemId, 'a-scene');
  assert.deepEqual(apply(initial, 'play-item', { id: 'a-disabled' }), initial);
  assert.deepEqual(apply(initial, 'play-playlist', { id: 'playlist-d' }), initial);
  const empty = apply(initial, 'play-playlist', { id: 'playlist-c' });
  assert.equal(empty.feedback.title, 'Empty Playlist'); assert.deepEqual(empty.runtime, initial.runtime);
  assert.equal(playlistPlayable(findPlaylist(initial, 'playlist-d')), false);
  assert.equal(itemPlayable(findPlaylist(initial, 'playlist-a'), findItem(initial, 'b-image').item), false);
});
check('Start bind failure restores old scene at zero with old pause/rate, without authored mutation', () => {
  const initial = fixture(); initial.faults.playback = 'start-bind'; initial.runtime.paused = true; initial.runtime.rate = 1.7;
  const next = apply(initial, 'play-item', { id: 'a-scene' });
  assert.deepEqual(identities(next), identities(initial));
  assert.deepEqual([next.runtime.time, next.runtime.paused, next.runtime.rate], [0, true, 1.7]);
  assert.equal(next.feedback.title, 'Unable to play media'); assert.equal(revision(next), revision(initial));
  const empty = createSessionFixture('empty-project'); empty.project = initial.project; empty.faults.playback = 'start-bind';
  assert.equal(apply(empty, 'play-item', { id: 'a-scene' }).runtime, null);
});
check('Active disable stops once, keeps selected identities and child flags, enable never restarts', () => {
  const initial = apply(fixture(), 'select-item', { id: 'b-image' });
  const next = apply(initial, 'set-playlist-enabled', { id: 'playlist-a', enabled: false });
  assert.deepEqual(identities(next), ['playlist-b', 'b-image', null, null]); assert.equal(next.runtime, null);
  assert.deepEqual(next.project.playlists[0].items.map(value => value.enabled), [true, true, false]);
  assert.equal(revision(next), revision(initial) + 1);
  assert.equal(revision(apply(next, 'set-playlist-enabled', { id: 'playlist-a', enabled: false })), revision(next));
  const enabled = apply(next, 'set-playlist-enabled', { id: 'playlist-a', enabled: true }); assert.equal(enabled.runtime, null);
});
check('Inactive toggles preserve runtime; parent-disabled Item switch is blocked; active Item disable stops', () => {
  const initial = fixture(); const disabled = apply(initial, 'set-item-enabled', { id: 'a-scene', enabled: false });
  assert.deepEqual(disabled.runtime, initial.runtime); assert.equal(disabled.activeItemId, 'a-opening');
  assert.deepEqual(apply(initial, 'set-item-enabled', { id: 'd-scene', enabled: false }), initial);
  const stopped = apply(initial, 'set-item-enabled', { id: 'a-opening', enabled: false });
  assert.equal(stopped.runtime, null); assert.equal(stopped.activePlaylistId, null); assert.equal(stopped.selectedItemId, 'a-opening');
});
check('Playlist delete uses next/previous selection, preserves unrelated selection, and active delete stops', () => {
  const initial = apply(fixture(), 'select-item', { id: 'b-image' });
  const unrelated = apply(initial, 'delete-playlist', { id: 'playlist-c' }); assert.equal(unrelated.selectedItemId, 'b-image');
  const selected = apply(initial, 'delete-playlist', { id: 'playlist-b' });
  assert.equal(selected.selectedPlaylistId, 'playlist-c'); assert.equal(selected.selectedItemId, null); assert.equal(selected.activeItemId, 'a-opening');
  const active = apply(initial, 'delete-playlist', { id: 'playlist-a' });
  assert.deepEqual(identities(active), ['playlist-b', 'b-image', null, null]); assert.equal(active.runtime, null);
  const last = apply(apply(initial, 'select-playlist', { id: 'playlist-d' }), 'delete-playlist', { id: 'playlist-d' });
  assert.equal(last.selectedPlaylistId, 'playlist-c');
});
check('Final Playlist deletion establishes empty project; future naming does not recycle', () => {
  let state = createSessionFixture('empty-project'); state = apply(state, 'add-playlist');
  assert.equal(state.project.playlists[0].alias, 'Playlist A'); const id = state.selectedPlaylistId;
  const empty = apply(state, 'delete-playlist', { id });
  assert.equal(empty.project.playlists.length, 0); assert.deepEqual(identities(empty), [null, null, null, null]);
  assert.equal(empty.runtime, null); assert.equal(apply(empty, 'add-playlist').project.playlists[0].alias, 'Playlist B');
  assert.deepEqual(apply(empty, 'delete-playlist', { id }), empty);
});
check('Monotonic naming crosses Z/AA and Add selects new Playlist without activating it', () => {
  let state = fixture(); const oldActive = state.activeItemId; state.nextPlaylistOrdinal = '25';
  state = apply(state, 'add-playlist'); assert.equal(state.project.playlists.at(-1).alias, 'Playlist Z');
  state = apply(state, 'delete-playlist', { id: state.selectedPlaylistId });
  state = apply(state, 'add-playlist'); assert.equal(state.project.playlists.at(-1).alias, 'Playlist AA');
  assert.equal(state.activeItemId, oldActive); assert.equal(state.selectedItemId, null);
});
check('Rename preserves whitespace, accepts duplicate aliases, retains IDs/source, and only actual changes dirty', () => {
  const initial = fixture(); const renamed = apply(initial, 'rename-item', { id: 'a-opening', alias: '  🌙 Opening  ' });
  assert.equal(findItem(renamed, 'a-opening').item.alias, '  🌙 Opening  ');
  assert.equal(findItem(renamed, 'a-opening').item.source, findItem(initial, 'a-opening').item.source);
  assert.equal(revision(renamed), revision(initial) + 1);
  assert.equal(revision(apply(renamed, 'rename-item', { id: 'a-opening', alias: '  🌙 Opening  ' })), revision(renamed));
  const duplicate = apply(initial, 'rename-playlist', { id: 'playlist-b', alias: 'Playlist A' }); assert.equal(duplicate.project.playlists[1].alias, 'Playlist A');
  const invalid = apply(initial, 'rename-playlist', { id: 'playlist-b', alias: ' \t ' });
  assert.equal(revision(invalid), revision(initial)); assert.equal(invalid.feedback.title, 'Invalid Name');
});
check('Delete current uses fresh authored successor, skips disabled, wraps, starts fresh and preserves unrelated selection', () => {
  let state = fixture(); state = apply(state, 'play-item', { id: 'a-scene' });
  state = apply(state, 'select-item', { id: 'a-disabled' }); state.runtime.time = 7; state.runtime.paused = true; state.runtime.rate = 1.7;
  const next = apply(state, 'delete-item', { id: 'a-scene' });
  assert.deepEqual(identities(next), ['playlist-a', 'a-disabled', 'playlist-a', 'a-opening']);
  assert.deepEqual([next.runtime.time, next.runtime.paused, next.runtime.rate], [0, false, 1]);
  assert.equal(revision(next), revision(state) + 1);
  const reordered = fixture(); reordered.project.playlists[0].items.reverse();
  const after = apply(reordered, 'delete-item', { id: 'a-opening' }); assert.equal(after.activeItemId, 'a-scene');
});
check('Current deletion with no eligible successor stops; successor bind failure never skips later eligible Item', () => {
  const initial = fixture(); initial.project.playlists[0].items[1].enabled = false;
  const none = apply(initial, 'delete-item', { id: 'a-opening' });
  assert.equal(none.runtime, null); assert.equal(none.feedback, null); assert.equal(none.activePlaylistId, null);
  const bad = fixture(); bad.faults.playback = 'successor-bind'; bad.project.playlists[0].items[2].enabled = true;
  const failed = apply(bad, 'delete-item', { id: 'a-opening' });
  assert.equal(failed.runtime, null); assert.equal(failed.feedback.title, 'Unable to synchronize playback');
  assert.equal(failed.project.playlists[0].items.length, 2); assert.equal(failed.selectedItemId, null);
});
check('Delete noncurrent same Playlist restores current time/pause/rate; another Playlist never rebinds', () => {
  const initial = fixture(); initial.runtime.paused = true; initial.runtime.rate = 1.7;
  const next = apply(initial, 'delete-item', { id: 'a-scene' });
  assert.deepEqual([next.runtime.time, next.runtime.paused, next.runtime.rate], [8, true, 1.7]);
  assert.equal(next.activeItemId, 'a-opening'); assert.equal(next.runtime.items.length, 2);
  const other = apply(apply(initial, 'select-item', { id: 'b-image' }), 'delete-item', { id: 'b-image' });
  assert.deepEqual(other.runtime, initial.runtime); assert.equal(other.selectedPlaylistId, 'playlist-b'); assert.equal(other.selectedItemId, null);
});
check('Rebuild prepare/bind/seek/rollback faults keep authored changes while projecting exact fallback outcomes', () => {
  for (const fault of ['rebuild-prepare', 'rebuild-bind', 'restore-seek', 'rollback-bind', 'rollback-seek']) {
    const initial = fixture(); initial.runtime.paused = true; initial.runtime.rate = 1.7; initial.faults.playback = fault;
    const next = apply(initial, 'delete-item', { id: 'a-scene' });
    assert.equal(findItem(next, 'a-scene'), undefined); assert.equal(revision(next), revision(initial) + 1);
    assert.equal(next.faults.playback, fault); assert.ok(next.feedback);
    if (fault === 'rollback-bind') { assert.equal(next.runtime, null); assert.equal(next.activeItemId, null); }
    else {
      assert.deepEqual([next.runtime.time, next.runtime.paused, next.runtime.rate], [fault === 'restore-seek' || fault === 'rollback-seek' ? 0 : 8, true, 1.7]);
      assert.equal(next.runtime.items.length, fault === 'restore-seek' ? 2 : initial.runtime.items.length);
    }
  }
});
check('Mixed Add Media reports unsupported input, keeps accepted additions and never changes selection', () => {
  const initial = fixture(); const next = apply(initial, 'add-media', { sources: mockSources });
  assert.equal(next.project.playlists[0].items.length, 6); assert.equal(revision(next), revision(initial) + 3);
  assert.equal(next.feedback.title, 'Unsupported Media'); assert.deepEqual(identities(next), identities(initial));
  assert.equal(next.runtime.time, 8); assert.equal(next.runtime.items.length, 6);
});
check('Pause acts on loaded runtime only and neither playback nor invalid actions dirty', () => {
  const initial = fixture(); const paused = apply(initial, 'toggle-pause');
  assert.equal(paused.runtime.paused, true); assert.equal(revision(paused), revision(initial));
  const empty = createSessionFixture('empty-project'); assert.deepEqual(apply(empty, 'toggle-pause'), empty);
  assert.deepEqual(apply(initial, 'select-item', { id: 'missing' }), initial);
  assert.deepEqual(apply(empty, 'add-media', { sources: mockSources.filter(source => source.kind) }), empty);
});
check('Play Item requires selected parent and does not invent cross-parent command behavior', () => {
  const initial = fixture();
  assert.deepEqual(apply(initial, 'play-item', { id: 'b-image' }), initial);
  const selected = apply(initial, 'select-item', { id: 'b-image' });
  const started = apply(selected, 'play-item', { id: 'b-image' });
  assert.deepEqual(identities(started), ['playlist-b', 'b-image', 'playlist-b', 'b-image']);
});
check('All target-rebuild bind faults stop deletion at the chosen successor without retrying another Item', () => {
  for (const fault of ['successor-bind', 'rebuild-prepare', 'rebuild-bind', 'rollback-bind']) {
    const initial = fixture(); initial.faults.playback = fault;
    const next = apply(initial, 'delete-item', { id: 'a-opening' });
    assert.equal(next.runtime, null); assert.equal(next.activeItemId, null);
    assert.equal(next.project.playlists[0].items.length, 2); assert.ok(next.feedback);
    assert.equal(revision(next), revision(initial) + 1);
  }
});
check('Add Media validates unsupported formats even with no selected Playlist, without authoring objects', () => {
  const empty = createSessionFixture('empty-project');
  const supported = apply(empty, 'add-media', { sources: mockSources.filter(source => source.kind) });
  assert.deepEqual(supported, empty);
  const unsupported = apply(empty, 'add-media', { sources: [mockSources[3]] });
  assert.equal(unsupported.feedback?.title, 'Unsupported Media');
  assert.match(unsupported.feedback?.message ?? '', /unsupported\.txt/);
  assert.deepEqual(unsupported.project, empty.project); assert.deepEqual(identities(unsupported), identities(empty));
  assert.deepEqual(unsupported.document, empty.document); assert.equal(unsupported.runtime, null);
  const mixed = apply(empty, 'add-media', { sources: mockSources });
  assert.equal(mixed.project.playlists.length, 0); assert.equal(revision(mixed), revision(empty));
  assert.equal(mixed.feedback?.title, 'Unsupported Media');
});
check('Mixed import keeps both unsupported and projection synchronization warnings readable', () => {
  for (const fault of ['rebuild-prepare', 'rebuild-bind', 'restore-seek', 'rollback-bind', 'rollback-seek']) {
    const initial = fixture(); initial.faults.playback = fault; initial.runtime.paused = true; initial.runtime.rate = 1.7;
    const next = apply(initial, 'add-media', { sources: [mockSources[0], mockSources[3]] });
    const readable = `${next.feedback?.title ?? ''}\n${next.feedback?.message ?? ''}`;
    assert.match(readable, /Unsupported Media/); assert.match(readable, /unsupported\.txt/);
    assert.match(readable, fault === 'restore-seek' ? /previous playback position could not be restored/ : /could not be synchronized/);
    assert.equal(revision(next), revision(initial) + 1); assert.equal(next.project.playlists[0].items.length, 4);
    assert.deepEqual(identities(next).slice(0, 2), identities(initial).slice(0, 2));
    if (fault === 'rollback-bind') { assert.equal(next.runtime, null); assert.equal(next.activeItemId, null); }
    else {
      assert.deepEqual([next.runtime.time, next.runtime.paused, next.runtime.rate], [fault === 'restore-seek' || fault === 'rollback-seek' ? 0 : 8, true, 1.7]);
      assert.equal(next.runtime.items.length, fault === 'restore-seek' ? 4 : initial.runtime.items.length);
    }
  }
});
const failed = checks.filter(check => !check.pass);
const result = { suite: 'session-entities', passed: checks.length - failed.length, failed: failed.length, checks };
const stage = process.argv.find(value => value.startsWith('--evidence='))?.split('=')[1] ?? 'green';
if (!['red', 'green', 'boundary-red'].includes(stage)) throw new Error('Evidence stage must be red, green or boundary-red.');
const directory = fileURLToPath(new URL('../node_modules/.cache/session-phase-a/', import.meta.url));
mkdirSync(directory, { recursive: true });
writeFileSync(`${directory}/entities-${stage}.json`, JSON.stringify(result, null, 2));
writeFileSync(`${directory}/entities-${stage}.log`, checks.map(value => `${value.pass ? 'PASS' : 'FAIL'} ${value.name}${value.error ? `\n${value.error}` : ''}`).join('\n'));
console.log(JSON.stringify(result, null, 2));
process.exitCode = failed.length ? 1 : 0;
