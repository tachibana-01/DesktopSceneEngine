import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { ProductionLauncherFacade, launcherEntryMode } from '../src/ProductionLauncherFacade.ts';
import { ProductionLauncherController } from '../src/ProductionLauncherController.ts';
import { launcherDisplayAction } from '../src/launcherSessionPresentation.ts';

// A7 adds characterization/lifetime proof, not a product behavior change. The
// authority audit found no frontend production fix requiring an artificial RED.
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const epoch = 'dba10904-5b14-43f0-995b-17bc99181d43';
const incarnation = 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2';
const operationId = '6812eb13-67c4-4fc1-8bae-0216f2472645';
const rows = ['A', 'B'].map((name, index) => ({ id: 'opaque exact A7 target ' + name,
  identityKind: 'TargetDevicePath', persistentOwnerCapable: true, friendlyName: 'QA Panel ' + name,
  sourceDeviceName: 'QA_SOURCE_' + name, desktopX: index * 1920, desktopY: 0,
  pixelWidth: 1920, pixelHeight: 1080, primary: index === 0, displayNumber: index + 1 }));
const catalog = (displays = rows, status = 'Success') => ({ status, displays });
const language = (effective = 'en', revision = 0) => ({ readStatus: 'Success', committed: effective, effective, revision });
const role = () => ({ boundDisplayId: rows[0].id, incarnation, controlConnected: true });
const operation = () => ({ displayId: rows[0].id, operationId, state: 'Pending' });
const sessions = (revision = 0, roles = [], operations = []) => ({ coordinatorEpoch: epoch, sessionRevision: revision,
  registeredSessionsScope: 'verified-registered-only', registeredSessions: roles, createOperations: operations });
const snapshot = (projection = sessions(), patch = {}) => ({ ...projection, displayEnumeration: catalog(),
  language: language(), host: { ready: true, launcherRoleReady: true }, ...patch });
function transport() {
  const listeners = new Set(), retiredListeners = [];
  return { listeners, retiredListeners, sent: [], adds: 0, removes: 0,
    postMessage(command) { this.sent.push(command); },
    addEventListener(type, fn) { assert.equal(type, 'message'); assert(!listeners.has(fn)); this.adds++; listeners.add(fn); },
    removeEventListener(type, fn) { assert.equal(type, 'message'); assert(listeners.has(fn)); this.removes++; listeners.delete(fn); retiredListeners.push(fn); },
    receive(data) { for (const fn of listeners) fn({ data }); },
    late(data) { for (const fn of retiredListeners) fn({ data }); },
  };
}
const response = (command, payload, patch = {}) => ({ kind: 'result', version: 1, type: command.type,
  requestId: command.requestId, generation: command.generation, ok: true, payload, ...patch });
const event = (type, payload, page = generation) => ({ kind: 'event', version: 1, type, generation: page, payload });
function page(bridge = transport(), pageGeneration = generation) {
  const facade = new ProductionLauncherFacade(bridge, pageGeneration), controller = new ProductionLauncherController(facade);
  return { facade, controller, bridge, generation: pageGeneration,
    async boot(value = snapshot()) { const pending = controller.bootstrap(); bridge.receive(response(bridge.sent.at(-1), value)); await pending; },
    dispose() { controller.dispose(); },
  };
}
const turn = async () => { await Promise.resolve(); await Promise.resolve(); };
const checks = [];
async function check(group, name, run) {
  try { await run(); checks.push({ group, name, pass: true }); }
  catch (error) { checks.push({ group, name, pass: false, error: error.stack ?? String(error) }); }
}

await check('authority', 'Entry mode cannot fall back from production to dev fixtures', () => {
  assert.equal(launcherEntryMode('?mode=production', false), 'production');
  assert.equal(launcherEntryMode('?mode=production&scenario=slow-start', true), 'production');
  for (const query of ['', '?view=launcher', '?mode=prototype', '?mode=invalid']) assert.equal(launcherEntryMode(query, false), 'invalid');
  assert.equal(launcherEntryMode('', true), 'prototype');
  assert.equal(launcherEntryMode('?mode=prototype', true), 'prototype');
  assert.throws(() => new ProductionLauncherFacade(undefined, generation), /native bridge is unavailable/);
});
await check('authority', 'Production import graph has no prototype/domain provider or persistence authority', () => {
  const src = fileURLToPath(new URL('../src/', import.meta.url)), visited = new Set();
  function inspect(path) {
    if (visited.has(path)) return; visited.add(path);
    const source = readFileSync(path, 'utf8');
    for (const forbidden of ['fixtureDisplays', 'mockDisplays', 'mockSessions', 'useLauncherPrototype', 'identityStatus',
      'localStorage', 'sessionStorage', 'activeSessions', 'CloseSession', 'ForceClose']) assert(!source.includes(forbidden), path + ': ' + forbidden);
    assert(!/setInterval\s*\(/.test(source), path + ': no production polling');
    if (!path.endsWith('ProductionLauncherFacade.ts')) assert(!/setTimeout\s*\(/.test(source), path + ': no domain timers');
    for (const match of source.matchAll(/(?:from\s*|import\s*)['"](\.\/[^'"]+)['"]/g)) {
      if (match[1].endsWith('.css')) continue;
      const base = resolve(dirname(path), match[1]);
      const target = [base, base + '.ts', base + '.tsx'].find(existsSync);
      assert(target, 'Resolve actual production module ' + match[1]); inspect(target);
    }
  }
  inspect(resolve(src, 'ProductionLauncherShell.tsx'));
  assert(visited.has(resolve(src, 'ProductionLauncherFacade.ts')));
  assert(visited.has(resolve(src, 'ProductionLauncherController.ts')));
  assert(![...visited].some(path => /Prototype|LauncherShell\.tsx$/.test(path) && !path.endsWith('ProductionLauncherShell.tsx')));
  const entry = readFileSync(resolve(src, 'main.tsx'), 'utf8');
  assert.match(entry, /import\.meta\.env\.DEV\s*\?\s*lazy\(\(\)\s*=>\s*import\('\.\/PrototypeApp'\)\)\s*:\s*null/);
  const facade = readFileSync(resolve(src, 'ProductionLauncherFacade.ts'), 'utf8');
  assert.equal([...facade.matchAll(/setTimeout\s*\(/g)].length, 1);
  assert.match(facade, /setTimeout\(\(\) => this\.fail\(requestId/); // transport deadline, no domain transition
});
for (const state of ['idle', 'ACTIVE', 'STARTING']) {
  await check('reconstruction', 'Fresh bootstrap restores ' + state + ', exact IDs and language without replay', async () => {
    const p = page(); try {
      const facts = sessions(9, state === 'ACTIVE' ? [role()] : [], state === 'STARTING' ? [operation()] : []);
      await p.boot(snapshot(facts, { language: language('zh_CN', 7), displayEnumeration: catalog([rows[1], rows[0]]) }));
      const actual = p.controller.getState();
      assert.equal(actual.bootstrap.coordinatorEpoch, epoch); assert.equal(actual.bootstrap.host.ready, true);
      assert.equal(actual.selectedDisplayId, rows[1].id); assert.deepEqual(actual.bootstrap.language, language('zh_CN', 7));
      assert.equal(launcherDisplayAction(actual.bootstrap, actual.catalog, rows[0].id).status, state === 'idle' ? 'noRegisteredSession' : state);
      if (state === 'ACTIVE') assert.equal(actual.bootstrap.registeredSessions[0].incarnation, incarnation);
      if (state === 'STARTING') assert.equal(actual.bootstrap.createOperations[0].operationId, operationId);
      assert.deepEqual(p.bridge.sent.map(command => command.type), ['GetLauncherBootstrapSnapshot']);
    } finally { p.dispose(); }
  });
}

const commands = [
  { type: 'GetLauncherBootstrapSnapshot', run: p => p.controller.bootstrap(), payload: () => snapshot() },
  { type: 'RefreshDisplays', run: p => p.controller.refresh(), payload: () => catalog([]) },
  { type: 'IdentifyDisplay', run: p => p.controller.identifySelected(), payload: () => ({ displayId: rows[0].id, status: 'Shown' }) },
  { type: 'CommitLanguage', run: p => { p.controller.openSettings(); p.controller.draftLanguage('zh_CN'); return p.controller.commitLanguage(false); },
    payload: () => ({ status: 'COMMITTED', language: language('zh_CN', 1), warnings: [] }) },
  { type: 'CreateSession', run: p => p.controller.createSession(rows[0].id),
    payload: () => ({ coordinatorEpoch: epoch, displayId: rows[0].id, operationId, status: 'Accepted' }) },
  { type: 'OpenSession', run: p => p.controller.openSession(rows[0].id, incarnation),
    payload: () => ({ coordinatorEpoch: epoch, incarnation, status: 'ActivationRequested' }) },
];
for (const command of commands) for (const ok of [true, false]) {
  await check('late-results', 'Dispose fences late ' + command.type + (ok ? ' success' : ' failure') + ' and fresh page never replays it', async () => {
    const bridge = transport(), old = page(bridge), freshGeneration = crypto.randomUUID();
    let fresh; try {
      if (command.type !== 'GetLauncherBootstrapSnapshot') await old.boot(snapshot(sessions(0, command.type === 'OpenSession' ? [role()] : [])));
      const pending = command.run(old), issued = bridge.sent.at(-1); assert.equal(issued.type, command.type);
      let writes = 0; old.controller.subscribe(() => writes++);
      const retiredState = old.controller.getState(), frozen = structuredClone(retiredState);
      old.dispose(); old.dispose(); await pending;
      assert.equal(bridge.listeners.size, 0); assert.equal(bridge.removes, 1);
      fresh = page(bridge, freshGeneration);
      const facts = sessions(12, [role()], [operation()]);
      const committed = snapshot(facts, { language: language('zh_CN', 8), displayEnumeration: catalog([rows[1], rows[0]]) });
      await fresh.boot(committed); const current = fresh.controller.getState(), beforeCommands = bridge.sent.length;
      const value = response(issued, command.payload(), ok ? {} : { ok: false, error: 'old page native failure' });
      bridge.receive(value); bridge.late(value); // even a callback already copied before unsubscribe is safe
      bridge.receive(event('LauncherSessionsChanged', sessions(999, []), generation));
      bridge.late(event('LauncherLanguageChanged', language('en', 999), generation));
      await turn();
      assert.equal(writes, 0); assert.equal(old.controller.getState(), retiredState); assert.deepEqual(retiredState, frozen);
      assert.equal(fresh.controller.getState(), current); assert.equal(current.bootstrap.language.effective, 'zh_CN');
      assert.equal(current.bootstrap.registeredSessions[0].incarnation, incarnation);
      assert.equal(current.bootstrap.createOperations[0].operationId, operationId);
      assert.equal(bridge.sent.length, beforeCommands);
      assert.deepEqual(bridge.sent.slice(-1).map(command => command.type), ['GetLauncherBootstrapSnapshot']);
    } finally { old.dispose(); fresh?.dispose(); }
  });
}
await check('ordering', 'Events before bootstrap preserve newest Session and committed language truth', async () => {
  const p = page(); try {
    const pending = p.controller.bootstrap(), issued = p.bridge.sent.at(-1);
    p.bridge.receive(event('LauncherSessionsChanged', sessions(4, [role()], [operation()])));
    p.bridge.receive(event('LauncherLanguageChanged', language('zh_CN', 3)));
    p.bridge.receive(response(issued, snapshot())); await pending;
    const state = p.controller.getState(); assert.equal(state.bootstrap.sessionRevision, 4);
    assert.equal(state.bootstrap.language.revision, 3); assert.equal(state.bootstrap.language.effective, 'zh_CN');
    assert.equal(state.bootstrap.registeredSessions[0].incarnation, incarnation);
    assert.equal(p.bridge.sent.length, 1);
  } finally { p.dispose(); }
});
await check('subscriptions', '50 facade/controller retire-recreate cycles return listeners and request deadlines to baseline', async () => {
  const bridge = transport(), originalSet = globalThis.setTimeout, originalClear = globalThis.clearTimeout;
  const deadlines = new Map(), completed = [];
  globalThis.setTimeout = (callback, delay, ...args) => {
    const handle = originalSet(callback, delay, ...args); deadlines.set(handle, callback); return handle;
  };
  globalThis.clearTimeout = handle => { deadlines.delete(handle); return originalClear(handle); };
  try {
    for (let index = 0; index < 50; index++) {
      const p = page(bridge, crypto.randomUUID());
      try {
        assert.equal(bridge.listeners.size, 1); await p.boot(); assert.equal(deadlines.size, 0);
        let writes = 0; p.controller.subscribe(() => writes++);
        bridge.receive(event('LauncherSessionsChanged', sessions(1, [role()]), p.generation));
        assert.equal(writes, 1);
        bridge.receive(event('LauncherSessionsChanged', sessions(1, [role()]), p.generation));
        assert.equal(writes, 1); // same native revision is not a second Session update
        bridge.receive(event('LauncherLanguageChanged', language('zh_CN', 1), p.generation));
        assert.equal(writes, 2); // one invalidation delivers one logical update, regardless of cycle
        const before = p.controller.getState();
        const waiting = p.controller.openSession(rows[0].id, incarnation), issued = bridge.sent.at(-1);
        assert.equal(deadlines.size, 1); const lateDeadline = [...deadlines.values()][0];
        p.dispose(); p.dispose(); await waiting;
        assert.equal(bridge.listeners.size, 0); assert.equal(deadlines.size, 0);
        const retiredState = p.controller.getState(); assert.notEqual(retiredState, before);
        const oldWrites = writes;
        bridge.late(response(issued, { coordinatorEpoch: epoch, incarnation, status: 'ActivationRequested' }));
        bridge.late(event('LauncherSessionsChanged', sessions(2, []), p.generation));
        bridge.late(event('LauncherLanguageChanged', language('en', 2), p.generation));
        lateDeadline(); await turn();
        assert.equal(p.controller.getState(), retiredState); assert.equal(writes, oldWrites);
        assert.equal(bridge.adds, index + 1); assert.equal(bridge.removes, index + 1);
        completed.push(index + 1);
      } finally { p.dispose(); }
    }
  } finally { globalThis.setTimeout = originalSet; globalThis.clearTimeout = originalClear; }
  assert.equal(completed.length, 50);
});
await check('facade', 'Repeated dispose cancels pending requests and unsubscribes exactly once', async () => {
  const bridge = transport(), facade = new ProductionLauncherFacade(bridge, generation);
  let languages = 0, roles = 0;
  const offLanguage = facade.subscribeLanguage(() => languages++), offSessions = facade.subscribeSessions(() => roles++);
  const pending = facade.getBootstrapSnapshot(), rejected = assert.rejects(pending, /retired/);
  facade.dispose(); facade.dispose(); offLanguage(); offLanguage(); offSessions(); offSessions(); await rejected;
  bridge.late(event('LauncherLanguageChanged', language('zh_CN', 1)));
  bridge.late(event('LauncherSessionsChanged', sessions(1, [role()])));
  assert.equal(languages, 0); assert.equal(roles, 0); assert.equal(bridge.removes, 1); assert.equal(bridge.listeners.size, 0);
  await assert.rejects(facade.getBootstrapSnapshot(), /retired/); assert.equal(bridge.sent.length, 1);
});
const output = { pass: checks.every(check => check.pass), sourceBehaviorChanges: 'NONE',
  red: 'Not applicable: authority audit and characterization/lifetime coverage only; no runtime change or artificial RED.',
  subscriptionRecreateCycles: 50, checks };
const cache = new URL('../node_modules/.cache/launcher-hardening-a7/', import.meta.url); mkdirSync(cache, { recursive: true });
writeFileSync(new URL('focused-checks.json', cache), JSON.stringify(output, null, 2));
console.log(JSON.stringify({ pass: output.pass, total: checks.length, subscriptionRecreateCycles: 50,
  failed: checks.filter(check => !check.pass) }, null, 2));
assert(output.pass, 'A7 frontend lifetime/authority characterization must pass');
