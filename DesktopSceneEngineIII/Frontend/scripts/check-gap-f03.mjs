import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createSessionFixture, mockSources } from '../src/sessionFixtures.ts';
import { entityTransition, findItem } from '../src/sessionEntities.ts';

const checks = [];
function check(name, test) {
  try { test(); checks.push({ name, pass: true }); }
  catch (error) { checks.push({ name, pass: false, error: error.message }); }
}

const faults = ['none', 'rebuild-prepare', 'rebuild-bind', 'restore-seek', 'rollback-bind', 'rollback-seek'];
const syncMessages = {
  add: 'Newly added media could not be synchronized with active playback. It will be available the next time the playlist is played.',
  remove: 'Playlist changes could not be synchronized with active playback. They will be applied the next time the playlist is played.',
};

function initialState(fault, paused = true, muted = true) {
  const state = createSessionFixture('normal');
  state.selectedItemId = 'a-scene'; // Editor selection differs from active a-opening.
  state.project.playlists[0].masterVolume = .7;
  state.project.playlists[0].items[0].mediaGain = .5;
  state.runtime.items = structuredClone(state.project.playlists[0].items);
  state.runtime.time = 23;
  state.runtime.paused = paused;
  state.runtime.rate = 1.7;
  state.runtime.volume = Math.fround(Math.fround(.7) * .5);
  state.outputMuted = muted;
  state.faults.playback = fault;
  return state;
}

for (const operation of ['add', 'remove']) {
  for (const fault of faults) {
    const initial = initialState(fault);
    const inputSnapshot = structuredClone(initial);
    const next = entityTransition(initial, operation === 'add'
      ? { type: 'add-media', sources: [mockSources[0]] }
      : { type: 'delete-item', id: 'a-scene' });
    const name = `F03 ${operation} / ${fault}`;
    const stopped = fault === 'rollback-bind';
    const freshProjection = fault === 'none' || fault === 'restore-seek';

    check(`${name}: authored mutation succeeds independently of runtime outcome`, () => {
      assert.equal(next.project.playlists[0].items.length, initial.project.playlists[0].items.length + (operation === 'add' ? 1 : -1));
      if (operation === 'add') assert.equal(next.project.playlists[0].items.at(-1).source, mockSources[0].source);
      else assert.equal(findItem(next, 'a-scene'), undefined);
      assert.equal(next.faults.playback, fault);
    });
    check(`${name}: successful authored mutation marks document dirty`, () => {
      assert.equal(next.document.revision, initial.document.revision + 1);
      assert.equal(next.document.cleanRevision, initial.document.cleanRevision);
      assert.notEqual(next.document.revision, next.document.cleanRevision);
      assert.equal(next.document.path, initial.document.path);
    });
    check(`${name}: selection and active identities follow authored/runtime results`, () => {
      assert.deepEqual([next.selectedPlaylistId, next.selectedItemId], ['playlist-a', operation === 'add' ? 'a-scene' : null]);
      assert.deepEqual([next.activePlaylistId, next.activeItemId], stopped ? [null, null] : ['playlist-a', 'a-opening']);
    });
    check(`${name}: runtime retains old projection or binds updated projection exactly`, () => {
      if (stopped) assert.equal(next.runtime, null);
      else assert.deepEqual(next.runtime.items, freshProjection ? next.project.playlists[0].items : initial.runtime.items);
    });
    check(`${name}: position outcome preserves pause, actual rate, audio and mute`, () => {
      assert.equal(next.outputMuted, initial.outputMuted);
      assert.equal(next.project.playlists[0].masterVolume, .7);
      assert.equal(findItem(next, 'a-opening').item.mediaGain, .5);
      if (stopped) assert.equal(next.runtime, null);
      else {
        const time = fault === 'restore-seek' || fault === 'rollback-seek' ? 0 : initial.runtime.time;
        assert.deepEqual({ ...next.runtime, items: undefined }, { ...initial.runtime, time, items: undefined });
      }
    });
    check(`${name}: caller emits the exact operation-specific warning`, () => {
      const expected = fault === 'none' ? null : fault === 'restore-seek'
        ? { level: 'warning', title: 'Playlist updated', message: 'Playlist updated, but the previous playback position could not be restored.' }
        : { level: 'warning', title: 'Unable to synchronize playback', message: syncMessages[operation] };
      assert.deepEqual(next.feedback, expected);
      if (operation === 'add') assert.deepEqual(next.feedbackQueue ?? [], expected ? [expected] : []);
    });
    check(`${name}: transition leaves the caller's source state intact`, () => {
      assert.deepEqual(initial, inputSnapshot);
    });
  }
  check(`F03 ${operation} / rollback-seek: playing unmuted state remains playing and unmuted at zero`, () => {
    const initial = initialState('rollback-seek', false, false);
    const next = entityTransition(initial, operation === 'add'
      ? { type: 'add-media', sources: [mockSources[0]] }
      : { type: 'delete-item', id: 'a-scene' });
    assert.deepEqual(next.runtime, { ...initial.runtime, time: 0 });
    assert.equal(next.outputMuted, false);
    assert.deepEqual([next.activePlaylistId, next.activeItemId], ['playlist-a', 'a-opening']);
    assert.equal(next.feedback.message, syncMessages[operation]);
  });
}

const failed = checks.filter(check => !check.pass);
const result = { suite: 'semantic-gap-f03', passed: checks.length - failed.length, failed: failed.length, checks };
const stage = process.argv.find(value => value.startsWith('--evidence='))?.split('=')[1] ?? 'green';
if (!['red', 'green'].includes(stage)) throw new Error('Evidence stage must be red or green.');
const directory = fileURLToPath(new URL('../node_modules/.cache/semantic-gap-closure/', import.meta.url));
mkdirSync(directory, { recursive: true });
writeFileSync(`${directory}/f03-${stage}.json`, JSON.stringify(result, null, 2));
writeFileSync(`${directory}/f03-${stage}.log`, checks.map(value => `${value.pass ? 'PASS' : 'FAIL'} ${value.name}${value.error ? `\n${value.error}` : ''}`).join('\n'));
console.log(JSON.stringify(result, null, 2));
process.exitCode = failed.length ? 1 : 0;
