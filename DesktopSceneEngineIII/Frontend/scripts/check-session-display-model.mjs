import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createSessionDisplay, displayFacts, displayTransition } from '../src/sessionDisplayModel.ts';

const checks = [];
function check(name, test) {
  try { test(); checks.push({ name, pass: true }); }
  catch (error) { checks.push({ name, pass: false, error: error.message }); }
}
const a = 'mock-persistent-display-a'; const b = 'mock-persistent-display-b';
const initial = () => createSessionDisplay();
const act = (state, type, fields = {}) => displayTransition(state, { type, ...fields });
const enumerate = (state, displays, status = 'success') => act(state, 'enumerate', { result: { status, displays } });
const fault = (state, faults) => act(state, 'faults', { faults });
const deployed = () => act(initial(), 'deploy');
const missing = { status: 'success', displays: [{ id: b, displayNumber: 1, friendlyName: 'Other primary', pixelWidth: 1920, pixelHeight: 1080, primary: true }] };
const failed = { status: 'api-failure', displays: [] };
function freeze(value) {
  if (value && typeof value === 'object' && !Object.isFrozen(value)) {
    Object.freeze(value); for (const child of Object.values(value)) freeze(child);
  }
  return value;
}

check('Initial session has exact persistent A binding and available metadata, Preview-only', () => {
  const state = initial(); const facts = displayFacts(state);
  assert.equal(state.boundId, a); assert.equal(facts.descriptor.id, a);
  assert.equal(facts.availability, 'available'); assert.equal(facts.presentation, 'preview-only');
  assert.equal(state.desired, false); assert.equal(state.targetRegistered, false); assert.equal(state.hostOwned, false);
  assert.equal(facts.deploymentLabel, 'Deploy'); assert.equal(facts.deploymentEnabled, true);
});
check('MainWindow metadata uses number/name/dimensions/Primary with unabridged tooltip', () => {
  const state = initial(); const descriptor = displayFacts(state).descriptor; const facts = displayFacts(state);
  assert.equal(descriptor.displayNumber, 1); assert.equal(descriptor.pixelWidth, 2560); assert.equal(descriptor.pixelHeight, 1440);
  assert.match(facts.label, /^Display 1 — .+ — 2560×1440 \(Primary\)$/); assert.equal(facts.tooltip, facts.label);
});
check('Unknown bound identity does not manufacture descriptor or fallback to available primary', () => {
  const state = createSessionDisplay('unknown-persistent-id'); const facts = displayFacts(state);
  assert.equal(state.boundId, 'unknown-persistent-id'); assert.equal(facts.descriptor, null);
  assert.equal(facts.availability, 'unavailable'); assert.equal(facts.canDeploy, false); assert.equal(facts.canIdentify, false);
});
check('Display number and ordering are presentation, exact ID resolution survives reorder', () => {
  const state = initial(); const list = state.enumeration.displays.slice().reverse().map(d => ({ ...d, displayNumber: d.id === a ? 8 : 1, primary: d.id !== a }));
  const next = enumerate(state, list); const facts = displayFacts(next);
  assert.equal(facts.descriptor.id, a); assert.equal(facts.descriptor.displayNumber, 8); assert.match(facts.label, /^Display 8/);
  assert.equal(facts.descriptor.primary, false); assert.equal(next.boundId, a);
});
check('Missing bound Display remains unavailable despite unrelated primary', () => {
  const next = act(initial(), 'enumerate', { result: missing }); const facts = displayFacts(next);
  assert.equal(next.boundId, a); assert.equal(facts.descriptor, null); assert.equal(facts.label, 'Selected display unavailable');
  assert.equal(facts.canIdentify, false); assert.equal(facts.deploymentEnabled, false);
});
check('Enumeration failure has different facts/label from successful absence', () => {
  for (const status of ['api-failure', 'incomplete-path-metadata', 'duplicate-identity']) {
    const next = enumerate(initial(), initial().enumeration.displays, status); const facts = displayFacts(next);
    assert.equal(facts.availability, 'enumeration-failed'); assert.equal(facts.label, 'Unable to enumerate displays');
    assert.equal(facts.descriptor, null); assert.equal(facts.canIdentify, false); assert.equal(facts.canDeploy, false);
  }
});
check('Ambiguous duplicate exact IDs resolve unavailable without choosing first match', () => {
  const state = initial(); const descriptor = state.enumeration.displays.find(d => d.id === a);
  assert.equal(displayFacts(enumerate(state, [descriptor, { ...descriptor, displayNumber: 9 }])).descriptor, null);
});
check('Exact bound Display return restores eligibility without rebinding', () => {
  const state = initial(); const next = enumerate(act(state, 'enumerate', { result: missing }), state.enumeration.displays);
  assert.equal(next.boundId, a); assert.equal(displayFacts(next).availability, 'available');
  assert.equal(displayFacts(next).canIdentify, true); assert.equal(displayFacts(next).canDeploy, true);
});
check('Long friendly name/identity remain complete and secondary has no Primary suffix', () => {
  const id = 'persistent-device-path-' + '0123456789'.repeat(16); const name = 'Very long professional display 名称 '.repeat(8);
  const state = enumerate(createSessionDisplay(id), [{ id, displayNumber: 27, friendlyName: name, pixelWidth: 7680, pixelHeight: 4320, primary: false }]);
  const facts = displayFacts(state); assert.equal(facts.boundId, id); assert.equal(facts.descriptor.id, id);
  assert.equal(facts.tooltip, `Display 27 — ${name} — 7680×4320`); assert.doesNotMatch(facts.label, /\(Primary\)/);
});
check('Empty friendly name preserves Display number and dimensions without invented fallback name', () => {
  const state = enumerate(initial(), [{ id: a, displayNumber: 4, friendlyName: '', pixelWidth: 1920, pixelHeight: 1200, primary: false }]);
  assert.equal(displayFacts(state).label, 'Display 4 — 1920×1200');
});
check('Identify requests exact bound ID and 2500ms temporary overlay, no identity promotion', () => {
  const state = initial(); const next = act(state, 'identify');
  assert.equal(next.counters.identifyRequests, 1); assert.equal(next.lastIdentifyRequestId, a); assert.equal(next.identifyStatus, 'shown');
  assert.equal(next.identifyDurationMilliseconds, 2500); assert.equal(next.boundId, state.boundId);
  assert.deepEqual(next.enumeration, state.enumeration); assert.equal(next.counters.enumerations, state.counters.enumerations);
  assert.equal(displayFacts(next).availability, 'available'); assert.equal('verified' in next, false);
});
check('Identify unavailable is inert and does not refresh or request overlay', () => {
  const state = act(initial(), 'enumerate', { result: missing }); const next = act(state, 'identify');
  assert.equal(next.counters.identifyRequests, 0); assert.equal(next.counters.enumerations, state.counters.enumerations);
  assert.equal(next.lastIdentifyRequestId, null);
});
check('Identify enumeration failure is inert even if stale descriptor list contains bound ID', () => {
  const state = enumerate(initial(), initial().enumeration.displays, 'api-failure');
  const next = act(state, 'identify'); assert.equal(next.counters.identifyRequests, 0); assert.equal(next.boundId, a);
});
check('Identify failure refreshes facts, never promotes/rebinds and has no generic warning', () => {
  for (const status of ['unresolved', 'api-failure', 'invalid-request']) {
    const state = initial(); const next = act(state, 'identify', { status, refresh: missing });
    assert.equal(next.identifyStatus, status); assert.equal(next.counters.identifyRequests, 1);
    assert.equal(next.counters.enumerations, state.counters.enumerations + 1); assert.equal(next.boundId, a);
    assert.equal(displayFacts(next).availability, 'unavailable'); assert.equal('warning' in next, false);
  }
});
check('Identify capability depends on exact resolution, independently of Engine readiness', () => {
  const state = act(initial(), 'capability', { engineReady: false });
  assert.equal(displayFacts(state).canIdentify, true); assert.equal(act(state, 'identify').identifyStatus, 'shown');
  assert.equal(displayFacts(state).canDeploy, false);
});
check('Deploy explicitly creates desired intent, live host and registered target for bound Display', () => {
  const state = deployed(); const facts = displayFacts(state);
  assert.equal(state.lastResult, 'completed'); assert.equal(state.desired, true); assert.equal(state.targetRegistered, true);
  assert.equal(state.hostOwned, true); assert.equal(state.hostHealthy, true); assert.equal(state.lastDeploymentRequestId, a);
  assert.equal(state.counters.hostCreateAttempts, 1); assert.equal(facts.retained, true); assert.equal(facts.healthy, true);
  assert.equal(facts.presentation, 'deployed'); assert.equal(facts.deploymentLabel, 'Undeploy'); assert.equal(facts.deploymentEnabled, true);
});
check('Display model is media/Project independent; empty Canvas can Deploy', () => {
  const state = initial(); assert.equal('project' in state, false); assert.equal('runtime' in state, false);
  assert.equal(act(state, 'deploy').lastResult, 'completed');
});
check('Repeated explicit Deploy is AlreadySatisfied and never creates duplicate endpoint', () => {
  const state = deployed(); const next = act(state, 'deploy');
  assert.equal(next.lastResult, 'already-satisfied'); assert.equal(next.counters.hostCreateAttempts, state.counters.hostCreateAttempts);
  assert.equal(next.targetRegistered, true);
});
check('Undeploy cancels intent and releases target before host; repeated explicit Undeploy is no-op', () => {
  const state = act(deployed(), 'undeploy'); assert.equal(state.lastResult, 'completed'); assert.equal(state.desired, false);
  assert.equal(state.targetRegistered, false); assert.equal(state.hostOwned, false); assert.equal(displayFacts(state).presentation, 'preview-only');
  assert.equal(state.counters.detachAttempts, 1); assert.equal(state.counters.destroyAttempts, 1);
  const repeated = act(state, 'undeploy'); assert.equal(repeated.lastResult, 'already-satisfied');
  assert.equal(repeated.counters.destroyAttempts, state.counters.destroyAttempts);
});
check('Toggle derives Deploy/Undeploy from retained state, including cancelled-but-owned state', () => {
  const start = act(initial(), 'toggle-deployment'); assert.equal(start.desired, true);
  const retained = act(fault(start, { detach: true }), 'toggle-deployment'); assert.equal(retained.desired, false);
  assert.equal(displayFacts(retained).deploymentLabel, 'Undeploy');
  const cleared = act(fault(retained, { detach: false }), 'toggle-deployment'); assert.equal(displayFacts(cleared).retained, false);
});
check('Deploy re-resolves fresh exact-bound facts and rejects stale cached availability', () => {
  const state = act(initial(), 'deploy', { refresh: missing });
  assert.equal(state.lastResult, 'rejected'); assert.equal(state.desired, false); assert.equal(state.boundId, a);
  assert.equal(state.counters.hostCreateAttempts, 0); assert.equal(displayFacts(state).canDeploy, false);
});
check('Engine/replacement/closing/ending capability gates commands without discarding ownership', () => {
  for (const fields of [{ engineReady: false }, { replacementInProgress: true }, { closePending: true }, { ending: true }]) {
    const lifecycle = { closePending: !!fields.closePending, ending: !!fields.ending };
    const state = act(initial(), 'capability', fields); const next = displayTransition(state, { type: 'deploy' }, lifecycle);
    assert.equal(displayFacts(state, lifecycle).deploymentEnabled, false); assert.equal(next.lastResult, 'rejected'); assert.equal(next.desired, false);
    const owned = act(deployed(), 'capability', fields); const denied = displayTransition(owned, { type: 'undeploy' }, lifecycle);
    assert.equal(displayFacts(owned, lifecycle).deploymentLabel, 'Undeploy'); assert.equal(displayFacts(owned, lifecycle).deploymentEnabled, false);
    assert.equal(denied.lastResult, 'rejected'); assert.equal(denied.targetRegistered, true); assert.equal(denied.desired, true);
  }
});
check('Platform host failures keep initial Preview-only and require explicit new Deploy', () => {
  for (const hostCreate of ['display-unavailable', 'transiently-unavailable', 'unsupported-shell-topology', 'native-failure']) {
    const state = act(fault(initial(), { hostCreate }), 'deploy');
    assert.equal(state.lastResult, 'failure'); assert.equal(state.desired, false); assert.equal(state.hostOwned, false);
    assert.equal(state.targetRegistered, false); assert.equal(state.continuationPending, false); assert.equal(displayFacts(state).deploymentLabel, 'Deploy');
    const event = act(state, 'native-event'); assert.equal(event.counters.hostCreateAttempts, 1);
    assert.equal(act(fault(event, { hostCreate: 'ready' }), 'deploy').lastResult, 'completed');
  }
});
check('Attach failure cleans live host and restores Preview-only without new warning', () => {
  const state = act(fault(initial(), { attach: true }), 'deploy');
  assert.equal(state.lastResult, 'failure'); assert.equal(state.targetRegistered, false); assert.equal(state.hostOwned, false);
  assert.equal(state.desired, false); assert.equal(state.counters.destroyAttempts, 1); assert.equal('warning' in state, false);
});
check('Pacing failure detaches registered target then cleans host', () => {
  const state = act(fault(initial(), { pacing: true }), 'deploy');
  assert.equal(state.lastResult, 'failure'); assert.equal(state.targetRegistered, false); assert.equal(state.hostOwned, false);
  assert.equal(state.counters.detachAttempts, 1); assert.equal(state.counters.destroyAttempts, 1);
});
check('Partial initial failure with failed cleanup retains host and Undeploy eligibility', () => {
  const state = act(fault(initial(), { attach: true, destroy: true }), 'deploy');
  assert.equal(state.lastResult, 'failure'); assert.equal(state.desired, false); assert.equal(state.targetRegistered, false);
  assert.equal(state.hostOwned, true); assert.equal(displayFacts(state).retained, true); assert.equal(displayFacts(state).healthy, false);
  assert.equal(displayFacts(state).deploymentLabel, 'Undeploy'); assert.equal(displayFacts(state).canUndeploy, true);
});
check('Partial pacing failure with detach rejection retains both target and borrowed host', () => {
  const state = act(fault(initial(), { pacing: true, detach: true }), 'deploy');
  assert.equal(state.desired, false); assert.equal(state.targetRegistered, true); assert.equal(state.hostOwned, true);
  assert.equal(state.counters.destroyAttempts, 0); assert.equal(displayFacts(state).presentation, 'cleanup-required');
});
check('Undeploy detach failure clears intent but retains target/host, allows retry while absent', () => {
  const state = act(fault(act(deployed(), 'enumerate', { result: missing }), { detach: true }), 'undeploy');
  assert.equal(state.lastResult, 'failure'); assert.equal(state.desired, false); assert.equal(state.targetRegistered, true); assert.equal(state.hostOwned, true);
  assert.equal(displayFacts(state).canUndeploy, true); assert.equal(displayFacts(state).deploymentEnabled, true);
  assert.equal(state.counters.destroyAttempts, 0); assert.equal(state.continuationPending, false);
  const retry = act(fault(state, { detach: false }), 'undeploy'); assert.equal(retry.lastResult, 'completed'); assert.equal(displayFacts(retry).retained, false);
});
check('Undeploy host destroy failure clears graphics ownership but retains native ownership', () => {
  const state = act(fault(deployed(), { destroy: true }), 'undeploy');
  assert.equal(state.targetRegistered, false); assert.equal(state.hostOwned, true); assert.equal(state.desired, false);
  assert.equal(state.lastResult, 'failure'); assert.equal(displayFacts(state).deploymentLabel, 'Undeploy');
  assert.equal(act(fault(state, { destroy: false }), 'undeploy').lastResult, 'completed');
});
check('Cached display loss alone retains endpoint facts and fixed binding until reconciliation', () => {
  const state = act(deployed(), 'enumerate', { result: missing });
  assert.equal(state.targetRegistered, true); assert.equal(state.hostOwned, true); assert.equal(state.desired, true); assert.equal(state.boundId, a);
  assert.equal(displayFacts(state).healthy, true); assert.equal(displayFacts(state).deploymentLabel, 'Undeploy');
  assert.equal(displayFacts(state).availability, 'unavailable'); assert.equal(displayFacts(state).presentation, 'deployed');
});
check('Native display loss removes endpoint but retains desired intent, no fallback, Undeploy remains', () => {
  const state = act(deployed(), 'native-event', { result: missing });
  assert.equal(state.boundId, a); assert.equal(state.desired, true); assert.equal(state.targetRegistered, false); assert.equal(state.hostOwned, false);
  assert.equal(state.continuationPending, true); assert.equal(displayFacts(state).retained, true); assert.equal(displayFacts(state).canUndeploy, true);
  assert.equal(displayFacts(state).presentation, 'cleanup-required'); assert.equal(state.counters.hostCreateAttempts, 2);
});
check('Transient recovery has one finite continuation, no self-rearming loop', () => {
  const state = act(deployed(), 'native-event', { result: missing }); const once = act(state, 'continue-reconciliation');
  assert.equal(once.counters.hostCreateAttempts, 3); assert.equal(once.continuationPending, false); assert.equal(once.desired, true);
  const twice = act(once, 'continue-reconciliation'); assert.equal(twice.counters.hostCreateAttempts, 3);
});
check('Later exact Display return reconciles retained intent to healthy deployment', () => {
  const initialState = deployed(); const lost = act(initialState, 'native-event', { result: missing });
  const recovered = act(lost, 'native-event', { result: initialState.enumeration });
  assert.equal(recovered.boundId, a); assert.equal(recovered.desired, true); assert.equal(displayFacts(recovered).presentation, 'deployed');
  assert.equal(recovered.continuationPending, false); assert.equal(recovered.counters.hostCreateAttempts, 3);
});
check('Enumeration failure while retained keeps cancellation authority and distinct display fact', () => {
  const state = act(deployed(), 'native-event', { result: failed }); const facts = displayFacts(state);
  assert.equal(facts.availability, 'enumeration-failed'); assert.equal(facts.retained, true); assert.equal(facts.deploymentLabel, 'Undeploy');
  assert.equal(facts.canIdentify, false); assert.equal(facts.deploymentEnabled, true);
  assert.equal(act(state, 'undeploy').lastResult, 'completed');
});
check('External native host loss does not silently drop registered target ownership', () => {
  const state = act(deployed(), 'host-lost');
  assert.equal(state.hostOwned, false); assert.equal(state.targetRegistered, true); assert.equal(state.desired, true);
  assert.equal(displayFacts(state).presentation, 'cleanup-required'); assert.equal(displayFacts(state).canUndeploy, true);
  const recovered = act(state, 'native-event'); assert.equal(displayFacts(recovered).presentation, 'deployed');
  assert.equal(recovered.counters.hostCreateAttempts, 2);
});
check('Host loss plus detach rejection retains stale target without publishing replacement', () => {
  const state = act(fault(act(deployed(), 'host-lost'), { detach: true }), 'native-event');
  assert.equal(state.targetRegistered, true); assert.equal(state.hostOwned, false); assert.equal(state.desired, true);
  assert.equal(state.counters.hostCreateAttempts, 1); assert.equal(state.continuationPending, false);
  const recovered = act(fault(state, { detach: false }), 'native-event'); assert.equal(displayFacts(recovered).healthy, true);
});
check('Healthy native attachment refresh does not recreate target or host', () => {
  const state = deployed(); const next = act(state, 'native-event');
  assert.equal(next.counters.hostCreateAttempts, 1); assert.equal(next.counters.detachAttempts, 0); assert.equal(displayFacts(next).healthy, true);
});
check('Structural/native terminal recovery failures retain intent without self retry', () => {
  for (const [attachment, hostCreate] of [['structurally-unsupported', 'unsupported-shell-topology'], ['native-failure', 'native-failure']]) {
    const state = act(fault(deployed(), { attachment, hostCreate }), 'native-event');
    assert.equal(state.desired, true); assert.equal(state.targetRegistered, false); assert.equal(state.hostOwned, false);
    assert.equal(state.continuationPending, false); assert.equal(state.counters.hostCreateAttempts, 2); assert.equal(displayFacts(state).retained, true);
  }
});
check('Cancelled intent prevents recreation on later display/native event', () => {
  const state = act(act(deployed(), 'native-event', { result: missing }), 'undeploy');
  assert.equal(state.desired, false); const event = act(state, 'native-event', { result: initial().enumeration });
  assert.equal(event.counters.hostCreateAttempts, state.counters.hostCreateAttempts); assert.equal(displayFacts(event).presentation, 'preview-only');
});
check('All transitions are immutable and clone-safe; facts never mutate cached state', () => {
  const state = freeze(initial()); const text = JSON.stringify(state); const next = act(state, 'deploy');
  assert.equal(JSON.stringify(state), text); assert.notEqual(next, state); assert.equal(next.desired, true);
  const copy = structuredClone(next); assert.deepEqual(copy, next); displayFacts(next); assert.deepEqual(next, copy);
});

const status = checks.every(check => check.pass) ? 'PASS' : 'FAIL';
const result = { suite: 'MainWindow Phase E bound Display/Identify/deployment model', checks: checks.length,
  passed: checks.filter(check => check.pass).length, status, results: checks };
const directory = fileURLToPath(new URL('../node_modules/.cache/session-phase-e/', import.meta.url));
mkdirSync(directory, { recursive: true });
const output = JSON.stringify(result, null, 2);
writeFileSync(`${directory}/display-model-${process.env.DSE_CHECK_STAGE || 'green'}.json`, `${output}\n`);
writeFileSync(`${directory}/display-model-${process.env.DSE_CHECK_STAGE || 'green'}.log`, `${output}\n`);
console.log(output);
if (status !== 'PASS') process.exitCode = 1;
