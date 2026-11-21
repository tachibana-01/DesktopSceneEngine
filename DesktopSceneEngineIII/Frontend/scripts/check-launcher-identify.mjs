import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { launcherRenderModule } from './launcher-render-module.mjs';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { ProductionLauncherFacade, launcherEntryMode } from '../src/ProductionLauncherFacade.ts';
import { ProductionLauncherController } from '../src/ProductionLauncherController.ts';

// RED before implementation: ProductionLauncherFacade.prototype.identifyDisplay
// was undefined. These checks now call the real production facade/controller.
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const nextGeneration = 'd7951708-aa40-497e-9160-37a96e1063e3';
const canonical = (kind, value) => `dse-display-id:v1:${kind}:${Buffer.byteLength(value)}:${value}`;
const targetId = canonical('target-device-path', '\\\\?\\DISPLAY#メーカー#opaque');
const weakId = canonical('source-device-name', '\\\\.\\DISPLAY2');
const catalog = { status: 'Success', displays: [
  { id: targetId, identityKind: 'TargetDevicePath', persistentOwnerCapable: true, friendlyName: 'Panel A', sourceDeviceName: '\\\\.\\DISPLAY1', desktopX: -1920, desktopY: 0, pixelWidth: 1920, pixelHeight: 1080, primary: false, displayNumber: 7 },
  { id: weakId, identityKind: 'SourceDeviceName', persistentOwnerCapable: false, friendlyName: 'Panel B', sourceDeviceName: '\\\\.\\DISPLAY2', desktopX: 0, desktopY: 0, pixelWidth: 2560, pixelHeight: 1440, primary: true, displayNumber: 2 },
] };
const snapshot = { coordinatorEpoch: 'dba10904-5b14-43f0-995b-17bc99181d43', sessionRevision: 0, displayEnumeration: catalog,
  registeredSessions: [{ boundDisplayId: targetId, incarnation: 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2', controlConnected: true }],
  registeredSessionsScope: 'verified-registered-only', createOperations: [], language: { readStatus: 'Success', committed: 'en', effective: 'en', revision: 0 },
  host: { ready: true, launcherRoleReady: true } };
const checks = [];
async function check(group, name, run) {
  try { await run(); checks.push({ group, name, pass: true }); }
  catch (error) { checks.push({ group, name, pass: false, error: String(error) }); }
}
function transport() {
  const listeners = new Set();
  return { sent: [], listeners, postMessage(command) { this.sent.push(command); },
    addEventListener(_type, listener) { listeners.add(listener); }, removeEventListener(_type, listener) { listeners.delete(listener); },
    receive(data) { for (const listener of listeners) listener({ data }); } };
}
const response = (command, payload, overrides = {}) => ({ kind: 'result', version: 1, requestId: command.requestId,
  type: command.type, generation: command.generation, ok: true, payload, ...overrides });
async function page(run, value = snapshot) {
  const bridge = transport(); const facade = new ProductionLauncherFacade(bridge, generation);
  const controller = new ProductionLauncherController(facade);
  try {
    const pending = controller.bootstrap(); bridge.receive(response(bridge.sent[0], structuredClone(value))); await pending;
    await run({ bridge, facade, controller });
  } finally { controller.dispose(); }
}
for (const status of ['Shown', 'Unresolved', 'InvalidRequest', 'ApiFailure']) await check('bridge', status + ' is preserved separately from transport success', async () => page(async ({ facade, bridge }) => {
  const pending = facade.identifyDisplay(targetId); const command = bridge.sent.at(-1);
  assert.deepEqual(Object.keys(command).sort(), ['kind', 'version', 'requestId', 'type', 'generation', 'payload'].sort());
  assert.equal(command.kind, 'command'); assert.equal(command.version, 1); assert.equal(command.type, 'IdentifyDisplay');
  assert.equal(command.generation, generation); assert.match(command.requestId, /^[A-Za-z0-9_-]{1,128}$/);
  assert.deepEqual(command.payload, { displayId: targetId });
  bridge.receive(response(command, { displayId: targetId, status }));
  assert.deepEqual(await pending, { displayId: targetId, status });
}));
await check('bridge', 'Correlated result must echo exact opaque target and preserve exact union/schema', async () => {
  for (const payload of [{ displayId: weakId, status: 'Shown' }, { displayId: targetId, status: true },
    { displayId: targetId, status: 'Success' }, { displayId: targetId, status: ['Shown'] },
    { displayId: targetId, status: 'Shown', persistentOwnerCapable: true }, { status: 'Shown' }, null]) {
    await page(async ({ facade, bridge }) => {
      const pending = facade.identifyDisplay(targetId); const rejected = assert.rejects(pending, /Identify result/);
      bridge.receive(response(bridge.sent.at(-1), payload)); await rejected;
    });
  }
});
await check('bridge', 'Wrong requestId or page generation cannot complete Identify', async () => page(async ({ facade, bridge }) => {
  let settled = false; const pending = facade.identifyDisplay(targetId).then(value => { settled = true; return value; });
  const command = bridge.sent.at(-1), payload = { displayId: targetId, status: 'Shown' };
  bridge.receive(response(command, payload, { requestId: 'unrelated' }));
  bridge.receive(response(command, payload, { generation: nextGeneration }));
  await Promise.resolve(); assert.equal(settled, false);
  bridge.receive(response(command, payload)); await pending;
}));
await check('bridge', 'Transport rejection is distinct from capability InvalidRequest', async () => page(async ({ facade, bridge }) => {
  const pending = facade.identifyDisplay(targetId); const rejected = assert.rejects(pending, /native rejected/);
  const command = bridge.sent.at(-1); const failure = response(command, null, { ok: false, error: 'native rejected' });
  delete failure.payload; bridge.receive(failure); await rejected;
  const retry = facade.identifyDisplay(targetId);
  bridge.receive(response(bridge.sent.at(-1), { displayId: targetId, status: 'InvalidRequest' }));
  assert.equal((await retry).status, 'InvalidRequest');
}));
await check('bridge', 'Malformed type/version/envelope cannot complete as Identify success', async () => {
  for (const overrides of [{ type: 'RefreshDisplays' }, { version: 2 }, { extra: true }]) await page(async ({ facade, bridge }) => {
    const pending = facade.identifyDisplay(targetId); const rejected = assert.rejects(pending, /response is invalid/);
    bridge.receive(response(bridge.sent.at(-1), { displayId: targetId, status: 'Shown' }, overrides)); await rejected;
  });
});
await check('bridge', 'Missing bridge fails explicitly, without prototype fallback', () => {
  assert.throws(() => new ProductionLauncherFacade(undefined, generation), /bridge/);
  assert.equal(launcherEntryMode('?mode=production', false), 'production');
  assert.equal(launcherEntryMode('?mode=prototype', false), 'invalid');
  for (const file of ['ProductionLauncherFacade.ts', 'ProductionLauncherController.ts', 'ProductionLauncherShell.tsx']) {
    const source = readFileSync(new URL('../src/' + file, import.meta.url), 'utf8');
    assert(!source.includes('useLauncherPrototype')); assert(!source.includes('identifyStates'));
    assert(!source.includes('950')); assert(!source.includes('2500')); assert(!source.includes('identityStatus'));
  }
});
await check('bridge', 'Invalid empty target rejects locally without sending a request', async () => page(async ({ facade, bridge }) => {
  const count = bridge.sent.length; await assert.rejects(facade.identifyDisplay(''), /target is invalid/); assert.equal(bridge.sent.length, count);
}));

for (const id of [targetId, weakId]) for (const status of ['Shown', 'Unresolved', 'InvalidRequest', 'ApiFailure']) {
  await check('semantics', (id === weakId ? 'Weak' : 'Target') + ' ' + status + ' preserves selection/catalog/registered facts', async () => page(async ({ controller, bridge }) => {
    controller.select(id); const before = controller.getState(), facts = structuredClone(before);
    const pending = controller.identifySelected(); const command = bridge.sent.at(-1);
    assert.deepEqual(command.payload, { displayId: id });
    bridge.receive(response(command, { displayId: id, status })); await pending;
    const after = controller.getState(); assert.equal(after.catalog, before.catalog); assert.equal(after.bootstrap, before.bootstrap);
    assert.deepEqual(after.catalog, facts.catalog); assert.deepEqual(after.bootstrap, facts.bootstrap);
    assert.equal(after.selectedDisplayId, id); assert.deepEqual(after.identifyFeedback, { displayId: id, status });
    assert.equal(after.catalog.displays.find(row => row.id === weakId).persistentOwnerCapable, false);
    assert.deepEqual(bridge.sent.map(item => item.type), ['GetLauncherBootstrapSnapshot', 'IdentifyDisplay']);
  }));
}
await check('semantics', 'Pending duplicate activations are suppressed synchronously', async () => page(async ({ controller, bridge }) => {
  const pending = controller.identifySelected(); assert.equal(controller.getState().identifyPending, true);
  await controller.identifySelected(); await controller.identifySelected(); assert.equal(bridge.sent.length, 2);
  bridge.receive(response(bridge.sent.at(-1), { displayId: targetId, status: 'Shown' })); await pending;
  assert.equal(controller.getState().identifyPending, false);
  const again = controller.identifySelected(); assert.equal(bridge.sent.length, 3);
  bridge.receive(response(bridge.sent.at(-1), { displayId: targetId, status: 'Shown' })); await again;
}));
await check('semantics', 'Selection changed while pending stays changed and feedback stays attached to old exact target', async () => page(async ({ controller, bridge }) => {
  const pending = controller.identifySelected(), command = bridge.sent.at(-1);
  controller.select(weakId); bridge.receive(response(command, { displayId: targetId, status: 'Unresolved' })); await pending;
  assert.equal(controller.getState().selectedDisplayId, weakId);
  assert.deepEqual(controller.getState().identifyFeedback, { displayId: targetId, status: 'Unresolved' });
}));
await check('semantics', 'Identify transport failure keeps all domain facts and selection intact', async () => page(async ({ controller, bridge }) => {
  const before = controller.getState(); const pending = controller.identifySelected();
  bridge.receive(response(bridge.sent.at(-1), {})); await pending; const after = controller.getState();
  assert.equal(after.catalog, before.catalog); assert.equal(after.bootstrap, before.bootstrap);
  assert.equal(after.selectedDisplayId, before.selectedDisplayId);
  assert.deepEqual(after.identifyFeedback, { displayId: targetId, status: 'TransportFailure' });
}));
await check('semantics', 'No selected valid row means no Identify request', async () => {
  for (const status of ['Success', 'ApiFailure', 'IncompletePathMetadata', 'DuplicateIdentity']) {
    const value = structuredClone(snapshot); value.displayEnumeration = { status, displays: [] };
    await page(async ({ controller, bridge }) => { await controller.identifySelected(); assert.equal(bridge.sent.length, 1); }, value);
  }
});
await check('semantics', 'Refresh during Identify preserves new current catalog and selected exact ID', async () => page(async ({ controller, bridge }) => {
  const identify = controller.identifySelected(), identifyCommand = bridge.sent.at(-1);
  const refresh = controller.refresh(), changedCatalog = { status: 'Success', displays: [catalog.displays[1]] };
  bridge.receive(response(bridge.sent.at(-1), changedCatalog)); await refresh;
  const before = controller.getState(); bridge.receive(response(identifyCommand, { displayId: targetId, status: 'Unresolved' })); await identify;
  assert.equal(controller.getState().catalog, before.catalog); assert.equal(controller.getState().selectedDisplayId, weakId);
}));
await check('semantics', 'Retired page receives no old Identify result; recreated page bootstraps fresh facts', async () => page(async ({ controller, bridge }) => {
  const pending = controller.identifySelected(), oldCommand = bridge.sent.at(-1), before = controller.getState();
  let updates = 0; controller.subscribe(() => updates++); controller.dispose(); await pending;
  const replacement = new ProductionLauncherController(new ProductionLauncherFacade(bridge, nextGeneration));
  try {
    const boot = replacement.bootstrap(), command = bridge.sent.at(-1);
    bridge.receive(response(oldCommand, { displayId: targetId, status: 'Shown' }));
    assert.equal(replacement.getState().bootstrap, null); assert.equal(controller.getState(), before); assert.equal(updates, 0);
    bridge.receive(response(command, structuredClone(snapshot))); await boot;
    assert.equal(replacement.getState().identifyFeedback, null); assert.equal(replacement.getState().identifyPending, false);
  } finally { replacement.dispose(); }
}));

const cache = new URL('../node_modules/.cache/launcher-identify-a3/', import.meta.url); mkdirSync(cache, { recursive: true });
const componentUrl = new URL('../src/ProductionLauncherShell.tsx', import.meta.url);
const renderModule = launcherRenderModule(componentUrl, cache);
const { ProductionLauncherView } = await import(renderModule.href + '?run=' + Date.now());
function render(extras = {}, language = 'en') {
  const facts = structuredClone(snapshot); facts.language = { readStatus: 'Success', committed: language, effective: language, revision: 0 };
  return renderToStaticMarkup(React.createElement(ProductionLauncherView, { snapshot: facts, catalog,
    selectedId: weakId, onSelect() {}, onRefresh() {}, onIdentify() {}, ...extras }));
}
await check('presentation', 'Selected name is an enabled native button, not nested in card or a new independent Identify button', () => {
  const html = render(); assert.equal((html.match(/data-identify-display=/g) ?? []).length, 1);
  assert.match(html, /<button type="button" class="display-name display-identify"[^>]*aria-label="Identify display Panel B"/);
  assert.match(html, /<button[^>]*class="display-name display-identify"[^>]*aria-disabled="false"/);
  assert(html.includes('</button><div class="display-identify-layer">')); assert(html.includes('data-identify-target="' + weakId + '"'));
});
await check('presentation', 'Pending disables only Identify request surface and does not say overlay is active', () => {
  const html = render({ identifyPending: true }); assert.match(html, /data-identify-display="true"[^>]*aria-busy="true"[^>]*aria-disabled="true"/);
  assert(html.includes('Identifying display…')); assert(!html.includes('IDENTIFYING')); assert(!html.includes('Identity verified'));
});
for (const [status, english, chinese] of [
  ['Unresolved', 'This display cannot currently be located.', '当前无法定位该显示器。'],
  ['InvalidRequest', 'The Identify request is invalid.', '识别请求无效。'],
  ['ApiFailure', 'Unable to show the Identify marker.', '无法显示识别标记。'],
  ['TransportFailure', 'The Identify request did not complete. Please retry.', '识别请求未完成，请重试。'],
]) await check('presentation', status + ' feedback uses effective application language', () => {
  assert(render({ identifyFeedback: { displayId: weakId, status } }).includes(english));
  assert(render({ identifyFeedback: { displayId: weakId, status } }, 'zh_CN').includes(chinese));
});
await check('presentation', 'Shown adds no promotion language or fake overlay; weak owner capability remains explained', () => {
  const html = render({ identifyFeedback: { displayId: weakId, status: 'Shown' } });
  assert(html.includes('Workspaces can’t be opened on this display right now.'));
  assert(!html.includes('Source identity'));
  assert.match(html, /data-session-action="Create"[^>]*disabled=""/);
  for (const forbidden of ['Identity verified', 'Display approved', 'Display ready', 'identify-overlay', 'OPEN SESSION', 'DEV STATE']) assert(!html.includes(forbidden));
});
await check('presentation', 'Old target failure is not associated with a newly selected display name', () => {
  const html = render({ identifyFeedback: { displayId: targetId, status: 'Unresolved' } });
  assert(!html.includes('This display cannot currently be located.')); assert(html.includes('data-identify-result=""'));
});
const output = { pass: checks.every(item => item.pass), red: 'IdentifyDisplay facade was undefined before implementation', checks };
writeFileSync(new URL('focused-checks.json', cache), JSON.stringify(output, null, 2));
const groups = Object.fromEntries(['bridge', 'semantics', 'presentation'].map(group => {
  const matching = checks.filter(item => item.group === group); return [group, { passed: matching.filter(item => item.pass).length, total: matching.length }];
}));
console.log(JSON.stringify({ pass: output.pass, groups, checks: checks.length, failed: checks.filter(item => !item.pass) }, null, 2));
assert(output.pass, 'Production Launcher A3 focused checks must pass');
