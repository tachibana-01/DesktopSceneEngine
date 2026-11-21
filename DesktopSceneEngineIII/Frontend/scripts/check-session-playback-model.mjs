import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createSessionFixture } from '../src/sessionFixtures.ts';
import { entityTransition } from '../src/sessionEntities.ts';
import { inspectorTransition } from '../src/sessionInspectorModel.ts';
import { continueDocumentIntent, loadOpenCandidate, saveToMockPath } from '../src/sessionDocumentMock.ts';
import { controllerCurrent, createController } from '../src/sessionPlaybackSequence.ts';
import { afterEntityPlayback, formatRuntimeTime, playbackTransition, rebasePlaybackToActive } from '../src/sessionPlayback.ts';

const checks = [];
function check(name, test) {
  try { test(); checks.push({ name, pass: true }); }
  catch (error) { checks.push({ name, pass: false, error: error.message }); }
}
const fixture = () => createSessionFixture('normal');
const play = (state, type, fields = {}) => playbackTransition(state, { type, ...fields });
const entity = (state, type, id) => {
  const action = { type, id };
  return afterEntityPlayback(state, entityTransition(state, action), action);
};
const selected = state => [state.selectedPlaylistId, state.selectedItemId];
const active = state => [state.activePlaylistId, state.activeItemId];
const revision = state => state.document.revision;
const runtime = state => [state.runtime?.time, state.runtime?.paused, state.runtime?.halted, state.runtime?.rate];
function freeze(value) {
  if (value && typeof value === 'object' && !Object.isFrozen(value)) {
    Object.freeze(value); for (const child of Object.values(value)) freeze(child);
  }
  return value;
}
function cross() {
  let state = play(fixture(), 'project-mode', { mode: 'list-loop' });
  state = play(state, 'next');
  return state;
}

check('New production Project defaults Repeat Current; fixture controller synchronizes authored mode', () => {
  const state = fixture();
  assert.equal(state.project.playlistMode, 'repeat-current');
  assert.equal(state.sequence.playlistMode, 'repeat-current');
  assert.deepEqual(controllerCurrent(state.sequence), { playlistId: 'playlist-a', itemId: 'a-opening' });
});
check('Immutable policy change authors one revision without runtime/selection/controller restart', () => {
  const initial = freeze(fixture()); const before = JSON.stringify(initial);
  const next = play(initial, 'project-mode', { mode: 'list-loop' });
  assert.equal(next.project.playlistMode, 'list-loop'); assert.equal(revision(next), revision(initial) + 1);
  assert.deepEqual(next.runtime, initial.runtime); assert.deepEqual(selected(next), selected(initial));
  assert.deepEqual(next.sequence, initial.sequence); assert.equal(JSON.stringify(initial), before);
  assert.equal(revision(play(next, 'project-mode', { mode: 'list-loop' })), revision(next));
});
check('Independent per-Playlist policy edits change selected authored scope only', () => {
  const initial = fixture(); initial.selectedPlaylistId = 'playlist-b'; initial.selectedItemId = 'b-image';
  const next = play(initial, 'playlist-item-mode', { id: 'playlist-b', mode: 'random' });
  assert.equal(next.project.playlists[1].itemMode, 'random'); assert.equal(next.project.playlists[0].itemMode, 'list-loop');
  assert.deepEqual(active(next), active(initial)); assert.deepEqual(selected(next), selected(initial)); assert.deepEqual(next.runtime, initial.runtime);
  assert.equal(revision(next), revision(initial) + 1);
  assert.equal(revision(play(next, 'playlist-item-mode', { id: 'playlist-b', mode: 'random' })), revision(next));
  assert.deepEqual(play(next, 'playlist-item-mode', { id: 'missing', mode: 'random' }), next);
});
check('Next uses active runtime, filters disabled Items, preserves different Playlist/Item selection', () => {
  const initial = fixture(); initial.selectedPlaylistId = 'playlist-b'; initial.selectedItemId = 'b-image';
  const next = play(initial, 'next');
  assert.deepEqual(active(next), ['playlist-a', 'a-scene']); assert.deepEqual(selected(next), selected(initial));
  assert.deepEqual(runtime(next).slice(0, 3), [0, false, false]); assert.equal(next.runtime.rate, 1.2);
  assert.equal(revision(next), revision(initial)); assert.deepEqual(active(play(next, 'next')), ['playlist-a', 'a-opening']);
});
check('List Loop crosses playable Playlists and wraps Project while preserving selection', () => {
  const initial = cross(); assert.deepEqual(active(initial), ['playlist-a', 'a-scene']);
  let next = play(initial, 'next'); assert.deepEqual(active(next), ['playlist-b', 'b-opening']);
  next = play(next, 'next'); assert.deepEqual(active(next), ['playlist-b', 'b-image']);
  next = play(next, 'next'); assert.deepEqual(active(next), ['playlist-a', 'a-opening']);
  assert.deepEqual(selected(next), selected(initial));
});
check('Previous traverses invocation history and forward restoration rather than editor index', () => {
  let state = cross(); state = play(state, 'next');
  state.selectedPlaylistId = 'playlist-d'; state.selectedItemId = 'd-scene';
  const previous = play(state, 'previous');
  assert.deepEqual(active(previous), ['playlist-a', 'a-scene']); assert.deepEqual(selected(previous), selected(state));
  assert.deepEqual(active(play(previous, 'next')), ['playlist-b', 'b-opening']);
  assert.deepEqual(active(play(fixture(), 'previous')), ['playlist-a', 'a-opening']);
});
check('Paused Previous/Next restart target Playing at zero; central pause preserves target/time', () => {
  const paused = play(fixture(), 'toggle-pause'); assert.equal(paused.runtime.paused, true);
  assert.equal(paused.runtime.time, 8); assert.equal(revision(paused), revision(fixture()));
  assert.equal(play(paused, 'toggle-pause').runtime.paused, false);
  const next = play(paused, 'next'); assert.deepEqual(runtime(next).slice(0, 3), [0, false, false]);
  const previous = play(play(next, 'toggle-pause'), 'previous');
  assert.deepEqual(active(previous), ['playlist-a', 'a-opening']); assert.deepEqual(runtime(previous).slice(0, 3), [0, false, false]);
});
check('Empty runtime transport is inert and never activates selected entity', () => {
  const initial = createSessionFixture('path-backed-clean'); initial.selectedPlaylistId = 'playlist-b'; initial.selectedItemId = 'b-image';
  for (const type of ['previous', 'next', 'toggle-pause', 'natural-completion']) assert.deepEqual(play(initial, type), initial);
  assert.equal(controllerCurrent(initial.sequence), null);
});
check('Item Repeat Current holds Item; outer Repeat Current re-enters same Playlist after Item round', () => {
  let state = play(fixture(), 'playlist-item-mode', { id: 'playlist-a', mode: 'repeat-current' });
  assert.deepEqual(active(play(state, 'next')), active(state));
  state = play(state, 'playlist-item-mode', { id: 'playlist-a', mode: 'list-loop' });
  state = play(state, 'next'); assert.equal(state.activeItemId, 'a-scene');
  assert.equal(play(state, 'next').activeItemId, 'a-opening');
});
check('Deterministic Random uses sampled round including replacement rather than fixed Next', () => {
  const initial = fixture(); initial.project.playlists[0].itemMode = 'random';
  initial.sequence = createController(42, [], [1, 1]); rebasePlaybackToActive(initial);
  const one = play(initial, 'next'); const two = play(one, 'next');
  assert.equal(one.activeItemId, 'a-scene'); assert.equal(two.activeItemId, 'a-scene');
  assert.deepEqual(play(initial, 'next'), one); assert.equal(revision(two), revision(initial));
});
check('Outer Random omits disabled and empty Playlists before sampling', () => {
  let state = fixture(); state.project.playlistMode = 'random'; state.sequence = createController(42, [1, 1], []); rebasePlaybackToActive(state);
  state = play(state, 'next'); state = play(state, 'next');
  assert.deepEqual(active(state), ['playlist-b', 'b-opening']);
});
check('Single playable Item repeats from zero; disabled parent and empty targets never play', () => {
  let state = fixture(); state.project.playlists[0].items[1].enabled = false; rebasePlaybackToActive(state);
  const next = play(state, 'next'); assert.deepEqual(active(next), active(state)); assert.equal(next.runtime.time, 0);
  state.project.playlists[0].enabled = false;
  const stopped = play(state, 'next'); assert.equal(stopped.runtime, null); assert.deepEqual(active(stopped), [null, null]);
  assert.deepEqual(selected(stopped), selected(state));
});
check('No playable target safe-stops, clears controller and keeps selection plus mute', () => {
  const initial = fixture(); initial.outputMuted = true;
  for (const playlist of initial.project.playlists) playlist.enabled = false;
  const next = play(initial, 'next');
  assert.equal(next.runtime, null); assert.deepEqual(active(next), [null, null]); assert.equal(controllerCurrent(next.sequence), null);
  assert.deepEqual(selected(next), selected(initial)); assert.equal(next.outputMuted, true); assert.equal(revision(next), revision(initial));
});
check('Natural completion advances through same controller and preserves editor selection', () => {
  const initial = cross(); initial.selectedPlaylistId = 'playlist-d'; initial.selectedItemId = 'd-scene';
  const next = play(initial, 'natural-completion'); assert.deepEqual(active(next), ['playlist-b', 'b-opening']);
  assert.deepEqual(selected(next), selected(initial)); assert.deepEqual(runtime(next).slice(0, 3), [0, false, false]);
  assert.equal(revision(next), revision(initial));
});
check('Time advances finite positive float delta times actual rate; pause freezes; duration hands off', () => {
  const initial = fixture(); initial.runtime.rate = 1.25;
  const next = play(initial, 'advance-time', { seconds: 0.5 }); assert.equal(next.runtime.time, 8.625);
  const paused = play(next, 'toggle-pause'); assert.deepEqual(play(paused, 'advance-time', { seconds: 1 }), paused);
  for (const seconds of [0, -1, NaN, Infinity]) assert.deepEqual(play(next, 'advance-time', { seconds }), next);
  const end = play(initial, 'advance-time', { seconds: 20 }); assert.equal(end.activeItemId, 'a-scene'); assert.equal(end.runtime.time, 0);
  assert.equal(revision(end), revision(initial));
});
check('Same-Playlist switch failure is silent, preserves old facts, rebases rather than skipping target', () => {
  const initial = fixture(); initial.faults.navigation = 'bind';
  const next = play(initial, 'next'); assert.deepEqual(next.runtime, initial.runtime); assert.deepEqual(active(next), active(initial));
  assert.equal(next.feedback, null); assert.deepEqual(controllerCurrent(next.sequence), controllerCurrent(initial.sequence));
  assert.deepEqual(active(play(next, 'next')), active(initial));
});
check('Previous failure uses the same silent switch or cross bind rollback paths and preserves selection', () => {
  let state = play(fixture(), 'next'); state.runtime.time = 31; state.faults.navigation = 'bind';
  const same = play(state, 'previous'); assert.deepEqual(same.runtime, state.runtime); assert.equal(same.feedback, null);
  assert.deepEqual(active(same), active(state)); assert.deepEqual(selected(same), selected(state));
  state = play(cross(), 'next'); state.runtime.time = 12; state.runtime.paused = true; state.faults.navigation = 'bind';
  const crossed = play(state, 'previous'); assert.deepEqual(active(crossed), active(state));
  assert.deepEqual(runtime(crossed), [0, true, false, 1]); assert.deepEqual(selected(crossed), selected(state));
  assert.equal(crossed.feedback.message, 'Media file could not be opened.');
});
check('Frozen runtime input and nested controller random/history data remain unchanged by navigation', () => {
  const initial = freeze(play(cross(), 'next')); const before = JSON.stringify(initial);
  const next = play(initial, 'previous'); assert.equal(next.activeItemId, 'a-scene');
  assert.equal(JSON.stringify(initial), before); assert.notEqual(next.sequence, initial.sequence);
});
check('Failed natural completion within Playlist stays paused/halted; Play cannot synthesize restart', () => {
  const initial = fixture(); initial.faults.navigation = 'bind'; initial.runtime.time = 23;
  const next = play(initial, 'natural-completion'); assert.equal(next.runtime.halted, true); assert.equal(next.runtime.paused, true);
  assert.equal(next.runtime.time, 23); assert.deepEqual(play(next, 'toggle-pause'), next);
  assert.deepEqual(play(next, 'advance-time', { seconds: 1 }), next); assert.equal(next.feedback, null);
  assert.deepEqual(play(next, 'natural-completion'), next, 'Halted Engine exposes only one completion fact until rebound');
  next.faults.navigation = 'none'; const recovered = play(next, 'next'); assert.equal(recovered.activeItemId, 'a-scene'); assert.equal(recovered.runtime.halted, false);
});
check('Cross-Playlist prepare failure preserves old runtime and warns Unable create sequence', () => {
  const initial = cross(); initial.faults.navigation = 'prepare'; initial.runtime.time = 12; initial.runtime.paused = true;
  const next = play(initial, 'next'); assert.deepEqual(next.runtime, initial.runtime); assert.deepEqual(active(next), active(initial));
  assert.equal(next.feedback.title, 'Unable to play media'); assert.equal(next.feedback.message, 'Unable to create a playback sequence.');
});
check('Cross-Playlist bind failure restores old snapshot at zero, pause/rate; no fallback target', () => {
  const initial = cross(); initial.faults.navigation = 'bind'; initial.runtime.time = 42; initial.runtime.paused = true; initial.runtime.rate = 1.7;
  const next = play(initial, 'next'); assert.deepEqual(active(next), active(initial));
  assert.deepEqual(runtime(next), [0, true, false, 1.7]); assert.deepEqual(next.runtime.items, initial.runtime.items);
  assert.equal(next.feedback.message, 'Media file could not be opened.'); assert.deepEqual(selected(next), selected(initial));
  assert.equal(revision(next), revision(initial));
});
check('Cross-Playlist rollback failure unconfigures but retains stale workspace IDs and rebased controller', () => {
  const initial = cross(); initial.faults.navigation = 'rollback';
  const next = play(initial, 'next'); assert.equal(next.runtime, null); assert.deepEqual(active(next), active(initial));
  assert.deepEqual(controllerCurrent(next.sequence), controllerCurrent(initial.sequence));
  assert.deepEqual(selected(next), selected(initial)); assert.equal(next.feedback.title, 'Unable to play media');
  const changedSelection = entity(next, 'select-playlist', 'playlist-b');
  assert.deepEqual(changedSelection.sequence, next.sequence, 'Selection cannot clear an existing failed-navigation controller');
});
check('Same-Playlist navigation uses existing projected rate/duration; cross fresh Play uses current authored values', () => {
  let initial = fixture(); initial.project.playlists[0].items[1].rate = 1.9; initial.project.playlists[0].items[1].duration = 2;
  const same = play(initial, 'next'); assert.equal(same.runtime.rate, 1.2);
  assert.equal(same.runtime.items.find(item => item.id === 'a-scene').duration, 107);
  initial = play(initial, 'project-mode', { mode: 'list-loop' }); initial.project.playlists[1].items[0].rate = 1.6;
  const other = play(play(initial, 'next'), 'next'); assert.equal(other.runtime.rate, 1.6);
});
check('Library explicit Play rebases shared runtime/history; invalid Play leaves controller unchanged', () => {
  let state = play(cross(), 'next'); state = entity(state, 'play-playlist', 'playlist-a');
  assert.deepEqual(controllerCurrent(state.sequence), { playlistId: 'playlist-a', itemId: 'a-opening' });
  assert.deepEqual(active(play(state, 'previous')), active(state));
  const denied = entity(state, 'play-playlist', 'playlist-d'); assert.deepEqual(denied.sequence, state.sequence);
});
check('Active Item deletion successor rebases; active disable clears controller; inactive edit leaves history', () => {
  const initial = cross(); const deleted = entity(initial, 'delete-item', 'a-scene');
  assert.equal(deleted.activeItemId, 'a-opening'); assert.deepEqual(controllerCurrent(deleted.sequence), { playlistId: 'playlist-a', itemId: 'a-opening' });
  const disabledAction = { type: 'set-item-enabled', id: 'a-opening', enabled: false };
  const disabled = afterEntityPlayback(deleted, entityTransition(deleted, disabledAction), disabledAction);
  assert.equal(controllerCurrent(disabled.sequence), null); assert.equal(disabled.runtime, null);
  const renamed = entityTransition(initial, { type: 'rename-item', id: 'b-opening', alias: 'Other' });
  assert.deepEqual(afterEntityPlayback(initial, renamed, { type: 'rename-item', id: 'b-opening', alias: 'Other' }).sequence, initial.sequence);
});
check('Failed Library Play preserves controller while resetting old halted runtime after rollback', () => {
  const initial = fixture(); initial.faults.playback = 'start-bind'; initial.runtime.halted = true; initial.runtime.paused = true;
  const next = entity(initial, 'play-item', 'a-scene');
  assert.deepEqual(next.sequence, initial.sequence); assert.equal(next.runtime.time, 0); assert.equal(next.runtime.halted, false);
});
// UI-PIC-001 supersedes Rail active-Master access with explicit PlaylistId editing.
check('Explicit Playlist A Master and selected B Inspector Master independently author their IDs', () => {
  const initial = fixture(); initial.selectedPlaylistId = 'playlist-b'; initial.selectedItemId = null;
  const row = inspectorTransition(initial, { type: 'playlist-master', id: 'playlist-a', value: 0.7 });
  assert.equal(row.project.playlists[0].masterVolume, 0.7); assert.equal(row.project.playlists[1].masterVolume, 1);
  assert.equal(row.runtime.volume, Math.fround(Math.fround(0.7))); assert.equal(revision(row), revision(initial) + 1);
  const inspector = inspectorTransition(row, { type: 'playlist-master', id: 'playlist-b', value: 0.2 });
  assert.equal(inspector.project.playlists[1].masterVolume, 0.2); assert.equal(inspector.runtime.volume, row.runtime.volume);
  assert.equal(revision(inspector), revision(row) + 1);
  assert.deepEqual(selected(inspector), selected(initial)); assert.deepEqual(active(inspector), active(initial));
  assert.equal(revision(inspectorTransition(row, { type: 'playlist-master', id: 'playlist-a', value: 0.7 })), revision(row));
  assert.deepEqual(inspectorTransition(row, { type: 'playlist-master', id: 'missing', value: 0.5 }), row);
  const empty = createSessionFixture('path-backed-clean'); empty.selectedPlaylistId = 'playlist-b';
  const authored = inspectorTransition(empty, { type: 'playlist-master', id: 'playlist-b', value: 0.5 });
  assert.equal(authored.project.playlists[1].masterVolume, 0.5); assert.equal(revision(authored), revision(empty) + 1);
  assert.equal(authored.runtime, null); assert.deepEqual(active(authored), active(empty)); assert.deepEqual(selected(authored), selected(empty));
});
check('Active gain updates non-BGM effective product; non-active same-source gain remains independent', () => {
  const initial = inspectorTransition(fixture(), { type: 'playlist-master', id: 'playlist-a', value: 0.7 });
  const activeGain = inspectorTransition(initial, { type: 'media-gain', id: 'a-opening', value: 0.5 });
  assert.equal(activeGain.runtime.volume, Math.fround(Math.fround(0.7) * 0.5));
  const inactiveGain = inspectorTransition(activeGain, { type: 'media-gain', id: 'b-opening', value: 0.2 });
  assert.equal(inactiveGain.runtime.volume, activeGain.runtime.volume); assert.equal(inactiveGain.project.playlists[0].items[0].mediaGain, 0.5);
});
check('Image playback uses gain factor one and runtime duration remains authored-projection snapshot', () => {
  let state = fixture(); state = entity(state, 'play-playlist', 'playlist-b'); state = play(state, 'next');
  assert.equal(state.activeItemId, 'b-image'); state.project.playlists[1].items[1].mediaGain = 0;
  state = inspectorTransition(state, { type: 'playlist-master', id: 'playlist-b', value: 0.4 }); assert.equal(state.runtime.volume, Math.fround(0.4));
  const edited = inspectorTransition(state, { type: 'image-duration', id: 'b-image', value: 1 });
  assert.equal(edited.runtime.items.find(item => item.id === 'b-image').duration, 10);
  const advanced = play(edited, 'advance-time', { seconds: 2 }); assert.equal(advanced.activeItemId, 'b-image'); assert.equal(advanced.runtime.time, 2);
});
check('Mute gates output independently, never rewrites authored gain or dirty revision', () => {
  let state = inspectorTransition(fixture(), { type: 'playlist-master', id: 'playlist-a', value: 0.7 });
  state = inspectorTransition(state, { type: 'media-gain', id: 'a-opening', value: 0.5 });
  const muted = play(state, 'toggle-mute'); assert.equal(muted.outputMuted, true); assert.deepEqual(muted.project, state.project);
  assert.equal(muted.runtime.volume, state.runtime.volume); assert.equal(revision(muted), revision(state));
  const adjusted = inspectorTransition(muted, { type: 'playlist-master', id: 'playlist-a', value: 0.6 }); assert.equal(adjusted.outputMuted, true);
  const unmuted = play(adjusted, 'toggle-mute'); assert.equal(unmuted.outputMuted, false);
  assert.equal(unmuted.project.playlists[0].masterVolume, 0.6); assert.equal(unmuted.project.playlists[0].items[0].mediaGain, 0.5);
  assert.equal(unmuted.runtime.volume, Math.fround(Math.fround(0.6) * 0.5));
});
check('Mute survives stop/New/Open in same Session but authored saved snapshot has no runtime fields', () => {
  const muted = play(fixture(), 'toggle-mute'); const stopped = entity(muted, 'delete-playlist', 'playlist-a');
  assert.equal(stopped.outputMuted, true); assert.equal(stopped.runtime, null);
  const fresh = continueDocumentIntent(muted, { kind: 'new' }, 'discard'); assert.equal(fresh.outputMuted, true); assert.equal(controllerCurrent(fresh.sequence), null);
  const candidate = loadOpenCandidate(muted, 'C:/MockProjects/studio.json').candidate;
  const opened = continueDocumentIntent(muted, { kind: 'open', candidate }, 'discard'); assert.equal(opened.outputMuted, true);
  const saved = saveToMockPath(muted, muted.document.path, false).state.files[muted.document.path].project;
  for (const name of ['outputMuted', 'runtime', 'sequence', 'activeItemId', 'paused', 'time']) assert(!(name in saved));
});
check('Stable Item IDs distinguish shared-source occurrences after rename and playback navigation', () => {
  let state = fixture(); state.project.playlistMode = 'list-loop';
  state.project.playlists[0].items[0].alias = 'Shared alias'; state.project.playlists[1].items[0].alias = 'Shared alias';
  rebasePlaybackToActive(state); state = play(play(state, 'next'), 'next');
  assert.equal(state.activeItemId, 'b-opening'); assert.equal(state.selectedItemId, 'a-opening');
  assert.equal(state.runtime.items[0].source, state.project.playlists[0].items[0].source);
});
check('Runtime time formatting follows production floor, invalid clamp, and hour field', () => {
  assert.equal(formatRuntimeTime(7.9), '00:07'); assert.equal(formatRuntimeTime(134), '02:14'); assert.equal(formatRuntimeTime(3727), '01:02:07');
  for (const value of [-1, NaN, Infinity]) assert.equal(formatRuntimeTime(value), '00:00');
});
check('Closed mock ignores every runtime/policy/audio operation', () => {
  const state = fixture(); state.closed = true;
  for (const action of [{ type: 'next' }, { type: 'previous' }, { type: 'toggle-pause' }, { type: 'toggle-mute' }, { type: 'natural-completion' }, { type: 'advance-time', seconds: 1 }, { type: 'project-mode', mode: 'random' }]) assert.equal(playbackTransition(state, action), state);
  assert.equal(inspectorTransition(state, { type: 'playlist-master', id: 'playlist-a', value: 0.1 }), state);
});

const status = checks.every(check => check.pass) ? 'PASS' : 'FAIL';
const result = { suite: 'MainWindow Phase C runtime/audio projection', checks: checks.length, passed: checks.filter(check => check.pass).length, status, results: checks };
const directory = fileURLToPath(new URL('../node_modules/.cache/session-phase-c/', import.meta.url));
mkdirSync(directory, { recursive: true });
writeFileSync(`${directory}/playback-model-${process.env.DSE_CHECK_STAGE || 'green'}.json`, `${JSON.stringify(result, null, 2)}\n`);
console.log(JSON.stringify(result, null, 2));
if (status !== 'PASS') process.exitCode = 1;
