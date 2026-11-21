import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {
  createLayer, createController, layerSetMode, layerRebase, layerClear,
  layerNext, layerPrevious, layerAdvanceInvocation, layerPreviousInInvocation,
  controllerSetPlaylistMode, controllerSynchronizeItemMode, controllerRebase,
  controllerClear, controllerCurrent, controllerCompleteCurrentItem, controllerPrevious,
} from '../src/sessionPlaybackSequence.ts';

// Expected results are transcribed from PlaybackSequenceControllerTest.cpp and
// MainWindowPlaybackSequenceTest.cpp. These exercise stored identity behavior,
// not implementation snapshots or a mocked sequence service.
const results = [];
function check(name, run) {
  try { run(); results.push({ name, pass: true }); console.log(`PASS ${name}`); }
  catch (error) { results.push({ name, pass: false, error: error.message }); console.error(`FAIL ${name}: ${error.message}`); }
}
const selected = identity => ({ kind: 'identity', identity });
const position = (playlistId, itemId) => selected({ playlistId, itemId });
const none = { kind: 'no-target' };
const round = { kind: 'round-complete' };
const sampler = samples => ({ samples, cursor: 0 });
const collections = (...entries) => ({ playlistIds: entries.map(entry => entry[0]),
  itemsByPlaylist: entries.map(([playlistId, itemIds, itemMode = 'list-loop']) => ({ playlistId, itemIds, itemMode })) });
const pair = () => collections(['A', ['a1', 'a2']], ['B', ['b1', 'b2', 'b3']]);
function ready(c = pair(), playlistMode = 'list-loop', playlistId = 'A', itemId = 'a1', outer = [], inner = []) {
  const state = createController(42, outer, inner);
  controllerSetPlaylistMode(state, playlistMode, c);
  controllerRebase(state, c, playlistId, itemId);
  return state;
}

check('ListLoop direct navigation wraps but finite invocation exposes its boundary', () => {
  const ids = ['A', 'B', 'C', 'D']; const state = createLayer();
  layerRebase(state, 'B', ids);
  assert.deepEqual(layerNext(state, ids), selected('C'));
  assert.deepEqual(layerNext(state, ['A', 'C', 'B', 'D']), selected('B'));
  assert.deepEqual(layerPrevious(state, ['A', 'C', 'B', 'D']), selected('C'));
  layerRebase(state, 'C', ids);
  assert.deepEqual(layerNext(state, ['A', 'B', 'C']), selected('A'));
  assert.deepEqual(layerPrevious(state, ['A', 'B', 'C']), selected('C'));
  layerRebase(state, 'B', ids);
  assert.deepEqual(layerAdvanceInvocation(state, ids), selected('C'));
  assert.deepEqual(layerAdvanceInvocation(state, ids), selected('D'));
  assert.deepEqual(layerAdvanceInvocation(state, ids), round);
});
check('ListLoop manual anchor bounds Previous and does not invent earlier history', () => {
  const ids = ['A', 'B', 'C', 'D']; const state = createLayer(); layerRebase(state, 'C', ids);
  assert.deepEqual(layerPreviousInInvocation(state, ids), none);
  assert.deepEqual(layerAdvanceInvocation(state, ids), selected('D'));
  assert.deepEqual(layerPreviousInInvocation(state, ids), selected('C'));
  assert.deepEqual(layerPreviousInInvocation(state, ids), none);
});
check('Single identity direct wrap versus finite completion, empty and removed current', () => {
  const state = createLayer(); layerRebase(state, 'A', ['A']);
  assert.deepEqual(layerNext(state, ['A']), selected('A'));
  assert.deepEqual(layerPrevious(state, ['A']), selected('A'));
  assert.deepEqual(layerAdvanceInvocation(state, ['A']), round);
  assert.deepEqual(layerNext(state, []), none);
  assert.deepEqual(layerAdvanceInvocation(state, ['B']), none);
});
check('Random samples N times with replacement including all-same rounds', () => {
  const ids = ['A', 'B', 'C', 'D']; const state = createLayer('random');
  layerRebase(state, 'B', ids, sampler([0, 0, 0, 0]));
  assert.deepEqual(layerPreviousInInvocation(state, ids), none);
  for (let i = 0; i < 4; i++) assert.deepEqual(layerAdvanceInvocation(state, ids), selected('A'));
  assert.deepEqual(layerAdvanceInvocation(state, ids), round);
});
check('Random Previous rewinds generated entries, anchor, and future without new draws', () => {
  const ids = ['A', 'B', 'C']; const draws = sampler([2, 0, 2]); const state = createLayer('random');
  layerRebase(state, 'B', ids, draws);
  for (const id of ['C', 'A', 'C']) assert.deepEqual(layerAdvanceInvocation(state, ids), selected(id));
  assert.deepEqual(layerPreviousInInvocation(state, ids), selected('A'));
  assert.deepEqual(layerPreviousInInvocation(state, ids), selected('C'));
  assert.deepEqual(layerPreviousInInvocation(state, ids), selected('B'));
  assert.deepEqual(layerPreviousInInvocation(state, ids), none);
  assert.deepEqual(layerAdvanceInvocation(state, ids), selected('C'));
  assert.equal(draws.cursor, 3);
});
check('Random removes future targets without regeneration and rejects removed current', () => {
  const state = createLayer('random'); const draws = sampler([1, 1, 2]);
  layerRebase(state, 'A', ['A', 'B', 'C'], draws);
  assert.deepEqual(layerAdvanceInvocation(state, ['A', 'C']), selected('C'));
  assert.deepEqual(layerAdvanceInvocation(state, ['A', 'C']), round);
  assert.deepEqual(layerAdvanceInvocation(state, ['A']), none);
  assert.equal(draws.cursor, 3);
});
check('Random round freezes additions and reorders by stable IDs', () => {
  const add = createLayer('random'); layerRebase(add, 'A', ['A', 'B'], sampler([1, 1]));
  assert.deepEqual(layerAdvanceInvocation(add, ['A', 'B', 'C']), selected('B'));
  assert.deepEqual(layerAdvanceInvocation(add, ['A', 'B', 'C']), selected('B'));
  assert.deepEqual(layerAdvanceInvocation(add, ['A', 'B', 'C']), round);
  const reorder = createLayer('random'); layerRebase(reorder, 'A', ['A', 'B', 'C'], sampler([1, 2, 0]));
  assert.deepEqual(layerAdvanceInvocation(reorder, ['C', 'A', 'B']), selected('B'));
  assert.deepEqual(layerAdvanceInvocation(reorder, ['C', 'A', 'B']), selected('C'));
});
check('Random Previous skips filtered history and requires surviving anchor', () => {
  const ids = ['A', 'B', 'C']; const state = createLayer('random');
  layerRebase(state, 'A', ids, sampler([1, 1, 2]));
  for (let i = 0; i < 3; i++) layerAdvanceInvocation(state, ids);
  assert.deepEqual(layerPreviousInInvocation(state, ['A', 'C']), selected('A'));
  assert.deepEqual(layerPreviousInInvocation(state, ['A', 'C']), none);
  layerRebase(state, 'A', ids, sampler([2, 2, 2]));
  layerAdvanceInvocation(state, ids);
  assert.deepEqual(layerPreviousInInvocation(state, ['C']), none);
});
check('Layer mode switches retain current, reset anchor and discard stale random future', () => {
  const ids = ['A', 'B', 'C']; const state = createLayer(); layerRebase(state, 'B', ids);
  layerSetMode(state, 'repeat-current', ids);
  assert.deepEqual(layerAdvanceInvocation(state, ids), selected('B'));
  assert.deepEqual(layerPreviousInInvocation(state, ids), selected('B'));
  layerSetMode(state, 'random', ids, sampler([0, 0, 0]));
  assert.equal(state.current, 'B');
  assert.deepEqual(layerAdvanceInvocation(state, ids), selected('A'));
  layerSetMode(state, 'repeat-current', ids);
  assert.deepEqual(layerNext(state, ids), selected('A'));
  layerSetMode(state, 'list-loop', ['C', 'B', 'A']);
  assert.deepEqual(layerNext(state, ['C', 'B', 'A']), selected('C'));
});
check('Layer clear discards transient anchors but retains RNG across rebase', () => {
  const state = createLayer('random'); const draws = sampler([0, 0, 1, 1]);
  layerRebase(state, 'B', ['A', 'B'], draws);
  assert.deepEqual(layerAdvanceInvocation(state, ['A', 'B']), selected('A'));
  layerClear(state);
  assert.equal(state.current, null); assert.deepEqual(layerNext(state, ['A', 'B']), none);
  layerRebase(state, 'B', ['A', 'B'], draws);
  assert.deepEqual(layerAdvanceInvocation(state, ['A', 'B']), selected('B'));
  assert.equal(draws.cursor, 4);
});
check('Two-level finite ListLoop crosses and wraps outer Playlists', () => {
  const c = pair(); const state = ready(c);
  for (const [p, i] of [['A', 'a2'], ['B', 'b1'], ['B', 'b2'], ['B', 'b3'], ['A', 'a1']])
    assert.deepEqual(controllerCompleteCurrentItem(state, c), position(p, i));
});
check('Cross-Playlist Previous has one bounded prior invocation and forward restoration', () => {
  const c = pair(); const state = ready(c);
  for (let i = 0; i < 5; i++) controllerCompleteCurrentItem(state, c);
  assert.deepEqual(controllerPrevious(state, c), position('B', 'b3'));
  assert.deepEqual(controllerPrevious(state, c), position('B', 'b2'));
  assert.deepEqual(controllerPrevious(state, c), position('B', 'b1'));
  assert.deepEqual(controllerPrevious(state, c), none);
  controllerCompleteCurrentItem(state, c); controllerCompleteCurrentItem(state, c);
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('A', 'a1'));
});
check('Forward restores a known Random invocation without resampling its future', () => {
  const c = collections(['A', ['a']], ['B', ['b1', 'b2', 'b3'], 'random']);
  const state = ready(c, 'list-loop', 'A', 'a', [], [2, 1, 2]);
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('B', 'b1'));
  assert.deepEqual(controllerPrevious(state, c), position('A', 'a'));
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('B', 'b1'));
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('B', 'b3'));
  assert.equal(state.itemSampler.cursor, 3);
});
check('Manual rebase to a mid-list Item clears stale invocation history', () => {
  const c = pair(); const state = ready(c);
  controllerCompleteCurrentItem(state, c); controllerCompleteCurrentItem(state, c);
  controllerRebase(state, c, 'B', 'b2');
  assert.deepEqual(controllerPrevious(state, c), none);
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('B', 'b3'));
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('A', 'a1'));
});
check('Project RepeatCurrent restarts Playlist while Item RepeatCurrent prevents outer progression', () => {
  const c = pair(); const outer = ready(c, 'repeat-current');
  assert.deepEqual(controllerCompleteCurrentItem(outer, c), position('A', 'a2'));
  assert.deepEqual(controllerCompleteCurrentItem(outer, c), position('A', 'a1'));
  const innerC = collections(['A', ['a1', 'a2'], 'repeat-current'], ['B', ['b']]);
  const inner = ready(innerC);
  for (let i = 0; i < 4; i++) assert.deepEqual(controllerCompleteCurrentItem(inner, innerC), position('A', 'a1'));
  assert.deepEqual(controllerPrevious(inner, innerC), position('A', 'a1'));
});
check('Two-level Random retains duplicate outer and inner invocation entries', () => {
  const c = collections(['A', ['a1', 'a2'], 'random'], ['B', ['b1', 'b2'], 'random']);
  const state = ready(c, 'random', 'A', 'a1', [1, 1], [1, 1, 1, 1, 1, 1]);
  for (const [p, i] of [['A', 'a2'], ['A', 'a2'], ['B', 'b1'], ['B', 'b2'], ['B', 'b2'], ['B', 'b1']])
    assert.deepEqual(controllerCompleteCurrentItem(state, c), position(p, i));
});
check('Each fresh Playlist invocation owns its configured Item policy', () => {
  const c = collections(['A', ['a1', 'a2']], ['B', ['b1', 'b2'], 'random']);
  const state = ready(c, 'list-loop', 'A', 'a1', [], [1, 1, 1, 1]);
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('A', 'a2'));
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('B', 'b1'));
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('B', 'b2'));
  controllerRebase(state, c, 'A', 'a1');
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('A', 'a2'));
  controllerRebase(state, c, 'B', 'b1');
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('B', 'b2'));
});
for (const outerMode of ['list-loop', 'random', 'repeat-current']) for (const innerMode of ['list-loop', 'random', 'repeat-current']) {
  check(`Two scopes ${outerMode} / ${innerMode} retain explicit precedence`, () => {
    const c = collections(['A', ['a1', 'a2'], innerMode], ['B', ['b']]);
    const state = ready(c, outerMode, 'A', 'a1', [1, 1], [1, 1]);
    if (innerMode === 'repeat-current') {
      for (let i = 0; i < 5; i++) assert.deepEqual(controllerCompleteCurrentItem(state, c), position('A', 'a1'));
      return;
    }
    assert.deepEqual(controllerCompleteCurrentItem(state, c), position('A', 'a2'));
    if (innerMode === 'random') assert.deepEqual(controllerCompleteCurrentItem(state, c), position('A', 'a2'));
    assert.deepEqual(controllerCompleteCurrentItem(state, c), outerMode === 'repeat-current' ? position('A', 'a1') : position('B', 'b'));
  });
}
check('Removed future Random Playlist is skipped and removed current requires caller rebase', () => {
  const c = collections(['A', ['a1', 'a2']], ['B', ['b']], ['C', ['c']]);
  const state = ready(c, 'random', 'A', 'a1', [1, 2, 0]);
  const changed = collections(['A', ['a1', 'a2']], ['C', ['c']]);
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('A', 'a2'));
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('C', 'c'));
  assert.deepEqual(controllerCompleteCurrentItem(state, collections(['A', ['a1', 'a2']])), none);
  assert.deepEqual(controllerPrevious(state, collections(['A', ['a1', 'a2']])), none);
});
check('Added Playlist enters next Random round only after explicit round boundary', () => {
  const initial = collections(['A', ['a']], ['B', ['b']]);
  const changed = collections(['A', ['a']], ['B', ['b']], ['C', ['c']]);
  const state = ready(initial, 'random', 'A', 'a', [1, 1, 2, 2, 2]);
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('B', 'b'));
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('B', 'b'));
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), round);
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('C', 'c'));
});
check('Empty generated outer Random round completes finitely and regenerated round can advance', () => {
  const c = collections(['A', ['a']], ['B', []], ['C', ['c']]);
  const state = ready(c, 'random', 'A', 'a', [1, 1, 1, 2, 2, 2]);
  assert.deepEqual(controllerCompleteCurrentItem(state, c), round);
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('C', 'c'));
});
check('ListLoop skips empty entries; all-empty and missing current have no target', () => {
  const c = collections(['A', ['a']], ['B', []], ['C', ['c']]); const state = ready(c, 'list-loop', 'A', 'a');
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('C', 'c'));
  assert.deepEqual(controllerCompleteCurrentItem(state, collections(['A', []], ['B', []], ['C', []])), none);
  const fresh = createController();
  assert.deepEqual(controllerCompleteCurrentItem(fresh, c), none);
  assert.deepEqual(controllerPrevious(fresh, c), none);
});
check('Previous never restores filtered prior invocation or disabled Item history', () => {
  const c = pair(); const state = ready(c);
  controllerCompleteCurrentItem(state, c); controllerCompleteCurrentItem(state, c);
  const removedPrevious = collections(['B', ['b1', 'b2', 'b3']]);
  assert.deepEqual(controllerPrevious(state, removedPrevious), none);
  assert.deepEqual(controllerCurrent(state), { playlistId: 'B', itemId: 'b1' });
  const inner = ready(c); controllerCompleteCurrentItem(inner, c);
  assert.deepEqual(controllerPrevious(inner, collections(['A', ['a2']], ['B', ['b1']])), none);
});
check('Latest authored order controls ListLoop future without changing current identity', () => {
  const c = collections(['A', ['a1', 'a2', 'a3']], ['B', ['b']], ['C', ['c']]); const state = ready(c);
  const changed = collections(['A', ['a1', 'a3', 'a2']], ['C', ['c']], ['B', ['b']]);
  assert.deepEqual(controllerCurrent(state), { playlistId: 'A', itemId: 'a1' });
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('A', 'a3'));
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('A', 'a2'));
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('C', 'c'));
});
check('Active mode synchronization preserves current and resets anchor plus bounded history', () => {
  const c = pair(); const state = ready(c); controllerCompleteCurrentItem(state, c); controllerCompleteCurrentItem(state, c);
  const changed = collections(['A', ['a1', 'a2']], ['B', ['b1', 'b2', 'b3'], 'repeat-current']);
  controllerSynchronizeItemMode(state, changed);
  assert.deepEqual(controllerCurrent(state), { playlistId: 'B', itemId: 'b1' });
  assert.deepEqual(controllerPrevious(state, changed), position('B', 'b1'));
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('B', 'b1'));
  const again = pair(); controllerSynchronizeItemMode(state, again);
  assert.deepEqual(controllerPrevious(state, again), none);
});
check('Inactive mode edits leave active invocation history and take effect on later entry', () => {
  const c = pair(); const state = ready(c); controllerCompleteCurrentItem(state, c);
  const changed = collections(['A', ['a1', 'a2']], ['B', ['b1', 'b2', 'b3'], 'repeat-current']);
  controllerSynchronizeItemMode(state, changed);
  assert.deepEqual(controllerPrevious(state, changed), position('A', 'a1'));
  controllerCompleteCurrentItem(state, changed);
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('B', 'b1'));
  assert.deepEqual(controllerCompleteCurrentItem(state, changed), position('B', 'b1'));
});
check('Project mode change resets outer history without moving active position', () => {
  const c = pair(); const state = ready(c); controllerCompleteCurrentItem(state, c); controllerCompleteCurrentItem(state, c);
  controllerSetPlaylistMode(state, 'repeat-current', c);
  assert.deepEqual(controllerCurrent(state), { playlistId: 'B', itemId: 'b1' });
  assert.deepEqual(controllerPrevious(state, c), none);
  controllerCompleteCurrentItem(state, c); controllerCompleteCurrentItem(state, c);
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('B', 'b1'));
});
check('Unchanged active configuration synchronization retains prior history', () => {
  const c = pair(); const state = ready(c); controllerCompleteCurrentItem(state, c); controllerCompleteCurrentItem(state, c);
  controllerSynchronizeItemMode(state, c);
  assert.deepEqual(controllerPrevious(state, c), position('A', 'a2'));
});
check('Clear removes all transient navigation state; explicit restart establishes fresh anchor', () => {
  const c = pair(); const state = ready(c); controllerCompleteCurrentItem(state, c); controllerCompleteCurrentItem(state, c);
  controllerClear(state);
  assert.equal(controllerCurrent(state), null);
  assert.deepEqual(controllerPrevious(state, c), none);
  assert.deepEqual(controllerCompleteCurrentItem(state, c), none);
  controllerRebase(state, c, 'A', 'a1');
  assert.deepEqual(controllerPrevious(state, c), none);
  assert.deepEqual(controllerCompleteCurrentItem(state, c), position('A', 'a2'));
});
check('JSON-cloned state continues deterministic Random history and preserves input collections', () => {
  const c = collections(['A', ['a1', 'a2'], 'random'], ['B', ['b'], 'random']); const original = JSON.stringify(c);
  const state = ready(c, 'random', 'A', 'a1', [1, 1], [1, 0, 0, 0, 0]);
  controllerCompleteCurrentItem(state, c);
  const clone = JSON.parse(JSON.stringify(state));
  for (let i = 0; i < 12; i++) assert.deepEqual(controllerCompleteCurrentItem(state, c), controllerCompleteCurrentItem(clone, c));
  assert.equal(JSON.stringify(c), original);
});
check('Deterministic fallback RNG samples valid IDs and remains bounded over repeated rounds', () => {
  const c = collections(['A', ['a1', 'a2'], 'random'], ['B', ['b1', 'b2'], 'random']);
  const state = ready(c, 'random'); const twin = ready(c, 'random');
  for (let i = 0; i < 300; i++) {
    const result = controllerCompleteCurrentItem(state, c);
    assert.deepEqual(result, controllerCompleteCurrentItem(twin, c));
    if (result.kind === 'identity') assert(c.itemsByPlaylist.some(p => p.playlistId === result.identity.playlistId && p.itemIds.includes(result.identity.itemId)));
    else assert.equal(result.kind, 'round-complete');
  }
});

const summary = { passed: results.filter(result => result.pass).length, failed: results.filter(result => !result.pass).length, results };
const evidenceDir = path.resolve('node_modules/.cache/session-phase-c');
fs.mkdirSync(evidenceDir, { recursive: true });
fs.writeFileSync(path.join(evidenceDir, `sequence-${summary.failed ? 'red' : 'green'}.json`), JSON.stringify(summary, null, 2));
console.log(`Sequence checks: ${summary.passed}/${results.length} PASS`);
if (summary.failed) process.exitCode = 1;
