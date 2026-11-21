import assert from 'node:assert/strict';
import { createSessionFixture, EDITORIAL_PATH, STUDIO_PATH, WRONG_OWNER_PATH, INVALID_PATH, SAVE_AS_PATH } from '../src/sessionFixtures.ts';
import { isDirty, documentName, loadOpenCandidate, saveToMockPath, continueDocumentIntent } from '../src/sessionDocumentMock.ts';
import { entityTransition } from '../src/sessionEntities.ts';

const checks = [];
function check(name, run) {
  run();
  checks.push(name);
  console.log(`PASS ${name}`);
}
function dirtyFixture() {
  const state = createSessionFixture('normal');
  state.document.revision++;
  state.project.playlists[0].alias = 'Current edit';
  return state;
}
function candidate(state, path) {
  const loaded = loadOpenCandidate(state, path);
  assert.equal(loaded.error, null);
  assert(loaded.candidate, 'valid candidate must be admitted');
  return loaded.candidate;
}
function assertReplaced(before, after, path) {
  assert.equal(after.document.path, path);
  assert.equal(after.document.revision, before.document.revision + 1);
  assert.equal(isDirty(after), false);
  assert.equal(after.document.cleanRevision, after.document.revision);
  assert.equal(after.selectedPlaylistId, null);
  assert.equal(after.selectedItemId, null);
  assert.equal(after.activePlaylistId, null);
  assert.equal(after.activeItemId, null);
  assert.equal(after.runtime, null);
  assert.equal(after.project.displayOwner, before.project.displayOwner);
}
function assertPreservedExceptFeedback(before, after) {
  assert.deepEqual({ ...after, feedback: null }, { ...before, feedback: null });
}

check('dirty derives from revision equality; title follows filename with Windows separators', () => {
  const state = dirtyFixture();
  assert.equal(isDirty(state), true);
  state.document.cleanRevision = state.document.revision;
  assert.equal(isDirty(state), false);
  state.document.cleanRevision++;
  assert.equal(isDirty(state), true, 'dirty is inequality, not revision greater-than');
  assert.equal(documentName(state), 'editorial.json');
  state.document.path = 'C:\\MockProjects\\项目.json';
  assert.equal(documentName(state), '项目.json');
  state.document.path = null;
  assert.equal(documentName(state), 'Untitled');
});

check('candidate validation is side effect free and owner admission precedes dirty decision', () => {
  const state = dirtyFixture();
  const before = structuredClone(state);
  const accepted = candidate(state, STUDIO_PATH);
  assert.equal(accepted.path, STUDIO_PATH);
  for (const path of [INVALID_PATH, WRONG_OWNER_PATH, 'C:/MockProjects/unknown-version.json', 'C:/MockProjects/unreadable.json', 'C:/MockProjects/absent.json']) {
    const result = loadOpenCandidate(state, path);
    assert.equal(result.candidate, null);
    assert.equal(result.error?.level, 'error');
    assert.equal(result.error?.title, 'Open Project Failed');
  }
  accepted.project.playlists[0].alias = 'Detached candidate';
  assert.deepEqual(state, before, 'loading does not mutate current state or stored file bytes');
});

check('Save writes detached authored snapshot, marks clean, preserves selection/runtime/revision', () => {
  const state = dirtyFixture();
  const before = structuredClone(state);
  const result = saveToMockPath(state, EDITORIAL_PATH, false);
  assert.equal(result.result, 'saved');
  assert.equal(isDirty(result.state), false);
  assert.equal(result.state.document.path, EDITORIAL_PATH);
  assert.equal(result.state.document.revision, state.document.revision);
  for (const key of ['selectedPlaylistId', 'selectedItemId', 'activePlaylistId', 'activeItemId', 'runtime']) assert.deepEqual(result.state[key], before[key]);
  assert.equal(result.state.feedback, null, 'successful save has clean/title output without success toast');
  assert.deepEqual(state, before, 'input projection must stay immutable');
  result.state.project.playlists[0].alias = 'Further unsaved edit';
  assert.equal(result.state.files[EDITORIAL_PATH].project.playlists[0].alias, 'Current edit');
});

check('Save As adopts path only on success and preserves source strings without rebasing', () => {
  const state = dirtyFixture();
  state.project.playlists[0].items[0].source = 'media/relative.mp4';
  const sources = state.project.playlists.flatMap(value => value.items.map(item => item.source));
  const result = saveToMockPath(state, SAVE_AS_PATH, true);
  assert.equal(result.result, 'saved');
  assert.equal(result.state.document.path, SAVE_AS_PATH);
  assert.equal(isDirty(result.state), false);
  assert.deepEqual(result.state.project.playlists.flatMap(value => value.items.map(item => item.source)), sources);
  assert.deepEqual(result.state.files[SAVE_AS_PATH].project, state.project);
  assert.equal(result.state.runtime.time, 8);
});

check('Save failure preserves identity, clean baseline, authored content and runtime', () => {
  for (const fault of ['injected', 'missing-parent', 'unreadable', 'empty-path']) {
    const state = dirtyFixture();
    if (fault === 'injected') state.faults.saveFails = true;
    const path = fault === 'missing-parent' ? 'C:/MockProjects/missing-parent/project.json' : fault === 'unreadable' ? 'C:/MockProjects/unreadable.json' : fault === 'empty-path' ? '' : SAVE_AS_PATH;
    const before = structuredClone(state);
    const result = saveToMockPath(state, path, true);
    assert.equal(result.result, 'failed');
    assert.equal(result.state.feedback?.title, 'Save Project Failed');
    assert.equal(result.state.feedback?.level, 'error');
    assertPreservedExceptFeedback(before, result.state);
    assert.equal(isDirty(result.state), true);
    assert.deepEqual(state, before);
  }
});

check('Save As destination admits absent/same owner/legacy/unrelated but rejects recognized unsafe DSE', () => {
  const state = dirtyFixture();
  state.files['C:/MockProjects/unrelated.json'] = { kind: 'other' };
  for (const path of [SAVE_AS_PATH, EDITORIAL_PATH, 'C:/MockProjects/legacy.json', INVALID_PATH, 'C:/MockProjects/unrelated.json']) {
    assert.equal(saveToMockPath(state, path, true).result, 'saved', `${path} should permit ordinary overwrite`);
  }
  for (const path of [WRONG_OWNER_PATH, 'C:/MockProjects/invalid-v3.json', 'C:/MockProjects/unknown-version.json', 'C:/MockProjects/unreadable.json']) {
    const result = saveToMockPath(state, path, true);
    assert.equal(result.result, 'failed', `${path} must fail before write`);
    assertPreservedExceptFeedback(state, result.state);
  }
});

check('current Save does not apply Save As owner/destination admission', () => {
  const state = dirtyFixture();
  for (const path of [WRONG_OWNER_PATH, 'C:/MockProjects/invalid-v3.json', 'C:/MockProjects/unknown-version.json']) {
    state.document.path = path;
    const result = saveToMockPath(state, path, false);
    assert.equal(result.result, 'saved');
    assert.equal(result.state.files[path].project.displayOwner, state.project.displayOwner);
    assert.equal(result.state.document.path, path);
  }
});

check('New after Discard creates legal empty clean Untitled, clears all identities, retains owner', () => {
  const state = dirtyFixture();
  state.project.canvasAspect = [21, 9];
  const before = structuredClone(state);
  const next = continueDocumentIntent(state, { kind: 'new' }, 'discard');
  assertReplaced(state, next, null);
  assert.equal(documentName(next), 'Untitled');
  assert.deepEqual(next.project.playlists, []);
  assert.deepEqual(next.project.canvasAspect, [16, 9], 'new document seeded from fixture bound display, not old authored ratio');
  assert.equal(next.nextPlaylistOrdinal, '0');
  assert.deepEqual(next.files, before.files, 'Discard does not persist current edits');
  assert.deepEqual(state, before);
});

check('New after successful Save retains saved file content before replacement', () => {
  const state = dirtyFixture();
  const saved = saveToMockPath(state, EDITORIAL_PATH, false);
  assert.equal(saved.result, 'saved');
  const next = continueDocumentIntent(saved.state, { kind: 'new' }, 'saved');
  assertReplaced(saved.state, next, null);
  assert.equal(next.files[EDITORIAL_PATH].project.playlists[0].alias, 'Current edit');
});

check('Open Discard commits detached preloaded candidate; never auto-selects loaded objects', () => {
  const state = dirtyFixture();
  const preloaded = candidate(state, STUDIO_PATH);
  state.files[STUDIO_PATH].project.playlists[0].alias = 'Changed disk after preload';
  const next = continueDocumentIntent(state, { kind: 'open', candidate: preloaded }, 'discard');
  assertReplaced(state, next, STUDIO_PATH);
  assert.equal(next.project.playlists[0].alias, 'Studio', 'Discard uses original validated candidate');
  preloaded.project.playlists[0].alias = 'Mutated stale reference';
  assert.equal(next.project.playlists[0].alias, 'Studio');
  assert.equal(next.nextPlaylistOrdinal, '0');
});

check('same-path Save → Open reload defeats stale candidate and leaves saved edits clean', () => {
  const state = dirtyFixture();
  const stale = candidate(state, EDITORIAL_PATH);
  assert.equal(stale.project.playlists[0].alias, 'Playlist A');
  const saved = saveToMockPath(state, EDITORIAL_PATH, false);
  assert.equal(saved.result, 'saved');
  const next = continueDocumentIntent(saved.state, { kind: 'open', candidate: stale }, 'saved');
  assertReplaced(saved.state, next, EDITORIAL_PATH);
  assert.equal(next.project.playlists[0].alias, 'Current edit');
  assert.equal(next.nextPlaylistOrdinal, '4');
});

check('different-path Save → Open reloads candidate while preserving saved current snapshot', () => {
  const state = dirtyFixture();
  const preloaded = candidate(state, STUDIO_PATH);
  const saved = saveToMockPath(state, EDITORIAL_PATH, false);
  saved.state.files[STUDIO_PATH].project.playlists[0].alias = 'Re-read Studio';
  const next = continueDocumentIntent(saved.state, { kind: 'open', candidate: preloaded }, 'saved');
  assertReplaced(saved.state, next, STUDIO_PATH);
  assert.equal(next.project.playlists[0].alias, 'Re-read Studio');
  assert.equal(next.files[EDITORIAL_PATH].project.playlists[0].alias, 'Current edit');
});

check('failed candidate reload preserves legitimate prior Save effects and current runtime', () => {
  for (const fault of ['invalid', 'wrong-owner']) {
    const state = createSessionFixture('dirty-untitled');
    state.faults.reload = fault;
    const preloaded = candidate(state, STUDIO_PATH);
    const saved = saveToMockPath(state, SAVE_AS_PATH, true);
    assert.equal(saved.result, 'saved');
    const next = continueDocumentIntent(saved.state, { kind: 'open', candidate: preloaded }, 'saved');
    assertPreservedExceptFeedback(saved.state, next);
    assert.equal(next.document.path, SAVE_AS_PATH, 'successful Save As adoption is not rolled back');
    assert.equal(isDirty(next), false, 'successful Save clean baseline is not rolled back');
    assert.equal(next.runtime.time, 8);
    assert.equal(next.feedback?.title, 'Open Project Failed');
  }
});

check('Close stops active runtime but retains document, selected identities and dirty/discard state', () => {
  const state = dirtyFixture();
  const next = continueDocumentIntent(state, { kind: 'close' }, 'discard');
  assert.equal(next.closed, true);
  assert.equal(next.runtime, null);
  assert.equal(next.activePlaylistId, null);
  assert.equal(next.activeItemId, null);
  assert.equal(next.selectedPlaylistId, state.selectedPlaylistId);
  assert.equal(next.selectedItemId, state.selectedItemId);
  assert.deepEqual(next.project, state.project);
  assert.deepEqual(next.document, state.document);
  assert.equal(isDirty(next), true, 'Discard close does not save or mark authored content clean');
  const saved = saveToMockPath(state, EDITORIAL_PATH, false);
  const savedClose = continueDocumentIntent(saved.state, { kind: 'close' }, 'saved');
  assert.equal(savedClose.closed, true);
  assert.equal(isDirty(savedClose), false);
});

check('Cancel/failure abort leaves destructive continuation undispatched', () => {
  const state = dirtyFixture();
  const before = structuredClone(state);
  candidate(state, STUDIO_PATH);
  assert.deepEqual(state, before, 'Cancel at dirty or picker surface dispatches no continuation');
  state.faults.saveFails = true;
  const failure = saveToMockPath(state, SAVE_AS_PATH, true);
  assert.equal(failure.result, 'failed');
  assertPreservedExceptFeedback(state, failure.state);
  assert.equal(failure.state.closed, false);
});

check('Open replacement recomputes default Playlist suffix ordinal without using count', () => {
  const state = dirtyFixture();
  state.files[STUDIO_PATH].project.playlists.push({ id: 'suffix', alias: 'Playlist AA', enabled: true, items: [] });
  state.files[STUDIO_PATH].project.playlists.push({ id: 'overflow-suffix', alias: 'Playlist ZZZZZZZZZZZZZZZZZZZZ', enabled: true, items: [] });
  const next = continueDocumentIntent(state, { kind: 'open', candidate: candidate(state, STUDIO_PATH) }, 'discard');
  assert.equal(next.nextPlaylistOrdinal, '27', 'next suffix after AA is AB; overflowing native ordinal suffix is ignored');
});

check('Rename → Save → Open → Add preserves native uint64 suffix precision', () => {
  let state = entityTransition(createSessionFixture('normal'), { type: 'rename-playlist', id: 'playlist-a', alias: 'Playlist BKTXHSOGHKKG' });
  const original = structuredClone(state.project);
  assert.doesNotThrow(() => JSON.stringify(state), 'Exact mock counter remains JSON compatible');
  state = saveToMockPath(state, EDITORIAL_PATH, false).state;
  assert.deepEqual(state.files[EDITORIAL_PATH].project, original, 'Save persists authored project only; counter is derived, not a document format change');
  state = continueDocumentIntent(state, { kind: 'open', candidate: candidate(state, EDITORIAL_PATH) }, 'discard');
  assert.deepEqual(state.project, original, 'Reopen preserves all existing authored identities and names');
  state = entityTransition(state, { type: 'add-playlist' });
  assert.equal(state.project.playlists.at(-1).alias, 'Playlist BKTXHSOGHKKH');
  assert.deepEqual(state.project.playlists.slice(0, -1), original.playlists, 'Add preserves existing Playlists and Items');
  assert(!original.playlists.some(entry => entry.alias === state.project.playlists.at(-1).alias));
  assert(!original.playlists.some(entry => entry.id === state.project.playlists.at(-1).id));
});

console.log(JSON.stringify({ suite: 'MainWindow Phase A document contract', checks: checks.length, status: 'PASS' }));
