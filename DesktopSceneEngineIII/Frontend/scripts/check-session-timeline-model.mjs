import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createSessionFixture } from '../src/sessionFixtures.ts';
import { playbackTransition } from '../src/sessionPlayback.ts';
import {
  timelineFacts, beginTimelineDrag, moveTimelineDrag, timelineReleaseIntent, applyTimelineSeek,
  timelineValueForTime, timeForValue, isTimelineEndIntent, valueForGroovePosition,
} from '../src/sessionTimelineModel.ts';

const checks = [];
function check(name, test) {
  try { test(); checks.push({ name, pass: true }); }
  catch (error) { checks.push({ name, pass: false, error: error.message }); }
}
const fixture = () => createSessionFixture('normal');
const current = state => state.runtime.items.find(item => item.id === state.activeItemId);
function freeze(value) {
  if (value && typeof value === 'object' && !Object.isFrozen(value)) {
    Object.freeze(value); for (const child of Object.values(value)) freeze(child);
  }
  return value;
}
function release(state, value) { return timelineReleaseIntent(state, moveTimelineDrag(beginTimelineDrag(state), value)); }
function commit(state, intent) {
  if (intent.type === 'next') return playbackTransition(state, { type: 'next' });
  if (intent.type === 'seek') return applyTimelineSeek(state, intent.seconds);
  return state;
}

check('Active runtime Video enables Timeline and projects the same time/duration fact', () => {
  const state = fixture();
  assert.deepEqual(timelineFacts(state), { enabled: true, itemId: 'a-opening', duration: 23, time: 8, value: timelineValueForTime(8, 23) });
});
check('Selected Playlist/Item never drives Timeline duration or identity', () => {
  const state = fixture(); state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = 'b-image';
  assert.equal(timelineFacts(state).itemId, 'a-opening'); assert.equal(timelineFacts(state).duration, 23);
});
check('Bound runtime snapshot supplies duration even when authored Item duration differs', () => {
  const state = fixture(); state.project.playlists[0].items[0].duration = 900;
  assert.equal(timelineFacts(state).duration, 23);
});
check('Active Image is seekable under its finite positive runtime duration', () => {
  const state = fixture(); state.activeItemId = 'a-disabled'; state.runtime.time = 3;
  assert.equal(timelineFacts(state).enabled, true); assert.equal(timelineFacts(state).duration, 10);
  assert.equal(applyTimelineSeek(state, 5).runtime.time, 5);
});
check('No runtime disables Timeline despite stale Workspace active identity', () => {
  const state = fixture(); state.runtime = null;
  assert.deepEqual(timelineFacts(state), { enabled: false, itemId: null, duration: 0, time: 0, value: 0 });
  assert.equal(beginTimelineDrag(state), null);
});
check('No valid active scene in runtime projection disables Timeline', () => {
  const state = fixture(); state.activeItemId = 'missing';
  assert.equal(timelineFacts(state).enabled, false); assert.equal(beginTimelineDrag(state), null);
  state.activeItemId = null; assert.equal(timelineFacts(state).enabled, false);
});
check('Zero, negative, NaN, infinite and float-overflow duration disable Timeline', () => {
  for (const duration of [0, -1, NaN, Infinity, -Infinity, 1e50]) {
    const state = fixture(); current(state).duration = duration;
    assert.equal(timelineFacts(state).enabled, false, `duration=${duration}`);
    assert.equal(beginTimelineDrag(state), null); assert.equal(applyTimelineSeek(state, 1), state);
  }
});
check('Timeline value mapping uses production 0..10000 rounding and clamps time', () => {
  assert.equal(timelineValueForTime(0, 120), 0); assert.equal(timelineValueForTime(60, 120), 5000);
  assert.equal(timelineValueForTime(120, 120), 10000); assert.equal(timelineValueForTime(240, 120), 10000);
  assert.equal(timelineValueForTime(-1, 120), 0); assert.equal(timelineValueForTime(0.5001, 10000), 1);
});
check('Timeline non-finite time mapping matches positive-infinity and NaN production branches', () => {
  assert.equal(timelineValueForTime(Infinity, 120), 10000);
  assert.equal(timelineValueForTime(NaN, 120), 0); assert.equal(timelineValueForTime(-Infinity, 120), 0);
  for (const duration of [0, NaN, Infinity]) assert.equal(timelineValueForTime(10, duration), 0);
});
check('Time mapping clamps slider coordinates without broadening exact endpoint intent', () => {
  assert.equal(timeForValue(0, 120), 0); assert.equal(timeForValue(5000, 120), 60);
  assert.equal(timeForValue(10000, 120), 120); assert.equal(timeForValue(10001, 120), 120);
  assert.equal(timeForValue(-1, 120), 0); assert.equal(timeForValue(5000, 0), 0);
  assert.equal(isTimelineEndIntent(9999), false); assert.equal(isTimelineEndIntent(10000), true); assert.equal(isTimelineEndIntent(10001), false);
});
check('Mapping emulates float intermediates in TimelineUtils, rather than double-only ratios', () => {
  assert.equal(timeForValue(9999, 120), Math.fround(Math.fround(120 * 9999) / 10000));
  assert.equal(timeForValue(3333, 107), Math.fround(Math.fround(107 * 3333) / 10000));
  assert.equal(timelineValueForTime(0.33335, 1), Math.round(Math.fround(Math.fround(0.33335) / 1) * 10000));
});
check('Pointer mapping uses groove left and inclusive width, not outer component width', () => {
  assert.equal(valueForGroovePosition(20, 20, 101), 0);
  assert.equal(valueForGroovePosition(70, 20, 101), 5000);
  assert.equal(valueForGroovePosition(120, 20, 101), 10000);
  assert.equal(valueForGroovePosition(-50, 20, 101), 0); assert.equal(valueForGroovePosition(999, 20, 101), 10000);
});
check('Groove mapping handles reversed appearance, integer geometry and degenerate groove', () => {
  assert.equal(valueForGroovePosition(20, 20, 101, true), 10000);
  assert.equal(valueForGroovePosition(120, 20, 101, true), 0);
  assert.equal(valueForGroovePosition(21, 20, 4), 3333);
  assert.equal(valueForGroovePosition(22, 20, 4), 6667);
  assert.equal(valueForGroovePosition(30, 20, 1), 0); assert.equal(valueForGroovePosition(30, 20, 0, true), 0);
});
check('Press snapshots active Item and duration without mutation or seek', () => {
  const state = freeze(fixture()); const before = structuredClone(state); const drag = beginTimelineDrag(state);
  assert.equal(drag.itemId, 'a-opening'); assert.equal(drag.duration, 23); assert.equal(drag.value, timelineFacts(state).value);
  assert.deepEqual(state, before);
});
check('Move changes only immutable preview coordinate and leaves runtime/selection/dirty untouched', () => {
  const state = freeze(fixture()); const before = structuredClone(state); const drag = freeze(beginTimelineDrag(state));
  const moved = moveTimelineDrag(drag, 7500);
  assert.equal(moved.value, 7500); assert.equal(moved.itemId, drag.itemId); assert.equal(moved.duration, drag.duration);
  assert.notEqual(moved, drag); assert.equal(drag.value, timelineFacts(state).value); assert.deepEqual(state, before);
});
check('Valid Release produces seek intent and does not itself mutate runtime', () => {
  const state = freeze(fixture()); const before = structuredClone(state);
  assert.deepEqual(release(state, 5000), { type: 'seek', seconds: 11.5 }); assert.deepEqual(state, before);
});
check('Runtime time may advance while drag snapshot stays valid', () => {
  const state = fixture(); const drag = moveTimelineDrag(beginTimelineDrag(state), 5000); state.runtime.time = 18;
  assert.deepEqual(timelineReleaseIntent(state, drag), { type: 'seek', seconds: 11.5 });
});
check('Stale Item rejects ordinary seek and exact-end Next equally', () => {
  const state = fixture(); const drag = beginTimelineDrag(state); const next = playbackTransition(state, { type: 'next' });
  for (const value of [5000, 10000]) assert.deepEqual(timelineReleaseIntent(next, moveTimelineDrag(drag, value)), { type: 'cancel' });
});
check('Natural completion changing active Item rejects old drag', () => {
  const state = fixture(); const drag = moveTimelineDrag(beginTimelineDrag(state), 10000);
  const completed = playbackTransition(state, { type: 'natural-completion' });
  assert.equal(completed.activeItemId, 'a-scene'); assert.deepEqual(timelineReleaseIntent(completed, drag), { type: 'cancel' });
});
check('Duration change outside native tolerance rejects instead of renormalizing preview', () => {
  const state = fixture(); const drag = moveTimelineDrag(beginTimelineDrag(state), 7000); current(state).duration += 0.001;
  assert.deepEqual(timelineReleaseIntent(state, drag), { type: 'cancel' });
});
check('Duration validation uses production absolute float tolerance 0.0001, not strict equality', () => {
  const state = fixture(); const drag = moveTimelineDrag(beginTimelineDrag(state), 5000); current(state).duration += 0.00005;
  assert.deepEqual(timelineReleaseIntent(state, drag), { type: 'seek', seconds: 11.5 });
});
check('Timeline invalidation or removed runtime cancels an existing drag', () => {
  for (const invalid of ['duration', 'runtime', 'active']) {
    const state = fixture(); const drag = moveTimelineDrag(beginTimelineDrag(state), 10000);
    if (invalid === 'duration') current(state).duration = NaN;
    if (invalid === 'runtime') state.runtime = null;
    if (invalid === 'active') state.activeItemId = null;
    assert.deepEqual(timelineReleaseIntent(state, drag), { type: 'cancel' });
  }
});
check('9999 remains ordinary seek on same active Item, without restarting or Next', () => {
  const state = fixture(); const intent = release(state, 9999); const sought = commit(state, intent);
  assert.equal(intent.type, 'seek'); assert.equal(sought.activeItemId, 'a-opening');
  assert.equal(sought.runtime.time, timeForValue(9999, 23)); assert.ok(sought.runtime.time < 23);
});
check('Only exact10000 returns Next;10001 ordinary seek clamps before duration', () => {
  const state = fixture(); assert.deepEqual(release(state, 10000), { type: 'next' });
  const over = release(state, 10001); assert.deepEqual(over, { type: 'seek', seconds: 23 });
  const sought = commit(state, over); assert.equal(sought.activeItemId, 'a-opening'); assert.ok(sought.runtime.time < 23);
});
check('Right-edge Next uses Phase C controller/filtering and preserves different editor selection', () => {
  const state = fixture(); state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = 'b-image';
  const next = commit(state, release(state, 10000));
  assert.deepEqual(next, playbackTransition(state, { type: 'next' })); assert.equal(next.activeItemId, 'a-scene');
  assert.equal(next.selectedPlaylistId, 'playlist-b'); assert.equal(next.selectedItemId, 'b-image');
  assert.equal(next.document.revision, state.document.revision);
});
check('Right-edge Next respects Repeat Current and safe-stops no playable target', () => {
  const repeated = createSessionFixture('playback-repeat-item');
  assert.equal(commit(repeated, release(repeated, 10000)).activeItemId, repeated.activeItemId);
  const missing = createSessionFixture('playback-no-target'); const stopped = commit(missing, release(missing, 10000));
  assert.equal(stopped.runtime, null); assert.equal(stopped.selectedItemId, missing.selectedItemId);
});
check('Ordinary seek preserves pause, actual1.25rate, Master70/Gain50/mute and selection', () => {
  const state = fixture(); state.runtime.paused = true; state.runtime.rate = 1.25; state.runtime.volume = Math.fround(0.35); state.outputMuted = true;
  state.project.playlists[0].masterVolume = 0.7; state.project.playlists[0].items[0].mediaGain = 0.5;
  state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = 'b-image';
  const before = freeze(state); const sought = applyTimelineSeek(before, 11.5);
  assert.equal(sought.runtime.time, 11.5); assert.equal(sought.runtime.paused, true); assert.equal(sought.runtime.rate, 1.25);
  assert.equal(sought.runtime.volume, before.runtime.volume); assert.equal(sought.outputMuted, true);
  assert.deepEqual(sought.project, before.project); assert.deepEqual(sought.document, before.document);
  assert.deepEqual(sought.sequence, before.sequence); assert.equal(sought.selectedPlaylistId, 'playlist-b'); assert.equal(sought.selectedItemId, 'b-image');
  const { time: _time, halted: _halted, ...rest } = sought.runtime; const { time: _oldTime, halted: _oldHalted, ...oldRest } = before.runtime;
  assert.deepEqual(rest, oldRest); assert.equal(before.runtime.time, 8);
});
check('Successful seek clears completion halt while preserving paused state', () => {
  const state = fixture(); state.runtime.halted = true; state.runtime.paused = true;
  const sought = applyTimelineSeek(state, 3); assert.equal(sought.runtime.halted, false); assert.equal(sought.runtime.paused, true);
});
check('Negative seek clamps zero; at/beyond duration uses previous IEEE float instead of Next', () => {
  const state = fixture(); assert.equal(applyTimelineSeek(state, -3).runtime.time, 0);
  assert.equal(applyTimelineSeek(state, 23).runtime.time, 22.999998092651367);
  assert.equal(applyTimelineSeek(state, 999).runtime.time, 22.999998092651367);
  assert.equal(applyTimelineSeek(state, 999).activeItemId, state.activeItemId);
});
check('Seek rejects non-finite or float-overflow targets without mutation', () => {
  const state = freeze(fixture());
  for (const seconds of [NaN, Infinity, -Infinity, 1e50]) assert.equal(applyTimelineSeek(state, seconds), state);
});
check('Deterministic seek failure leaves runtime position/pause/halt/dirty/feedback unchanged', () => {
  const state = freeze({ ...fixture(), faults: { ...fixture().faults, seekFails: true } });
  assert.equal(applyTimelineSeek(state, 5), state);
});
check('Closed Session ignores drag and seek', () => {
  const state = fixture(); const drag = beginTimelineDrag(state); state.closed = true;
  assert.equal(beginTimelineDrag(state), null); assert.equal(applyTimelineSeek(state, 4), state);
  assert.deepEqual(timelineReleaseIntent(state, drag), { type: 'cancel' });
});

check('Natural cross-Playlist prepare/bind failure emits drag cancellation despite unchanged Item/duration', () => {
  for (const scenario of ['playback-prepare-failure', 'playback-cross-failure']) {
    const state = createSessionFixture(scenario); const snapshot = beginTimelineDrag(state);
    const next = playbackTransition(state, { type: 'natural-completion' });
    assert.equal(next.activeItemId, state.activeItemId); assert.equal(current(next).duration, snapshot.duration);
    assert((next.timelineDragCancellation ?? 0) > (state.timelineDragCancellation ?? 0));
    // The target predicate remains Item/duration only; cancellation is a command effect.
    assert.equal(timelineReleaseIntent(next, snapshot).type, 'seek');
    assert.equal(next.timelineInvalidation, state.timelineInvalidation);
  }
});
check('Natural same-Playlist failure and same-Item Repeat do not invent drag cancellation', () => {
  for (const scenario of ['playback-natural-failure', 'playback-repeat-item']) {
    const state = createSessionFixture(scenario), snapshot = moveTimelineDrag(beginTimelineDrag(state), 3000);
    const next = playbackTransition(state, { type: 'natural-completion' });
    assert.equal(next.timelineDragCancellation, state.timelineDragCancellation);
    assert.equal(timelineReleaseIntent(next, snapshot).type, 'seek');
  }
});

const status = checks.every(check => check.pass) ? 'PASS' : 'FAIL';
const result = { suite: 'MainWindow Phase D seek/drag projection', checks: checks.length, passed: checks.filter(check => check.pass).length, status, results: checks };
const directory = fileURLToPath(new URL('../node_modules/.cache/session-phase-d/', import.meta.url));
mkdirSync(directory, { recursive: true });
const output = `${JSON.stringify(result, null, 2)}\n`;
const stage = process.env.DSE_CHECK_STAGE || 'green';
writeFileSync(`${directory}/seek-model-${stage}.json`, output);
writeFileSync(`${directory}/seek-model-${stage}.log`, output);
console.log(output);
if (status !== 'PASS') process.exitCode = 1;
