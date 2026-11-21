import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { launcherRenderModule } from './launcher-render-module.mjs';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { ProductionLauncherFacade, readLauncherCatalog, readLauncherSnapshot, launcherEntryMode } from '../src/ProductionLauncherFacade.ts';
import { ProductionLauncherController } from '../src/ProductionLauncherController.ts';

// RED before implementation: facade.refreshDisplays was undefined. These checks
// now drive the actual production facade/controller and render production JSX.
const checks = [];
async function check(group, name, run) {
  try { await run(); checks.push({ group, name, pass: true }); }
  catch (error) { checks.push({ group, name, pass: false, error: String(error) }); }
}
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const nextGeneration = 'd7951708-aa40-497e-9160-37a96e1063e3';
const targetId = 'dse-display-id:v1:target-device-path:26:\\?\\DISPLAY#メーカー#opaque';
const weakId = 'dse-display-id:v1:source-device-name:12:\\.\\DISPLAY2';
const thirdId = 'opaque identity C, never reconstructed';
const clone = value => structuredClone(value);
function rows() {
  return [
    { id: targetId, identityKind: 'TargetDevicePath', persistentOwnerCapable: true, friendlyName: 'Production panel A', sourceDeviceName: '\\\\.\\DISPLAY1', desktopX: -1920, desktopY: 0, pixelWidth: 1920, pixelHeight: 1080, primary: false, displayNumber: 7, refreshRate: { numerator: 60000, denominator: 1001 } },
    { id: weakId, identityKind: 'SourceDeviceName', persistentOwnerCapable: false, friendlyName: 'Production fallback B', sourceDeviceName: '\\\\.\\DISPLAY2', desktopX: 0, desktopY: 0, pixelWidth: 2560, pixelHeight: 1440, primary: true, displayNumber: 2 },
    { id: thirdId, identityKind: 'TargetDevicePath', persistentOwnerCapable: true, friendlyName: 'Production panel C', sourceDeviceName: '\\\\.\\DISPLAY3', desktopX: 2560, desktopY: -20, pixelWidth: 1080, pixelHeight: 1920, primary: false, displayNumber: 3 },
  ];
}
function catalog(displays = rows(), status = 'Success') { return { status, displays }; }
function snapshot(displayEnumeration = catalog()) {
  return { coordinatorEpoch: 'dba10904-5b14-43f0-995b-17bc99181d43', sessionRevision: 0, displayEnumeration,
    registeredSessions: [{ boundDisplayId: targetId, incarnation: 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2', controlConnected: true }],
    registeredSessionsScope: 'verified-registered-only', createOperations: [],
    language: { readStatus: 'Success', committed: 'en', effective: 'en', revision: 0 },
    host: { ready: true, launcherRoleReady: true } };
}
function transport() {
  const listeners = new Set();
  return { sent: [], listeners,
    postMessage(message) { this.sent.push(message); },
    addEventListener(type, listener) { assert.equal(type, 'message'); listeners.add(listener); },
    removeEventListener(type, listener) { assert.equal(type, 'message'); listeners.delete(listener); },
    receive(data) { for (const listener of listeners) listener({ data }); } };
}
function result(command, payload, overrides = {}) {
  return { kind: 'result', version: 1, requestId: command.requestId, type: command.type,
    generation: command.generation, ok: true, payload, ...overrides };
}
function setup(pageGeneration = generation) {
  const bridge = transport();
  const facade = new ProductionLauncherFacade(bridge, pageGeneration);
  const controller = new ProductionLauncherController(facade);
  return { bridge, facade, controller };
}
async function boot(fixture, value = snapshot()) {
  const pending = fixture.controller.bootstrap();
  fixture.bridge.receive(result(fixture.bridge.sent.at(-1), value));
  await pending;
}
async function refresh(fixture, value) {
  const pending = fixture.controller.refresh();
  fixture.bridge.receive(result(fixture.bridge.sent.at(-1), value));
  await pending;
}
async function withPage(run) {
  const fixture = setup();
  try { await boot(fixture); await run(fixture); } finally { fixture.controller.dispose(); }
}

await check('command', 'Typed Refresh success + empty preserves the exact wire envelope', async () => {
  const { bridge, facade, controller } = setup();
  try {
    assert.equal(typeof facade.refreshDisplays, 'function');
    const pending = facade.refreshDisplays(); const command = bridge.sent[0];
    assert.deepEqual(Object.keys(command).sort(), ['kind', 'version', 'requestId', 'type', 'generation', 'payload'].sort());
    assert.equal(command.kind, 'command'); assert.equal(command.version, 1); assert.equal(command.type, 'RefreshDisplays');
    assert.equal(command.generation, generation); assert.deepEqual(command.payload, {});
    assert.match(command.requestId, /^[A-Za-z0-9_-]{1,128}$/);
    bridge.receive(result(command, catalog([]))); assert.deepEqual(await pending, catalog([]));
  } finally { controller.dispose(); }
});
for (const count of [1, 3]) await check('command', 'Refresh success with ' + count + ' descriptors', async () => {
  const { bridge, facade, controller } = setup();
  try {
    const value = catalog(rows().slice(0, count)); const pending = facade.refreshDisplays();
    bridge.receive(result(bridge.sent[0], value)); assert.deepEqual(await pending, value);
  } finally { controller.dispose(); }
});
for (const status of ['ApiFailure', 'IncompletePathMetadata', 'DuplicateIdentity']) {
  await check('command', status + ' is an enumeration outcome despite transport ok=true', async () => {
    const { bridge, facade, controller } = setup();
    try {
      const pending = facade.refreshDisplays(); bridge.receive(result(bridge.sent[0], catalog([], status)));
      assert.deepEqual(await pending, { status, displays: [] });
    } finally { controller.dispose(); }
  });
}
await check('command', 'Bootstrap and Refresh share catalog validation including opaque exact IDs', () => {
  const value = catalog();
  assert.deepEqual(readLauncherCatalog(value), readLauncherSnapshot(snapshot(value)).displayEnumeration);
  assert.equal(readLauncherCatalog(value).displays[0].id, targetId);
  for (const mutate of [
    value => delete value.status, value => value.status = 'Empty', value => value.status = ['Success'],
    value => value.status = { toString() { return 'Success'; } },
    value => value.displays[0].id = '', value => value.displays[1].id = targetId,
    value => value.displays[0].pixelWidth = NaN, value => value.displays[0].desktopX = Infinity,
    value => value.displays[0].pixelHeight = -1, value => value.displays[0].displayNumber = 1.2,
    value => value.displays[0].refreshRate.denominator = 0, value => value.displays[0].refreshRate.extra = 1,
    value => value.displays[1].persistentOwnerCapable = true, value => value.displays[0].identityKind = 'FriendlyName',
    value => value.displays[1].identityKind = ['SourceDeviceName'],
    value => value.displays[1].identityKind = { toString() { return 'SourceDeviceName'; } },
    value => value.displays[0].extra = true, value => value.status = 'ApiFailure',
  ]) {
    const invalid = clone(value); mutate(invalid);
    assert.throws(() => readLauncherCatalog(invalid));
    assert.throws(() => readLauncherSnapshot(snapshot(invalid)));
  }
});
await check('command', 'Parallel facade Refresh callers coalesce; old duplicate result cannot overwrite new request', async () => {
  const { bridge, facade, controller } = setup();
  try {
    const first = facade.refreshDisplays(); const same = facade.refreshDisplays();
    assert.equal(first, same); assert.equal(bridge.sent.length, 1);
    bridge.receive(result(bridge.sent[0], catalog(rows().slice(0, 1)))); await first;
    let settled = false; const second = facade.refreshDisplays().then(value => { settled = true; return value; });
    bridge.receive(result(bridge.sent[0], catalog([], 'ApiFailure')));
    await Promise.resolve(); assert.equal(settled, false);
    bridge.receive(result(bridge.sent[1], catalog())); assert.deepEqual(await second, catalog());
  } finally { controller.dispose(); }
});
await check('command', 'Stale generation, wrong correlation and unrelated events cannot complete Refresh', async () => {
  const { bridge, facade, controller } = setup();
  try {
    let settled = false; const pending = facade.refreshDisplays().then(value => { settled = true; return value; });
    const command = bridge.sent[0];
    bridge.receive(result(command, catalog(), { generation: nextGeneration }));
    bridge.receive(result(command, catalog(), { requestId: 'earlier-request' }));
    bridge.receive(result(command, catalog(), { kind: 'event' }));
    await Promise.resolve(); assert.equal(settled, false);
    bridge.receive(result(command, catalog())); assert.deepEqual(await pending, catalog());
  } finally { controller.dispose(); }
});
await check('command', 'Malformed correlated Refresh/native errors reject and permit a fresh retry', async () => {
  for (const response of [
    command => result(command, {}), command => result(command, catalog(), { type: 'GetLauncherBootstrapSnapshot' }),
    command => result(command, catalog(), { version: 2 }), command => ({ ...result(command, catalog()), extra: true }),
    command => ({ kind: 'result', version: 1, requestId: command.requestId, type: command.type, generation, ok: false, error: 'Native refresh unavailable' }),
  ]) {
    const { bridge, facade, controller } = setup();
    try {
      const pending = facade.refreshDisplays(); const rejected = assert.rejects(pending);
      bridge.receive(response(bridge.sent[0])); await rejected;
      const retry = facade.refreshDisplays(); assert.equal(bridge.sent.length, 2);
      bridge.receive(result(bridge.sent[1], catalog([]))); await retry;
    } finally { controller.dispose(); }
  }
});
await check('command', 'Reload disposes old Refresh callbacks; new page bootstraps current catalog without replay', async () => {
  const fixture = setup(); await boot(fixture);
  const pending = fixture.controller.refresh(); const oldCommand = fixture.bridge.sent.at(-1);
  const before = fixture.controller.getState(); let retiredUpdates = 0;
  fixture.controller.subscribe(() => retiredUpdates++); fixture.controller.dispose(); await pending;
  const replacement = new ProductionLauncherController(new ProductionLauncherFacade(fixture.bridge, nextGeneration));
  try {
    const bootstrap = replacement.bootstrap(); const command = fixture.bridge.sent.at(-1);
    assert.equal(command.type, 'GetLauncherBootstrapSnapshot'); assert.equal(command.generation, nextGeneration);
    fixture.bridge.receive(result(oldCommand, catalog([], 'DuplicateIdentity')));
    assert.equal(replacement.getState().bootstrap, null); assert.equal(retiredUpdates, 0);
    assert.equal(fixture.controller.getState(), before);
    fixture.bridge.receive(result(command, snapshot(catalog(rows().slice(1))))); await bootstrap;
    assert.equal(replacement.getState().selectedDisplayId, weakId);
    assert.deepEqual(fixture.bridge.sent.map(message => message.type), ['GetLauncherBootstrapSnapshot', 'RefreshDisplays', 'GetLauncherBootstrapSnapshot']);
  } finally { replacement.dispose(); }
  assert.equal(fixture.bridge.listeners.size, 0);
});
await check('command', 'Production missing bridge remains failure; entry mode cannot silently select prototype', () => {
  assert.throws(() => new ProductionLauncherFacade(undefined, generation), /bridge/);
  assert.throws(() => new ProductionLauncherFacade(transport(), null), /generation/);
  assert.equal(launcherEntryMode('?mode=production', false), 'production');
  assert.equal(launcherEntryMode('?mode=prototype', false), 'invalid');
  assert.equal(launcherEntryMode('', false), 'invalid');
});
await check('command', 'Controller duplicate clicks are synchronously suppressed and use only Refresh', async () => withPage(async fixture => {
  const { controller, bridge } = fixture; const pending = controller.refresh();
  assert.equal(controller.getState().refreshPending, true);
  await controller.refresh(); await controller.refresh();
  assert.equal(bridge.sent.length, 2);
  bridge.receive(result(bridge.sent[1], catalog())); await pending;
  assert.equal(controller.getState().refreshPending, false);
  assert.deepEqual(bridge.sent.map(message => message.type), ['GetLauncherBootstrapSnapshot', 'RefreshDisplays']);
}));
await check('command', 'Bootstrap failure remains explicit; retry re-reads all facts instead of inventing registry/language', async () => {
  const fixture = setup();
  try {
    const initial = fixture.controller.bootstrap(); fixture.bridge.receive(result(fixture.bridge.sent[0], {})); await initial;
    assert.equal(fixture.controller.getState().bootstrap, null); assert(fixture.controller.getState().bootstrapError);
    await fixture.controller.refresh(); assert.equal(fixture.bridge.sent.length, 1);
    await boot(fixture); assert.equal(fixture.bridge.sent[1].type, 'GetLauncherBootstrapSnapshot');
    assert.equal(fixture.controller.getState().bootstrapError, null);
    assert.deepEqual(fixture.controller.getState().bootstrap.registeredSessions, snapshot().registeredSessions);
  } finally { fixture.controller.dispose(); }
});
await check('command', 'Transport validation failure clears rows/selection without fake ApiFailure and allows retry', async () => withPage(async fixture => {
  const oldFacts = fixture.controller.getState().bootstrap;
  await refresh(fixture, { status: 'Success', displays: [{ id: 'malformed' }] });
  const state = fixture.controller.getState();
  assert.equal(state.catalog, null); assert.equal(state.selectedDisplayId, null); assert(state.refreshError);
  assert.equal(state.bootstrap, oldFacts);
  await refresh(fixture, catalog()); assert.equal(fixture.controller.getState().refreshError, null);
  assert.equal(fixture.controller.getState().selectedDisplayId, targetId);
}));

await check('selection', 'Initial nonempty catalog selects first production row, regardless of primary', async () => withPage(async ({ controller }) => {
  assert.equal(controller.getState().selectedDisplayId, targetId);
  assert.equal(controller.getState().catalog.displays[0].primary, false);
}));
await check('selection', 'Exact selected ID follows reorder instead of previous index', async () => withPage(async fixture => {
  fixture.controller.select(weakId);
  await refresh(fixture, catalog([rows()[1], rows()[2], rows()[0]]));
  assert.equal(fixture.controller.getState().selectedDisplayId, weakId);
  assert.deepEqual(fixture.controller.getState().catalog.displays.map(row => row.id), [weakId, thirdId, targetId]);
}));
await check('selection', 'Selected display removed selects first remaining production row', async () => withPage(async fixture => {
  fixture.controller.select(weakId); await refresh(fixture, catalog([rows()[2], rows()[0]]));
  assert.equal(fixture.controller.getState().selectedDisplayId, thirdId);
}));
await check('selection', 'Success empty clears selection without error', async () => withPage(async fixture => {
  await refresh(fixture, catalog([])); const state = fixture.controller.getState();
  assert.deepEqual(state.catalog, catalog([])); assert.equal(state.selectedDisplayId, null); assert.equal(state.refreshError, null);
}));
for (const status of ['ApiFailure', 'IncompletePathMetadata', 'DuplicateIdentity']) await check('selection', status + ' clears stale rows and selection', async () => withPage(async fixture => {
  await refresh(fixture, catalog([], status));
  assert.equal(fixture.controller.getState().selectedDisplayId, null);
  assert.deepEqual(fixture.controller.getState().catalog, catalog([], status));
  assert.equal(fixture.controller.getState().refreshError, null);
}));
await check('selection', 'Metadata updates retain exact identity and resolve details from current catalog', async () => withPage(async fixture => {
  const changed = rows()[0]; Object.assign(changed, { friendlyName: 'Renamed panel', desktopX: 30, desktopY: 40,
    pixelWidth: 3840, pixelHeight: 2160, primary: true, displayNumber: 12, refreshRate: { numerator: 144, denominator: 1 } });
  await refresh(fixture, catalog([changed, rows()[1]])); const state = fixture.controller.getState();
  assert.equal(state.selectedDisplayId, targetId);
  assert.deepEqual(state.catalog.displays.find(row => row.id === state.selectedDisplayId), changed);
}));
await check('selection', 'Weak identity stays visible/selectable and owner capability never promoted', async () => withPage(async fixture => {
  await refresh(fixture, catalog([rows()[1], rows()[0]])); fixture.controller.select(weakId);
  const state = fixture.controller.getState(); assert.equal(state.selectedDisplayId, weakId);
  const selected = state.catalog.displays.find(row => row.id === state.selectedDisplayId);
  assert.equal(selected.identityKind, 'SourceDeviceName'); assert.equal(selected.persistentOwnerCapable, false);
  assert.equal(Object.hasOwn(selected, 'identityStatus'), false); assert.equal(Object.hasOwn(selected, 'identifySucceeded'), false);
}));
await check('selection', 'Display removal keeps registered Session, epoch and language facts unchanged', async () => withPage(async fixture => {
  const original = fixture.controller.getState().bootstrap;
  await refresh(fixture, catalog([rows()[1]]));
  assert.equal(fixture.controller.getState().bootstrap, original);
  assert.equal(original.registeredSessions[0].boundDisplayId, targetId);
}));
await check('selection', 'Unknown selection cannot become a production command target', async () => withPage(async fixture => {
  fixture.controller.select('display-a'); fixture.controller.select('7');
  assert.equal(fixture.controller.getState().selectedDisplayId, targetId);
}));
await check('selection', 'Selection changed while pending is preserved if exact ID survives', async () => withPage(async fixture => {
  const pending = fixture.controller.refresh(); fixture.controller.select(thirdId);
  fixture.bridge.receive(result(fixture.bridge.sent.at(-1), catalog(rows().reverse()))); await pending;
  assert.equal(fixture.controller.getState().selectedDisplayId, thirdId);
}));

const cache = new URL('../node_modules/.cache/launcher-refresh-a2/', import.meta.url);
mkdirSync(cache, { recursive: true });
const componentUrl = new URL('../src/ProductionLauncherShell.tsx', import.meta.url);
const renderModule = launcherRenderModule(componentUrl, cache);
const { ProductionLauncherView, productionDeckPosition } = await import(renderModule.href + '?run=' + Date.now());
function render(value = catalog(), selectedId = value?.displays[0]?.id ?? null, extras = {}) {
  return renderToStaticMarkup(React.createElement(ProductionLauncherView, { snapshot: snapshot(), catalog: value,
    selectedId, onSelect() {}, onRefresh() {}, ...extras }));
}
await check('presentation', 'Refresh uses established icon placement; real metadata and selection are visible', () => {
  const html = render(); assert(html.includes('aria-label="Refresh displays"')); assert(html.includes('launcher-global-actions'));
  assert(html.includes('data-selected-display-id="' + targetId + '"')); assert(html.includes('Production panel A'));
  assert(html.includes('1920')); assert(html.includes('1080')); assert(!html.includes('DEV STATE'));
});
await check('presentation', 'Pending Refresh is disabled with subtle feedback', () => {
  const html = render(catalog(), targetId, { refreshing: true });
  assert.match(html, /data-refresh-displays="true"[^>]*aria-busy="true"[^>]*disabled=""/);
  assert(html.includes('Refreshing displays…')); assert(html.includes('data-refresh-state="pending"'));
});
await check('presentation', 'Deck position follows exact selected index through many/one/reordered transitions', () => {
  assert.equal(productionDeckPosition(7, 0, 1), 0);
  assert.equal(productionDeckPosition(-5, 0, 1), 0);
  assert.equal(productionDeckPosition(0, 1, 3), 1);
  assert.equal(productionDeckPosition(7, 0, 3), 6);
  assert.equal(productionDeckPosition(-5, 2, 3), -4);
});
await check('presentation', 'Success empty explains that no displays were found and Refresh remains available', () => {
  const html = render(catalog([])); assert(html.includes('No displays found'));
  assert(!html.includes('Couldn’t find displays')); assert(!html.includes('data-display-id='));
  assert(html.includes('aria-label="Refresh displays"'));
});
await check('presentation', 'All enumeration failures remain distinct and retryable without stale rows', () => {
  for (const [status, explanation] of [
    ['ApiFailure', 'Windows couldn’t find any displays.'],
    ['IncompletePathMetadata', 'Windows hasn’t finished identifying the displays.'],
    ['DuplicateIdentity', 'Some displays couldn’t be told apart.'],
  ]) {
    const html = render(catalog([], status)); assert(html.includes('Couldn’t find displays'));
    assert(html.includes(explanation)); assert(html.includes('data-enumeration-status="' + status + '"'));
    assert(!html.includes('No displays found')); assert(!html.includes('data-display-id='));
    assert(html.includes('data-refresh-retry'));
  }
});
await check('presentation', 'Transport failure is distinct from Platform failure and has retry', () => {
  const html = render(null, null, { refreshError: 'Invalid message' });
  assert(html.includes('Couldn’t refresh displays')); assert(html.includes('data-enumeration-status="unavailable"'));
  assert(html.includes('data-refresh-retry')); assert(!html.includes('No displays found'));
  assert(!html.includes('ApiFailure')); assert(!html.includes('data-display-id='));
});
await check('presentation', 'Weak selection has no synthetic Identify promotion/Create action', () => {
  const html = render(catalog(), weakId); assert(html.includes('Production fallback B'));
  assert(html.includes('Workspaces can’t be opened on this display right now.'));
  assert(!html.includes('Source identity')); assert(!html.includes('production-source'));
  assert(html.includes('data-identify-display="true"'));
  assert.match(html, /data-session-action="Create"[^>]*disabled=""/);
  for (const forbidden of ['OPEN SESSION', 'AVAILABLE', 'STARTING', 'CLOSING', 'DISCONNECTED']) assert(!html.includes(forbidden), forbidden);
});

const output = { pass: checks.every(item => item.pass), checks };
writeFileSync(new URL('focused-checks.json', cache), JSON.stringify(output, null, 2));
const groups = Object.fromEntries(['command', 'selection', 'presentation'].map(group => {
  const matching = checks.filter(check => check.group === group);
  return [group, { passed: matching.filter(check => check.pass).length, total: matching.length }];
}));
console.log(JSON.stringify({ pass: output.pass, groups, failed: checks.filter(item => !item.pass) }, null, 2));
assert(output.pass, 'Production Launcher A2 behavior checks must pass');
