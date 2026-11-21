import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { createSessionFixture } from '../src/sessionFixtures.ts';
import { findItem, findPlaylist, entityTransition } from '../src/sessionEntities.ts';
import { inspectorTransition, normalizedAspect } from '../src/sessionInspectorModel.ts';

const checks = [];
function check(name, run) {
  try { run(); checks.push({ name, pass: true }); console.log(`PASS ${name}`); }
  catch (error) { checks.push({ name, pass: false, error: error.message }); console.error(`FAIL ${name}: ${error.message}`); }
}
const fixture = () => createSessionFixture('normal');
const item = (state, id = 'a-opening') => findItem(state, id).item;
const playlist = (state, id = 'playlist-a') => findPlaylist(state, id);
const apply = (state, type, id, value) => inspectorTransition(state, { type, id, value });
const identities = state => [state.selectedPlaylistId, state.selectedItemId, state.activePlaylistId, state.activeItemId];
const approx = (actual, expected) => assert(Math.abs(actual - expected) < 1e-6, `${actual} should approximate ${expected}`);
function changed(before, after) {
  assert.equal(after.document.revision, before.document.revision + 1);
  assert.equal(after.document.cleanRevision, before.document.cleanRevision);
  assert.deepEqual(identities(after), identities(before));
  assert.deepEqual(after.files, before.files);
  assert.equal(after.feedback, null);
}
function presentation(state, id = 'a-opening', mutate = () => {}) {
  const value = structuredClone(item(state, id).presentation); mutate(value); return value;
}
function activeImage() {
  let state = fixture();
  state = entityTransition(state, { type: 'select-playlist', id: 'playlist-b' });
  return entityTransition(state, { type: 'play-item', id: 'b-image' });
}
function freeze(value) {
  if (value && typeof value === 'object') { Object.freeze(value); for (const member of Object.values(value)) freeze(member); }
  return value;
}

check('inline Name reuses alias mutation and preserves whitespace, source and identities', () => {
  for (const [type, id, get] of [['rename-playlist', 'playlist-a', playlist], ['rename-item', 'a-opening', item]]) {
    const state = fixture(); const after = inspectorTransition(state, { type, id, alias: '  中文 / Editorial  ' });
    changed(state, after); assert.equal(get(after).alias, '  中文 / Editorial  ');
    assert.equal(item(after).source, item(state).source); assert.equal(get(after).id, get(state).id);
  }
});
check('inline invalid Name is silent; menu Rename keeps its existing warning', () => {
  for (const [type, id] of [['rename-playlist', 'playlist-a'], ['rename-item', 'a-opening']]) {
    const state = fixture(); const action = { type, id, alias: ' \t\n' };
    assert.deepEqual(inspectorTransition(state, action), state);
    assert.equal(entityTransition(state, action).feedback.title, 'Invalid Name');
  }
});
check('same Name does not dirty; duplicate aliases are legal', () => {
  const state = fixture();
  assert.deepEqual(inspectorTransition(state, { type: 'rename-item', id: 'a-opening', alias: item(state).alias }), state);
  const after = inspectorTransition(state, { type: 'rename-playlist', id: 'playlist-b', alias: playlist(state).alias });
  changed(state, after); assert.equal(playlist(after, 'playlist-b').alias, playlist(after).alias);
});
check('selected non-active Playlist Master updates only that authored Playlist', () => {
  const state = createSessionFixture('selected-playlist-differs');
  const after = apply(state, 'playlist-master', 'playlist-b', 0.35);
  changed(state, after); assert.equal(playlist(after, 'playlist-b').masterVolume, 0.35);
  assert.equal(playlist(after).masterVolume, 1); assert.deepEqual(after.runtime, state.runtime);
});
check('active Playlist Master immediately multiplies current VIDEO authored gain', () => {
  const state = fixture(); item(state).mediaGain = 0.4; state.runtime.paused = true;
  const after = apply(state, 'playlist-master', 'playlist-a', 0.5);
  changed(state, after); approx(after.runtime.volume, 0.2);
  assert.equal(after.runtime.time, state.runtime.time); assert.equal(after.runtime.paused, true);
  assert.equal(after.runtime.rate, state.runtime.rate); assert.deepEqual(after.runtime.items, state.runtime.items);
});
check('Master clamps at 0/1 and rejects nonfinite or missing target', () => {
  const state = fixture();
  const low = apply(state, 'playlist-master', 'playlist-a', -3); changed(state, low); assert.equal(playlist(low).masterVolume, 0); assert.equal(low.runtime.volume, 0);
  const high = apply(low, 'playlist-master', 'playlist-a', 3); changed(low, high); assert.equal(playlist(high).masterVolume, 1);
  for (const value of [NaN, Infinity, -Infinity]) assert.deepEqual(apply(state, 'playlist-master', 'playlist-a', value), state);
  assert.deepEqual(apply(state, 'playlist-master', 'missing', 0.5), state);
});
check('same active Master can refresh runtime output without dirtying', () => {
  const state = fixture(); state.runtime.volume = 0.12; item(state).mediaGain = 0.7;
  const after = apply(state, 'playlist-master', 'playlist-a', 1);
  assert.equal(after.document.revision, state.document.revision); approx(after.runtime.volume, 0.7);
});
check('Master uses Image factor one; missing active projection does not start playback', () => {
  const state = activeImage(); item(state, 'b-image').mediaGain = 0.02;
  const after = apply(state, 'playlist-master', 'playlist-b', 0.73);
  changed(state, after); approx(after.runtime.volume, 0.73);
  const empty = fixture(); empty.runtime = null; empty.activePlaylistId = empty.activeItemId = null;
  const edited = apply(empty, 'playlist-master', 'playlist-b', 0.2); changed(empty, edited); assert.equal(edited.runtime, null);
});
check('Image Duration authors immediately including both UI boundaries', () => {
  let state = fixture(); state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = 'b-image';
  for (const value of [0.1, 86400, 12.34]) { const after = apply(state, 'image-duration', 'b-image', value); changed(state, after); assert.equal(item(after, 'b-image').duration, value); assert.deepEqual(after.runtime, state.runtime); state = after; }
});
check('active Image Duration retains old runtime snapshot, rate, pause and position', () => {
  const state = activeImage(); state.runtime.time = 4; state.runtime.paused = true;
  const after = apply(state, 'image-duration', 'b-image', 40);
  changed(state, after); assert.equal(item(after, 'b-image').duration, 40); assert.deepEqual(after.runtime, state.runtime);
});
check('duration setter follows broader production model validation, not UI bounds', () => {
  const state = fixture();
  for (const value of [0.01, 90000]) { const after = apply(state, 'image-duration', 'b-image', value); changed(state, after); assert.equal(item(after, 'b-image').duration, value); }
  for (const value of [0, -1, NaN, Infinity]) assert.deepEqual(apply(state, 'image-duration', 'b-image', value), state);
  assert.deepEqual(apply(state, 'image-duration', 'a-opening', 12), state);
  assert.deepEqual(apply(state, 'image-duration', 'b-image', 10), state);
});
check('non-active Video configured Apply authors desired rate without runtime activation', () => {
  const state = createSessionFixture('selected-item-differs');
  const after = apply(state, 'video-rate', 'a-scene', 1.7);
  changed(state, after); assert.equal(item(after, 'a-scene').rate, 1.7); assert.deepEqual(after.runtime, state.runtime);
});
check('active Video Apply updates actual rate and preserves time/pause/output', () => {
  const state = fixture(); state.runtime.paused = true;
  const after = apply(state, 'video-rate', 'a-opening', 1.8);
  changed(state, after); assert.equal(item(after).rate, 1.8); approx(after.runtime.rate, 1.8);
  assert.equal(after.runtime.time, 8); assert.equal(after.runtime.paused, true); assert.equal(after.runtime.volume, 1);
  assert.deepEqual(after.runtime.items, state.runtime.items, 'Apply does not rebuild scene projection');
});
check('Video model rejects off-grid/nonfinite/wrong-type/out-of-range; endpoints legal', () => {
  const state = fixture();
  for (const value of [0.49, 2.01, 0.55, NaN, Infinity, -Infinity]) assert.deepEqual(apply(state, 'video-rate', 'a-opening', value), state);
  assert.deepEqual(apply(state, 'video-rate', 'b-image', 1.5), state);
  for (const value of [0.5, 2]) { const after = apply(state, 'video-rate', 'a-opening', value); changed(state, after); assert.equal(item(after).rate, value); approx(after.runtime.rate, value); }
});
check('runtime rate failure retains new authored configured and dirty with old actual, silently', () => {
  const state = fixture(); state.faults.rateFails = true;
  const after = apply(state, 'video-rate', 'a-opening', 1.6);
  changed(state, after); assert.equal(item(after).rate, 1.6); assert.deepEqual(after.runtime, state.runtime);
});
check('same configured Apply retries actual rate without new authored revision', () => {
  const state = fixture(); item(state).rate = 1.6;
  const after = apply(state, 'video-rate', 'a-opening', 1.6);
  assert.equal(after.document.revision, state.document.revision); approx(after.runtime.rate, 1.6);
});
check('rate needs active target and projection scene mapping; inactive/no runtime stays inactive', () => {
  const state = fixture(); state.runtime.items = state.runtime.items.filter(value => value.id !== 'a-opening');
  const after = apply(state, 'video-rate', 'a-opening', 1.3); changed(state, after); assert.equal(after.runtime.rate, 1);
  state.runtime = null; state.activePlaylistId = state.activeItemId = null;
  const inactive = apply(state, 'video-rate', 'a-opening', 1.4); changed(state, inactive); assert.equal(inactive.runtime, null);
});
check('non-active Media Gain is independent; active gain multiplies active Master', () => {
  const state = fixture(); playlist(state).masterVolume = 0.6;
  const inactive = apply(state, 'media-gain', 'a-scene', 0.25); changed(state, inactive); assert.deepEqual(inactive.runtime, state.runtime);
  const active = apply(state, 'media-gain', 'a-opening', 0.5); changed(state, active); approx(active.runtime.volume, 0.3);
  assert.equal(active.runtime.time, state.runtime.time); assert.deepEqual(active.runtime.items, state.runtime.items);
});
check('gain clamps, rejects nonfinite/Image, same value retries runtime without dirty', () => {
  const state = fixture();
  const low = apply(state, 'media-gain', 'a-opening', -1); changed(state, low); assert.equal(item(low).mediaGain, 0); assert.equal(low.runtime.volume, 0);
  const high = apply(low, 'media-gain', 'a-opening', 2); changed(low, high); assert.equal(item(high).mediaGain, 1);
  for (const value of [NaN, Infinity, -Infinity]) assert.deepEqual(apply(state, 'media-gain', 'a-opening', value), state);
  assert.deepEqual(apply(state, 'media-gain', 'b-image', 0.5), state);
  state.runtime.volume = 0; const same = apply(state, 'media-gain', 'a-opening', 1);
  assert.equal(same.document.revision, state.document.revision); assert.equal(same.runtime.volume, 1);
});
check('gain runtime update requires current projected scene mapping', () => {
  const state = fixture(); state.runtime.items = [];
  const after = apply(state, 'media-gain', 'a-opening', 0.2); changed(state, after); assert.equal(after.runtime.volume, 1);
});
check('same-source Item occurrences retain independent rate, gain and visual properties', () => {
  const state = fixture(); assert.equal(item(state).source, item(state, 'b-opening').source);
  let after = apply(state, 'video-rate', 'b-opening', 1.9);
  after = apply(after, 'media-gain', 'b-opening', 0.2);
  after = apply(after, 'presentation', 'b-opening', presentation(after, 'b-opening', value => { value.layout.positionX = Math.fround(0.25); }));
  assert.deepEqual(item(after), item(state)); assert.deepEqual(after.runtime, state.runtime);
  assert.equal(item(after, 'b-opening').rate, 1.9); assert.equal(item(after, 'b-opening').mediaGain, 0.2);
  assert.equal(item(after, 'b-opening').presentation.layout.positionX, 0.25); assert.equal(after.document.revision, state.document.revision + 3);
});
check('AspectRatio::from normalizes positive int32 pair, rejects invalid values', () => {
  assert.deepEqual(normalizedAspect(16, 10), { numerator: 8, denominator: 5 });
  assert.deepEqual(normalizedAspect(10000, 10000), { numerator: 1, denominator: 1 });
  assert.deepEqual(normalizedAspect(7, 3), { numerator: 7, denominator: 3 });
  for (const pair of [[0, 1], [-1, 1], [1, 0], [NaN, 1], [1, Infinity], [1.5, 1], [2147483648, 1]]) assert.equal(normalizedAspect(...pair), null);
});
check('inactive presentation immediate update preserves all fields and runtime', () => {
  const state = fixture();
  const value = presentation(state, 'b-image', p => { p.crop = { x: 0.25, y: 0.25, width: 0.5, height: 0.5 }; p.layout = { positionX: -0.5, positionY: 0.25, scaleX: 2, scaleY: 0.75 }; p.mediaPresentation = { mode: 'custom', customAspect: { numerator: 8, denominator: 5 } }; });
  const after = apply(state, 'presentation', 'b-image', value);
  changed(state, after); assert.deepEqual(item(after, 'b-image').presentation, value); assert.deepEqual(after.runtime, state.runtime);
});
check('presentation validity follows model crop/finite/positive rules, not UI bounds', () => {
  const state = fixture();
  const valid = presentation(state, 'b-image', p => { p.layout = { positionX: -3, positionY: 4, scaleX: 0.01, scaleY: 4 }; p.mediaPresentation.customAspect = { numerator: 10001, denominator: 3 }; });
  const after = apply(state, 'presentation', 'b-image', valid); changed(state, after);
  for (const mutate of [p => { p.crop.x = -0.1; }, p => { p.crop.width = 0; }, p => { p.crop.width = 2; }, p => { p.layout.scaleX = 0; }, p => { p.layout.positionY = NaN; }, p => { p.mediaPresentation.mode = 'fit'; }, p => { p.mediaPresentation.customAspect.denominator = 0; }]) {
    assert.deepEqual(apply(state, 'presentation', 'b-image', presentation(state, 'b-image', mutate)), state);
  }
});
check('presentation equality is exact ratio identity; policy preserves authored custom values', () => {
  const state = fixture();
  const custom = presentation(state, 'b-image', p => { p.mediaPresentation = { mode: 'custom', customAspect: { numerator: 21, denominator: 9 } }; });
  let after = apply(state, 'presentation', 'b-image', custom); changed(state, after);
  const native = presentation(after, 'b-image', p => { p.mediaPresentation.mode = 'native'; });
  const next = apply(after, 'presentation', 'b-image', native); changed(after, next);
  assert.deepEqual(item(next, 'b-image').presentation.mediaPresentation.customAspect, { numerator: 21, denominator: 9 });
  assert.deepEqual(apply(next, 'presentation', 'b-image', native), next);
  const reduced = presentation(after, 'b-image', p => { p.mediaPresentation.customAspect = { numerator: 7, denominator: 3 }; });
  changed(after, apply(after, 'presentation', 'b-image', reduced));
});
check('successful active visual rebuild refreshes whole Playlist snapshot, retains actual/pause/time and volume', () => {
  const state = fixture(); state.runtime.paused = true; state.runtime.rate = 1.3;
  item(state, 'a-disabled').duration = 37; playlist(state).masterVolume = 0.4; item(state).mediaGain = 0.5;
  const after = apply(state, 'presentation', 'a-opening', presentation(state, 'a-opening', p => { p.layout.scaleX = p.layout.scaleY = Math.fround(1.2); }));
  changed(state, after); assert.deepEqual(after.runtime.items, playlist(after).items);
  assert.equal(after.runtime.time, 8); assert.equal(after.runtime.paused, true); assert.equal(after.runtime.rate, 1.3); approx(after.runtime.volume, 0.2);
});
check('visual prepare failure keeps previous projection and authored change, silently', () => {
  const state = fixture(); state.faults.playback = 'rebuild-prepare';
  const after = apply(state, 'presentation', 'a-opening', presentation(state, 'a-opening', p => { p.layout.positionY = -0.5; }));
  changed(state, after); assert.equal(item(after).presentation.layout.positionY, -0.5); assert.deepEqual(after.runtime, state.runtime);
});
check('visual bind failure rolls runtime back to previous snapshot without authored rollback or warning', () => {
  const state = fixture(); state.faults.playback = 'rebuild-bind'; state.runtime.paused = true;
  const after = apply(state, 'presentation', 'a-opening', presentation(state, 'a-opening', p => { p.layout.positionX = 0.5; }));
  changed(state, after); assert.equal(item(after).presentation.layout.positionX, 0.5); assert.deepEqual(after.runtime, state.runtime);
});
check('visual restore-seek failure keeps updated projection at zero without warning', () => {
  const state = fixture(); state.faults.playback = 'restore-seek'; state.runtime.paused = true;
  const after = apply(state, 'presentation', 'a-opening', presentation(state, 'a-opening', p => { p.mediaPresentation.mode = 'native'; }));
  changed(state, after); assert.equal(after.runtime.time, 0); assert.equal(after.runtime.paused, true); assert.equal(after.runtime.rate, state.runtime.rate); assert.deepEqual(after.runtime.items, playlist(after).items);
});
check('visual rollback-seek failure keeps old projection at zero without warning', () => {
  const state = fixture(); state.faults.playback = 'rollback-seek';
  const after = apply(state, 'presentation', 'a-opening', presentation(state, 'a-opening', p => { p.mediaPresentation.mode = 'native'; }));
  changed(state, after); assert.equal(after.runtime.time, 0); assert.deepEqual(after.runtime.items, state.runtime.items);
  assert.equal(item(after).presentation.mediaPresentation.mode, 'native');
});
check('visual rollback-bind failure stops and clears active only, retains authored selection silently', () => {
  const state = fixture(); state.faults.playback = 'rollback-bind';
  const after = apply(state, 'presentation', 'a-opening', presentation(state, 'a-opening', p => { p.layout.positionX = 0.5; }));
  assert.equal(after.document.revision, state.document.revision + 1); assert.equal(after.runtime, null);
  assert.equal(after.activePlaylistId, null); assert.equal(after.activeItemId, null);
  assert.equal(after.selectedPlaylistId, state.selectedPlaylistId); assert.equal(after.selectedItemId, state.selectedItemId);
  assert.equal(item(after).presentation.layout.positionX, 0.5); assert.equal(after.feedback, null);
});
check('same visual value still attempts runtime rebuild without dirtying', () => {
  const state = fixture(); state.faults.playback = 'restore-seek';
  const after = apply(state, 'presentation', 'a-opening', presentation(state));
  assert.equal(after.document.revision, state.document.revision); assert.equal(after.runtime.time, 0); assert.equal(after.feedback, null);
});
check('disabled selected entities remain editable without activation', () => {
  const state = fixture();
  const after = apply(state, 'video-rate', 'd-scene', 1.5); changed(state, after); assert.equal(item(after, 'd-scene').rate, 1.5); assert.deepEqual(after.runtime, state.runtime);
  const imageAfter = apply(state, 'image-duration', 'a-disabled', 12); changed(state, imageAfter); assert.equal(item(imageAfter, 'a-disabled').duration, 12);
});
check('transitions and action values preserve readonly inputs; closed state ignores edits', () => {
  const state = freeze(fixture()); const before = structuredClone(state);
  const action = freeze({ type: 'presentation', id: 'a-opening', value: presentation(state, 'a-opening', p => { p.layout.positionY = 0.25; }) });
  const after = inspectorTransition(state, action); changed(state, after); assert.deepEqual(state, before);
  after.project.playlists[0].items[0].presentation.layout.positionY = -0.25;
  assert.equal(action.value.layout.positionY, 0.25);
  const closed = { ...fixture(), closed: true }; assert.deepEqual(apply(closed, 'playlist-master', 'playlist-a', 0.2), closed);
});

const failed = checks.filter(check => !check.pass).length;
const proofName = process.env.INSPECTOR_MODEL_PROOF || 'inspector-model-green.json';
assert(/^[a-z0-9-]+\.json$/.test(proofName), 'proof output must remain a cache basename');
const cache = new URL('../node_modules/.cache/session-phase-b/', import.meta.url);
await mkdir(cache, { recursive: true });
await writeFile(new URL(proofName, cache), `${JSON.stringify({ passed: checks.length - failed, failed, total: checks.length, checks }, null, 2)}\n`);
console.log(`Inspector model checks: ${checks.length - failed}/${checks.length} PASS`);
if (failed) process.exitCode = 1;
