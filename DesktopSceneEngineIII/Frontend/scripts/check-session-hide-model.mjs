import assert from 'node:assert/strict';
import fs from 'node:fs';
import { hideFacts as projectHideFacts } from '../src/sessionHideModel.ts';
import { createSessionRole, roleTransition as hideTransition } from '../src/sessionLifecycleModel.ts';
const hideFacts = (role, modalBlocked = role.hide.modalBlocked) => projectHideFacts(role.hide, modalBlocked, role.lifecycle);

const checks = [];
function check(name, run) {
  try { run(); checks.push({ name, pass: true }); }
  catch (error) { checks.push({ name, pass: false, error: String(error) }); }
}
const initial = createSessionRole;
const request = (state = initial(), extra = {}) => hideTransition(state, { type: 'request', ...extra });
const receive = (state, allowed = true, extra = {}) => hideTransition(state, {
  type: 'permit', request: state.hide.pending?.request ?? 1, allowed, ...extra,
});
const hidden = () => receive(request());
const facts = (state, local = {}, recovery = {}) => hideTransition(state, { type: 'facts', facts: local, recovery });

check('Qualified visible Session has Hide and Close available before any request', () => {
  const state = initial(); assert.equal(hideFacts(state).qualified, true); assert.equal(hideFacts(state).eligible, true);
  assert.equal(hideFacts(state).canClose, true); assert.equal(state.hide.hidden, false); assert.equal(state.hide.pending, null);
});
for (const [name, value] of Object.entries({
  roleEnding: true, joined: false, recoveryQualified: false, controlConnected: false,
  coordinatorLifetime: false, coordinatorAlive: false, qualificationAgeValid: false, qualificationAgeMs: 3000,
})) {
  check(`Fresh qualification rejects ${name}=${value} instead of trusting an enabled button`, () => {
    const state = facts(initial(), {}, { [name]: value });
    assert.equal(hideFacts(state).qualified, false); assert.equal(hideFacts(state).eligible, false);
    const next = request(state); assert.equal(next.hide.hidden, false); assert.equal(next.hide.pending, null);
  });
}
check('Qualification lifetime is strictly below 3000ms', () => {
  assert.equal(hideFacts(facts(initial(), {}, { qualificationAgeMs: 2999 })).eligible, true);
  assert.equal(hideFacts(facts(initial(), {}, { qualificationAgeMs: 3000 })).eligible, false);
});
check('Absent local window blocks Hide and Close without manufacturing a request', () => {
  const state = facts(initial(), { windowAvailable: false });
  assert.equal(hideFacts(state).eligible, false); assert.equal(hideFacts(state).canClose, false); assert.equal(request(state).hide.pending, null);
});
check('Relevant modal or native disabled-owner fact blocks Hide before permit issuance', () => {
  const state = facts(initial(), { modalBlocked: true });
  assert.equal(hideFacts(state).eligible, false); assert.equal(hideFacts(state).canClose, false);
  assert.equal(request(state).hide.pending, null); assert.equal(request(initial(), { modalBlocked: true }).hide.pending, null);
  assert.equal(hideFacts(initial(), true).eligible, false);
});
check('Modal dismissal re-enables Hide without changing recovery qualification', () => {
  const state = facts(facts(initial(), { modalBlocked: true }), { modalBlocked: false });
  assert.equal(hideFacts(state).eligible, true); assert.equal(hideFacts(state).qualified, true);
});
check('Close Pending blocks Hide and a second ordinary Close', () => {
  const state = hideTransition(initial(), { type: 'close-begin' });
  assert.equal(state.lifecycle.closePending, true); assert.equal(hideFacts(state).eligible, false);
  assert.equal(hideFacts(state).canClose, false); assert.equal(request(state).hide.pending, null);
});
check('Hide request records generation and sequence but remains visible until a permit', () => {
  const state = request(); assert.equal(state.hide.hidden, false); assert.equal(state.hide.pending.request, 1);
  assert.equal(state.hide.pending.showGeneration, 0); assert.equal(state.hide.pending.sentAtMs, 0); assert.equal(state.hide.lastResult, 'requested');
});
check('One in-flight request suppresses duplicate issuance without disabling local eligibility', () => {
  const state = request(); assert.equal(hideFacts(state).eligible, true);
  assert.equal(request(state), state); assert.equal(state.hide.sequence, 1);
});
check('Fresh allowed matching permit hides the same Session and consumes the request', () => {
  const state = hidden(); assert.equal(state.hide.hidden, true); assert.equal(state.hide.pending, null);
  assert.equal(state.hide.lastResult, 'hidden'); assert.equal(state.hide.showGeneration, 0);
});
check('Denied permit is consumed while Session fails visible', () => {
  const state = receive(request(), false); assert.equal(state.hide.hidden, false); assert.equal(state.hide.pending, null);
  assert.equal(state.hide.lastResult, 'permit-denied');
});
check('Unsolicited or wrong request reply cannot consume or authorize another request', () => {
  const state = request(); assert.equal(receive(state, true, { request: 7 }), state);
  assert.equal(hideTransition(initial(), { type: 'permit', request: 1, allowed: true }).hide.hidden, false);
});
check('Consumed permit cannot be replayed after an explicit Show', () => {
  const state = hidden(); const shown = hideTransition(state, { type: 'restore' });
  assert.equal(hideTransition(shown, { type: 'permit', request: 1, allowed: true }), shown);
  assert.equal(shown.hide.hidden, false); assert.equal(shown.hide.showGeneration, 1);
});
check('A newer Show invalidates an in-flight Hide generation even when already visible', () => {
  const requested = request(); const shown = hideTransition(requested, { type: 'restore' });
  const state = receive(shown); assert.equal(state.hide.hidden, false); assert.equal(state.hide.pending, null);
  assert.equal(state.hide.lastResult, 'stale-generation'); assert.equal(state.hide.showGeneration, 1);
});
check('Permit age 2999ms succeeds with current renewed qualification', () => {
  const aged = hideTransition(request(), { type: 'advance', milliseconds: 2999 });
  assert.equal(receive(facts(aged, {}, { qualificationAgeMs: 0 })).hide.hidden, true);
});
check('Permit age 3000ms fails despite a newer qualification reply', () => {
  const aged = hideTransition(request(), { type: 'advance', milliseconds: 3000 });
  const state = receive(facts(aged, {}, { qualificationAgeMs: 0 }));
  assert.equal(state.hide.hidden, false); assert.equal(state.hide.pending, null); assert.equal(state.hide.lastResult, 'permit-expired');
});
check('Recovery loss between request and permit is rechecked locally', () => {
  const state = receive(facts(request(), {}, { recoveryQualified: false }));
  assert.equal(state.hide.hidden, false); assert.equal(state.hide.pending, null); assert.equal(state.hide.lastResult, 'recheck-failed');
});
check('Modal appearing between request and permit rejects Hide and consumes token', () => {
  const state = receive(facts(request(), { modalBlocked: true }));
  assert.equal(state.hide.hidden, false); assert.equal(state.hide.pending, null); assert.equal(state.hide.lastResult, 'recheck-failed');
  assert.equal(receive(request(), true, { modalBlocked: true }).hide.hidden, false);
});
check('Close becoming pending between authorization and commit rejects Hide', () => {
  const state = receive(hideTransition(request(), { type: 'close-begin' }));
  assert.equal(state.hide.hidden, false); assert.equal(state.hide.pending, null); assert.equal(state.lifecycle.closePending, true);
});
check('Known recovery loss restores a hidden Session and supersedes pending generation', () => {
  const state = facts(hidden(), {}, { controlConnected: false });
  assert.equal(state.hide.hidden, false); assert.equal(state.hide.showGeneration, 1); assert.equal(hideFacts(state).eligible, false);
});
check('Expiry observed while hidden fails visible without Engine or document teardown', () => {
  const state = hideTransition(hidden(), { type: 'advance', milliseconds: 3000 });
  assert.equal(state.hide.hidden, false); assert.equal(state.lifecycle.ending, false); assert.equal(state.hide.windowAvailable, true);
});
check('Temporary modal while hidden does not itself trigger a recovery-loss restore', () => {
  const state = facts(hidden(), { modalBlocked: true });
  assert.equal(state.hide.hidden, true); assert.equal(state.hide.showGeneration, 0); assert.equal(hideFacts(state).eligible, false);
});
check('Revoke clears pending permit and fails a hidden Session visible', () => {
  const state = hideTransition(request(hidden()), { type: 'revoke' });
  assert.equal(state.hide.hidden, false); assert.equal(state.hide.pending, null); assert.equal(state.hide.recovery.recoveryQualified, false);
});
check('Successful requalification re-enables Hide without automatically hiding Session', () => {
  const revoked = hideTransition(hidden(), { type: 'revoke' });
  const state = facts(revoked, {}, { recoveryQualified: true, qualificationAgeValid: true, qualificationAgeMs: 0 });
  assert.equal(state.hide.hidden, false); assert.equal(hideFacts(state).eligible, true);
  assert.equal(receive(request(state)).hide.hidden, true);
});
check('Restore does not require recovery authority and increments Show generation each time', () => {
  let state = hideTransition(hidden(), { type: 'revoke' });
  state = hideTransition(state, { type: 'restore' }); state = hideTransition(state, { type: 'restore' });
  assert.equal(state.hide.hidden, false); assert.equal(state.hide.showGeneration, 3); assert.equal(hideFacts(state).qualified, false);
});
check('Late permit and Show cannot resurrect accepted teardown', () => {
  const state = hideTransition(request(), { type: 'teardown' });
  assert.equal(state.lifecycle.ending, true); assert.equal(state.hide.showGeneration, 1); assert.equal(hideFacts(state).eligible, false);
  assert.equal(hideTransition(state, { type: 'restore' }), state);
  assert.equal(receive(state).hide.hidden, false);
});
check('Aborted Close from hidden preserves original hidden presentation when recovery stayed qualified', () => {
  const state = hideTransition(hideTransition(hidden(), { type: 'close-begin' }), { type: 'close-abort' });
  assert.equal(state.lifecycle.closePending, false); assert.equal(state.hide.hidden, true); assert.equal(hideFacts(state).eligible, true);
});
check('Show during a hidden Close decision does not override aborted-close return-to-hidden contract', () => {
  let state = hideTransition(hidden(), { type: 'close-begin' });
  state = hideTransition(state, { type: 'close-interaction' });
  assert.equal(state.hide.hidden, false); assert.equal(state.lifecycle.closePending, true);
  state = hideTransition(state, { type: 'close-abort' }); assert.equal(state.hide.hidden, true); assert.equal(state.hide.showGeneration, 1);
});
check('Recovery lost during a hidden Close permanently forces aborted Close visible even after renewed qualification', () => {
  let state = hideTransition(hidden(), { type: 'close-begin' });
  state = facts(state, {}, { recoveryQualified: false });
  assert.equal(state.hide.closeRecoveryLost, true); assert.equal(state.hide.hidden, false);
  state = facts(state, {}, { recoveryQualified: true });
  state = hideTransition(state, { type: 'close-abort' });
  assert.equal(state.hide.hidden, false); assert.equal(state.lifecycle.closePending, false);
});
check('A remaining modal on aborted hidden Close prevents return to hidden', () => {
  const state = hideTransition(facts(hideTransition(hidden(), { type: 'close-begin' }), { modalBlocked: true }), { type: 'close-abort' });
  assert.equal(state.hide.hidden, false); assert.equal(state.lifecycle.closePending, false);
});
check('Close rejection during an existing modal does not acquire a pending operation', () => {
  const state = facts(initial(), { modalBlocked: true });
  assert.equal(hideTransition(state, { type: 'close-begin' }), state);
});
check('Frozen input remains unchanged and model owns no Project/runtime/deployment values', () => {
  const state = initial(); Object.freeze(state.hide.recovery); Object.freeze(state);
  const result = receive(request(state)); assert.equal(result.hide.hidden, true); assert.equal(state.hide.hidden, false);
  for (const key of ['project', 'document', 'dirty', 'runtime', 'selected', 'active', 'canvas', 'display', 'deployment', 'preferences']) assert.equal(key in result, false);
});
const passed = checks.filter(check => check.pass).length;
const result = { suite: 'MainWindow Phase F Session Hide authority', checks: checks.length, passed, status: passed === checks.length ? 'PASS' : 'FAIL', results: checks };
const stage = process.env.DSE_CHECK_STAGE ?? 'green';
fs.writeFileSync(new URL(`../node_modules/.cache/session-phase-f/hide-model-${stage}.json`, import.meta.url), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify(result, null, 2));
if (result.status !== 'PASS') process.exitCode = 1;
