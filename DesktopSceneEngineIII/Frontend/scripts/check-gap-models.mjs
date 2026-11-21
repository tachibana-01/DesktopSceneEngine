import assert from 'node:assert/strict';
import fs from 'node:fs';
import { createSessionFixture } from '../src/sessionFixtures.ts';
import { createSessionRole, roleTransition } from '../src/sessionLifecycleModel.ts';
import { hideFacts } from '../src/sessionHideModel.ts';
import { displayFacts } from '../src/sessionDisplayModel.ts';
import { presentationTransition, sessionDisplay } from '../src/sessionPresentationModel.ts';
import { createSessionLanguage, languageTransition } from '../src/sessionLanguageModel.ts';

const checks = [];
function check(finding, name, run) { try { run(); checks.push({ finding, name, pass: true }); } catch (error) { checks.push({ finding, name, pass: false, error: String(error) }); } }
const flush = (state, value) => languageTransition(languageTransition(languageTransition(state, { type: 'external-write', value }), { type: 'invalidate' }), { type: 'flush' });
check('F02', 'Only an applied committed change advances Inspector rebuild lifecycle', () => {
  let state = createSessionLanguage();
  for (const action of [{ type: 'draft', value: 'zh_CN' }, { type: 'external-write', value: 'zh_CN' }, { type: 'invalidate' }]) { state = languageTransition(state, action); assert.equal(state.retranslation, 0); }
  state = languageTransition(state, { type: 'flush' }); assert.equal(state.retranslation, 1); assert.equal(state.effective, 'zh_CN');
});
check('F02', 'Same desired and installed language skips rebuild', () => { const state = flush(createSessionLanguage(), 'zh_CN'); assert.equal(flush(state, 'zh_CN').retranslation, 1); });
check('F02', 'Read failure retains UI and transient lifecycle', () => { const state = { ...createSessionLanguage(), readFails: true }; const next = flush(state, 'zh_CN'); assert.equal(next.retranslation, 0); assert.equal(next.effective, 'en'); });
check('F02', 'Catalog fallback still rebuilds; later failed retry rebuilds as production does', () => {
  const state = flush({ ...createSessionLanguage(), catalogAvailable: false }, 'zh_CN'); assert.equal(state.desired, 'zh_CN'); assert.equal(state.effective, 'en'); assert.equal(state.retranslation, 1);
  assert.equal(flush(state, 'zh_CN').retranslation, 2);
});
check('F02', 'Restored catalog applies pending desired language and rebuilds', () => { const state = flush({ ...createSessionLanguage(), catalogAvailable: false }, 'zh_CN'); const next = flush({ ...state, catalogAvailable: true }, 'zh_CN'); assert.equal(next.effective, 'zh_CN'); assert.equal(next.retranslation, 2); });
check('F02', 'Draft, same-language reload and faults do not themselves advance rebuild lifecycle', () => { let state = flush(createSessionLanguage(), 'en'); state = languageTransition(state, { type: 'faults', value: { qtbaseAvailable: false } }); assert.equal(state.retranslation, 0); });

const view = (state, action) => presentationTransition(state, { scope: 'view', action });
check('F04', 'Pending modal warning rejects unrelated user preferences without clearing live choice', () => {
  const pending = view(createSessionFixture('background-save-failure'), { type: 'background', mode: 'reveal' });
  for (const action of [{ type: 'highlight', value: true }, { type: 'background', mode: 'cover' }]) { const next = view(pending, action); assert.equal(next, pending); assert(next.view.warning); assert.equal(next.view.background, 'reveal'); }
});
check('F04', 'Acknowledgement alone clears warning; failed persistence does not roll back live mode', () => {
  const state = createSessionFixture('background-save-failure'), pending = view(state, { type: 'background', mode: 'reveal' }), done = view(pending, { type: 'dismiss-warning' });
  assert.equal(done.view.warning, null); assert.equal(done.view.background, 'reveal'); assert.deepEqual(done.view.persisted, state.view.persisted); assert.deepEqual(done.document, state.document);
});
check('F04', 'External persisted fact cannot silently erase pending acknowledgement', () => {
  const state = view(createSessionFixture('background-save-failure'), { type: 'background', mode: 'reveal' });
  const next = view(state, { type: 'external-background', id: state.view.boundId, mode: 'cover' }); assert.equal(next.view.warning, state.view.warning); assert.equal(next.view.background, 'reveal');
});
check('F04', 'After acknowledgement same-mode reselect does not retry or warn again', () => { const pending = view(createSessionFixture('background-save-failure'), { type: 'background', mode: 'reveal' }); const done = view(pending, { type: 'dismiss-warning' }); assert.equal(view(done, { type: 'background', mode: 'reveal' }), done); assert.equal(done.view.backgroundWrites, 1); });

const display = (state, action, lifecycle) => presentationTransition(state, { scope: 'display', action }, lifecycle);
for (const condition of ['closePending', 'ending']) for (const owned of [false, true]) for (const intent of ['deploy', 'undeploy', 'toggle-deployment']) check('F05', `${condition}, owned=${owned}: fresh ${intent} rejects and preserves ownership`, () => {
  let state = createSessionFixture('normal'); if (owned) state = display(state, { type: 'deploy' });
  const role = roleTransition(createSessionRole(), { type: condition === 'closePending' ? 'close-begin' : 'teardown' });
  const before = structuredClone(sessionDisplay(state));
  assert.equal(displayFacts(state.display, role.lifecycle).deploymentEnabled, false);
  assert.equal(hideFacts(role.hide, false, role.lifecycle).eligible, false);
  const next = display(state, { type: intent }, role.lifecycle);
  assert.equal(next.display.lastResult, 'rejected');
  for (const field of ['desired', 'targetRegistered', 'hostOwned', 'hostHealthy', 'continuationPending', 'lastDeploymentRequestId']) assert.deepEqual(next.display[field], before[field], field);
  for (const field of ['project', 'document', 'runtime', 'selectedPlaylistId', 'selectedItemId', 'activePlaylistId', 'activeItemId']) assert.deepEqual(next[field], state[field], field);
});
check('F05', 'A rendered ready projection cannot authorize execution after Close begins', () => {
  const state = createSessionFixture('normal'), role = createSessionRole(); assert.equal(displayFacts(state.display, role.lifecycle).canDeploy, true);
  const pending = roleTransition(role, { type: 'close-begin' }); assert.equal(display(state, { type: 'deploy' }, pending.lifecycle).display.lastResult, 'rejected');
});
check('F05', 'Display and Hide store no competing lifecycle copies', () => {
  const state = createSessionFixture('normal'), role = createSessionRole();
  for (const field of ['closePending', 'ending']) { assert.equal(field in state.display, false); assert.equal(field in role.hide, false); assert.equal(role.lifecycle[field], false); }
});
check('F05', 'Ordinary Hide and Show keep the same Session lifecycle and deployment', () => {
  const state = display(createSessionFixture('normal'), { type: 'deploy' }); const role = createSessionRole();
  const requested = roleTransition(role, { type: 'request' }), hidden = roleTransition(requested, { type: 'permit', request: requested.hide.pending.request, allowed: true }), shown = roleTransition(hidden, { type: 'restore' });
  assert.equal(hidden.hide.hidden, true); assert.equal(shown.hide.hidden, false); assert.equal(shown.lifecycle, role.lifecycle); assert(displayFacts(state.display, shown.lifecycle).retained);
});
check('F05', 'Cancelled Close re-enables commands without auto-Undeploy', () => { const state = display(createSessionFixture('normal'), { type: 'deploy' }); const pending = roleTransition(createSessionRole(), { type: 'close-begin' }), aborted = roleTransition(pending, { type: 'close-abort' }); assert.equal(displayFacts(state.display, aborted.lifecycle).canUndeploy, true); assert.equal(state.display.desired, true); });
check('F05', 'Ready Deploy/Undeploy and unavailable Display eligibility retain production behavior', () => {
  let state = createSessionFixture('normal'); const role = createSessionRole(); state = display(state, { type: 'deploy' }, role.lifecycle); assert.equal(displayFacts(state.display, role.lifecycle).presentation, 'deployed'); state = display(state, { type: 'undeploy' }, role.lifecycle); assert.equal(displayFacts(state.display, role.lifecycle).presentation, 'preview-only');
  const missing = createSessionFixture('display-unavailable'); assert.equal(displayFacts(missing.display, role.lifecycle).canDeploy, false);
});
check('F05', 'Cleanup-required retained ownership remains retryable only when lifecycle is ready', () => {
  const state = display(createSessionFixture('undeploy-failure'), { type: 'undeploy' }); assert.equal(displayFacts(state.display).presentation, 'cleanup-required'); assert.equal(displayFacts(state.display).canUndeploy, true);
  const role = roleTransition(createSessionRole(), { type: 'close-begin' }); const blocked = display(state, { type: 'undeploy' }, role.lifecycle); assert.equal(blocked.display.lastResult, 'rejected'); assert.equal(blocked.display.targetRegistered, true);
});
const result = { pass: checks.every(check => check.pass), groups: Object.fromEntries([...new Set(checks.map(check => check.finding))].map(id => { const group = checks.filter(check => check.finding === id); return [id, { passed: group.filter(check => check.pass).length, total: group.length }]; })), checks };
fs.writeFileSync(new URL('../node_modules/.cache/semantic-gap-closure/models-' + (result.pass ? 'green' : 'failed') + '.json', import.meta.url), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify(result, null, 2)); if (!result.pass) process.exitCode = 1;
