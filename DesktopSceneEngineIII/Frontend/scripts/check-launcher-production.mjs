import assert from 'node:assert/strict';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { launcherRenderModule } from './launcher-render-module.mjs';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';

// RED originally fails here: the Launcher had only a fixture-backed entry.
const facadeUrl = new URL('../src/ProductionLauncherFacade.ts', import.meta.url);
assert(existsSync(facadeUrl), 'A production Launcher bootstrap path must exist independently of the prototype');
const { ProductionLauncherFacade, readLauncherSnapshot, reconcileDisplaySelection, launcherEntryMode } = await import(facadeUrl.href);
const checks = [];
async function check(name, run) {
  try { await run(); checks.push({ name, pass: true }); }
  catch (error) { checks.push({ name, pass: false, error: String(error) }); }
}
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const otherGeneration = 'd7951708-aa40-497e-9160-37a96e1063e3';
const targetId = 'opaque target : \\?\\DISPLAY#メーカー#not-a-deck-index';
const weakId = 'opaque source : \\.\\DISPLAY2';
function snapshot() {
  return {
    coordinatorEpoch: 'dba10904-5b14-43f0-995b-17bc99181d43', sessionRevision: 0,
    displayEnumeration: { status: 'Success', displays: [
      { id: targetId, identityKind: 'TargetDevicePath', persistentOwnerCapable: true, friendlyName: 'Actual manufacturer panel', sourceDeviceName: '\\\\.\\DISPLAY1', desktopX: -1920, desktopY: 0, pixelWidth: 1920, pixelHeight: 1080, primary: true, displayNumber: 1, refreshRate: { numerator: 60000, denominator: 1001 } },
      { id: weakId, identityKind: 'SourceDeviceName', persistentOwnerCapable: false, friendlyName: 'Weak source panel', sourceDeviceName: '\\\\.\\DISPLAY2', desktopX: 0, desktopY: 0, pixelWidth: 2560, pixelHeight: 1440, primary: false, displayNumber: 2 },
    ] },
    registeredSessions: [{ boundDisplayId: targetId, incarnation: 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2', controlConnected: true }],
    registeredSessionsScope: 'verified-registered-only', createOperations: [],
    language: { readStatus: 'Success', committed: 'en', effective: 'en', revision: 0 },
    host: { ready: true, launcherRoleReady: true },
  };
}
function transport() {
  const listeners = new Set();
  return {
    sent: [], listeners,
    postMessage(command) { this.sent.push(command); },
    addEventListener(type, listener) { assert.equal(type, 'message'); listeners.add(listener); },
    removeEventListener(type, listener) { assert.equal(type, 'message'); listeners.delete(listener); },
    receive(data) { for (const listener of listeners) listener({ data }); },
  };
}
function result(command, payload = snapshot(), overrides = {}) {
  return { kind: 'result', version: 1, requestId: command.requestId, type: 'GetLauncherBootstrapSnapshot', generation, ok: true, payload, ...overrides };
}
await check('Explicit production entry fails closed; built defaults cannot select fixtures', () => {
  assert.equal(launcherEntryMode('?mode=production', false), 'production');
  assert.equal(launcherEntryMode('?mode=prototype', false), 'invalid');
  assert.equal(launcherEntryMode('', false), 'invalid');
  assert.equal(launcherEntryMode('?mode=prototype', true), 'prototype');
  assert.equal(launcherEntryMode('?view=session', true), 'prototype');
  assert.equal(launcherEntryMode('?mode=unknown', true), 'invalid');
});
await check('Snapshot keeps raw display identity and verified roles, including en/zh_CN and language failure', () => {
  const original = snapshot(); assert.deepEqual(readLauncherSnapshot(original), original);
  original.language = { readStatus: 'Success', committed: 'zh_CN', effective: 'zh_CN', revision: 0 };
  assert.deepEqual(readLauncherSnapshot(original), original);
  original.language = { readStatus: 'Failure', effective: 'en', revision: 0 };
  assert.deepEqual(readLauncherSnapshot(original), original);
  original.registeredSessions = [];
  assert.deepEqual(readLauncherSnapshot(original).registeredSessions, []);
});
await check('Success empty and each enumeration failure are distinct valid facts', () => {
  for (const status of ['Success', 'ApiFailure', 'IncompletePathMetadata', 'DuplicateIdentity']) {
    const value = snapshot(); value.displayEnumeration = { status, displays: [] };
    assert.equal(readLauncherSnapshot(value).displayEnumeration.status, status);
  }
});
await check('Invalid or overstated snapshots fail closed', () => {
  for (const mutate of [
    value => value.registeredSessionsScope = 'all-occupied-slots',
    value => value.displayEnumeration.displays[0].id = '',
    value => value.displayEnumeration.displays[1].id = targetId,
    value => value.displayEnumeration.displays[1].persistentOwnerCapable = true,
    value => value.displayEnumeration.displays[0].pixelWidth = NaN,
    value => value.displayEnumeration.displays[0].refreshRate.denominator = 0,
    value => value.displayEnumeration.status = 'ApiFailure',
    value => value.registeredSessions[0].pid = 123,
    value => value.language.committed = 'fr',
    value => delete value.language.committed,
    value => value.host.launcherRoleReady = false,
  ]) { const value = snapshot(); mutate(value); assert.throws(() => readLauncherSnapshot(value)); }
});
await check('Selection is an opaque DisplayId: first / preserve through reorder / disappeared / empty / failure', () => {
  const value = snapshot();
  assert.equal(reconcileDisplaySelection(null, value.displayEnumeration), targetId);
  assert.equal(reconcileDisplaySelection('missing', value.displayEnumeration), targetId);
  assert.equal(reconcileDisplaySelection(weakId, value.displayEnumeration), weakId);
  value.displayEnumeration.displays.reverse();
  assert.equal(reconcileDisplaySelection(targetId, value.displayEnumeration), targetId);
  value.displayEnumeration.displays.pop();
  assert.equal(reconcileDisplaySelection(targetId, value.displayEnumeration), weakId);
  value.displayEnumeration.displays = [];
  assert.equal(reconcileDisplaySelection(weakId, value.displayEnumeration), null);
  value.displayEnumeration.status = 'ApiFailure';
  assert.equal(reconcileDisplaySelection(weakId, value.displayEnumeration), null);
});
await check('Facade sends exactly one readonly command, correlated success resolves validated native facts', async () => {
  const bridge = transport(); const facade = new ProductionLauncherFacade(bridge, generation);
  try {
    const pending = facade.getBootstrapSnapshot();
    assert.equal(bridge.sent.length, 1);
    const command = bridge.sent[0];
    assert.deepEqual(Object.keys(command).sort(), ['kind', 'version', 'requestId', 'type', 'generation', 'payload'].sort());
    assert.equal(command.kind, 'command'); assert.equal(command.version, 1);
    assert.equal(command.type, 'GetLauncherBootstrapSnapshot'); assert.equal(command.generation, generation);
    assert.deepEqual(command.payload, {}); assert.match(command.requestId, /^[A-Za-z0-9_-]{1,128}$/);
    bridge.receive(result(command));
    assert.deepEqual(await pending, snapshot());
  } finally { facade.dispose(); }
  assert.equal(bridge.listeners.size, 0);
});
await check('Old generation, wrong correlation and non-result messages cannot complete a request', async () => {
  const bridge = transport(); const facade = new ProductionLauncherFacade(bridge, generation);
  try {
    let settled = false; const pending = facade.getBootstrapSnapshot().then(value => { settled = true; return value; });
    const command = bridge.sent[0];
    bridge.receive(result(command, snapshot(), { generation: otherGeneration }));
    bridge.receive(result(command, snapshot(), { requestId: 'old-request' }));
    bridge.receive({ ...result(command), kind: 'event' });
    await Promise.resolve(); assert.equal(settled, false);
    bridge.receive(result(command)); assert.deepEqual(await pending, snapshot());
  } finally { facade.dispose(); }
});
await check('Correlated malformed/native failure results show errors; no fixture resolution', async () => {
  for (const response of [
    command => result(command, {}, {}),
    command => result(command, snapshot(), { version: 2 }),
    command => ({ ...result(command), extra: true }),
    command => ({ kind: 'result', version: 1, type: command.type, requestId: command.requestId, generation, ok: false, error: 'Native bootstrap unavailable' }),
  ]) {
    const bridge = transport(); const facade = new ProductionLauncherFacade(bridge, generation);
    try { const pending = facade.getBootstrapSnapshot(); const rejected = assert.rejects(pending); bridge.receive(response(bridge.sent[0])); await rejected; }
    finally { facade.dispose(); }
  }
});
await check('Dispose retires callbacks; recreated page requests a fresh snapshot with new generation', async () => {
  const bridge = transport(); const facade = new ProductionLauncherFacade(bridge, generation);
  const pending = facade.getBootstrapSnapshot(); const retired = assert.rejects(pending, /retired/);
  const old = bridge.sent[0]; facade.dispose(); await retired;
  assert.equal(bridge.listeners.size, 0);
  const replacement = new ProductionLauncherFacade(bridge, otherGeneration);
  try {
    let settled = false; const fresh = replacement.getBootstrapSnapshot().then(value => { settled = true; return value; });
    bridge.receive(result(old)); await Promise.resolve(); assert.equal(settled, false);
    const command = bridge.sent[1];
    assert.notEqual(command.requestId, old.requestId); assert.equal(command.generation, otherGeneration);
    bridge.receive(result(command, snapshot(), { generation: otherGeneration })); assert.deepEqual(await fresh, snapshot());
  } finally { replacement.dispose(); }
});
await check('Missing bridge or invalid page generation fails explicitly', () => {
  assert.throws(() => new ProductionLauncherFacade(undefined, generation), /bridge/);
  assert.throws(() => new ProductionLauncherFacade(transport(), null), /generation/);
  assert.throws(() => new ProductionLauncherFacade(transport(), 'mock-generation'), /generation/);
});

// Render actual production JSX, keeping the production snapshot contract module.
const cache = new URL('../node_modules/.cache/launcher-production-a1/', import.meta.url);
mkdirSync(cache, { recursive: true });
const componentUrl = new URL('../src/ProductionLauncherShell.tsx', import.meta.url);
const renderModule = launcherRenderModule(componentUrl, cache);
const { ProductionLauncherView } = await import(renderModule.href + '?run=' + Date.now());
function render(value, selectedId = value.displayEnumeration.displays[0]?.id ?? null) {
  return renderToStaticMarkup(React.createElement(ProductionLauncherView, { snapshot: value, catalog: value.displayEnumeration, selectedId, onSelect() {}, onRefresh() {} }));
}
const visibleText = html => html.replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ');
await check('Chinese Launcher uses public display names and workspace language without showing Windows identities', () => {
  const value = snapshot();
  value.language = { readStatus: 'Success', committed: 'zh_CN', effective: 'zh_CN', revision: 0 };
  value.registeredSessions = [];
  value.displayEnumeration.displays[0].friendlyName = '';
  value.displayEnumeration.displays[1].friendlyName = '24G40E';
  const text = visibleText(render(value, targetId));
  for (const expected of ['显示器 1', '24G40E', '尚未打开', '开始使用', '在这块屏幕上打开一个工作区。'])
    assert(text.includes(expected), expected);
  for (const forbidden of ['DISPLAY1', '显示器目录', '持久会话归属', '经过验证', '当前语言', '已提交'])
    assert(!text.includes(forbidden), forbidden);
  value.displayEnumeration.displays[0].friendlyName = String.raw`\\?\DISPLAY#INTERNAL`;
  assert(!visibleText(render(value, targetId)).includes('INTERNAL'));
  value.registeredSessions = snapshot().registeredSessions;
  const opened = visibleText(render(value, targetId));
  assert(opened.includes('打开工作区') && opened.includes('返回这块屏幕的工作区。'));
  value.displayEnumeration.displays = [];
  assert(!visibleText(render(value)).includes(targetId));
});
await check('Production JSX renders actual metadata and only verified registration facts', () => {
  const html = render(snapshot());
  assert(html.includes('Actual manufacturer panel')); assert(html.includes('1920')); assert(html.includes('1080'));
  assert(html.includes('data-display-id="' + targetId.replaceAll('&', '&amp;').replaceAll('"', '&quot;') + '"'));
  assert(html.includes('data-session-status="ACTIVE"')); assert(html.includes('data-session-action="Open"')); assert(html.includes('data-action-incarnation="ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2"'));
  assert(html.includes('aria-label="Refresh displays"'));
  assert(html.includes('data-identify-display="true"'));
  for (const forbidden of ['DEV STATE', 'Hide to tray', 'AVAILABLE', 'display-a']) assert(!html.includes(forbidden), forbidden);
});
await check('Registry absence is unknown occupancy, weak identities are explained without fake UI states', () => {
  const value = snapshot(); value.registeredSessions = [];
  const html = render(value, weakId);
  assert(visibleText(html).includes('Not open')); assert(html.includes('data-session-status="noRegisteredSession"'));
  assert(visibleText(html).includes('Workspaces can’t be opened on this display right now.'));
  assert.match(html, /data-session-action="Create"[^>]* disabled=""/);
  for (const forbidden of ['STARTING', 'CLOSING', 'DISCONNECTED', 'AVAILABLE']) assert(!html.includes(forbidden));
});
await check('Actual component shows empty and enumeration failures without fabricated display rows', () => {
  for (const status of ['Success', 'ApiFailure', 'IncompletePathMetadata', 'DuplicateIdentity']) {
    const value = snapshot(); value.displayEnumeration = { status, displays: [] };
    const html = render(value);
    assert(html.includes(status === 'Success' ? 'No displays found' : 'Couldn’t find displays'));
    assert(!html.includes('data-display-id='));
    if (status !== 'Success') assert(html.includes(status));
  }
});
await check('Effective language bootstrap is read-only and visibly handles preference read failure', () => {
  const value = snapshot(); value.language = { readStatus: 'Success', committed: 'zh_CN', effective: 'zh_CN', revision: 0 };
  assert(render(value).includes('data-language="zh_CN"'));
  value.language = { readStatus: 'Failure', effective: 'en', revision: 0 };
  assert(visibleText(render(value)).includes('Your saved language couldn’t be loaded.'));
});

const output = { pass: checks.every(item => item.pass), checks };
writeFileSync(new URL('focused-checks.json', cache), JSON.stringify(output, null, 2));
console.log(JSON.stringify({ pass: output.pass, checks: checks.length, failed: checks.filter(item => !item.pass) }, null, 2));
assert(output.pass, 'Production Launcher focused checks must pass');
