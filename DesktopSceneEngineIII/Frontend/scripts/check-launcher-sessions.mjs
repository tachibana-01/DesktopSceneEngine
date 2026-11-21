import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { launcherRenderModule } from './launcher-render-module.mjs';
import { ProductionLauncherFacade, readLauncherSnapshot, readLauncherSessionsSnapshot } from '../src/ProductionLauncherFacade.ts';
import { ProductionLauncherController } from '../src/ProductionLauncherController.ts';
import { launcherSessionPresentation } from '../src/launcherSessionPresentation.ts';
import { launcherCopy } from '../src/launcherLanguage.ts';

const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const epoch = 'dba10904-5b14-43f0-995b-17bc99181d43';
const otherEpoch = 'd7951708-aa40-497e-9160-37a96e1063e3';
const incarnation = 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2';
const replacement = 'b902bc98-e64a-43e0-ae66-16e257e5cd5b';
const row = { id: 'opaque exact target 日本語', identityKind: 'TargetDevicePath', persistentOwnerCapable: true,
  friendlyName: 'Panel A', sourceDeviceName: '\\\\.\\DISPLAY1', desktopX: 0, desktopY: 0,
  pixelWidth: 1920, pixelHeight: 1080, primary: true, displayNumber: 1 };
const catalog = (displays = [row], status = 'Success') => ({ status, displays });
const role = (patch = {}) => ({ boundDisplayId: row.id, incarnation, controlConnected: true, ...patch });
const sessions = (revision = 0, roles = [role()], owner = epoch) => ({ coordinatorEpoch: owner, sessionRevision: revision,
  registeredSessionsScope: 'verified-registered-only', createOperations: [], registeredSessions: roles });
const snapshot = (projection = sessions()) => ({ ...projection, displayEnumeration: catalog(),
  language: { readStatus: 'Success', committed: 'en', effective: 'en', revision: 0 }, host: { ready: true, launcherRoleReady: true } });
const close = result => ({ generation: otherEpoch, request: '18446744073709551615', result });
const presentation = (facts = sessions(), displays = catalog()) => [...launcherSessionPresentation(facts, displays).values()];
const checks = [];
async function check(group, name, run) {
  try { await run(); checks.push({ group, name, pass: true }); }
  catch (error) { checks.push({ group, name, pass: false, error: String(error) }); }
}
function fixture() {
  const listeners = new Set();
  const bridge = { sent: [], postMessage(command) { this.sent.push(command); },
    addEventListener(_type, listener) { listeners.add(listener); }, removeEventListener(_type, listener) { listeners.delete(listener); },
    receive(data) { for (const listener of listeners) listener({ data }); } };
  const facade = new ProductionLauncherFacade(bridge, generation), controller = new ProductionLauncherController(facade);
  const event = (payload, patch = {}) => bridge.receive({ kind: 'event', version: 1, type: 'LauncherSessionsChanged', generation, payload, ...patch });
  const reply = (payload, command = bridge.sent.at(-1)) => bridge.receive({ kind: 'result', version: 1,
    requestId: command.requestId, type: command.type, generation, ok: true, payload });
  return { bridge, facade, controller, event, reply, async boot(payload = snapshot()) { const p = controller.bootstrap(); reply(payload); await p; }, dispose() { controller.dispose(); } };
}
await check('contract', 'One strict Session schema serves standalone events and extended bootstrap', () => {
  assert.deepEqual(readLauncherSessionsSnapshot(sessions()), sessions()); assert.deepEqual(readLauncherSnapshot(snapshot()), snapshot());
  assert.deepEqual(readLauncherSessionsSnapshot(sessions(0, [])), sessions(0, []));
  const two = sessions(0, [role(), role({ boundDisplayId: 'other exact target', incarnation: replacement })]);
  assert.deepEqual(readLauncherSessionsSnapshot(two), two);
});
await check('contract', 'Identity, safe revision and zero-payload extra fields fail closed', () => {
  for (const mutate of [
    value => value.coordinatorEpoch = '00000000-0000-0000-0000-000000000000',
    value => value.coordinatorEpoch = 'pid:42', value => value.sessionRevision = -1,
    value => value.sessionRevision = Number.MAX_SAFE_INTEGER + 1, value => value.sessionRevision = '1',
    value => value.registeredSessionsScope = 'all-occupied-slots', value => value.extra = 1,
    value => value.registeredSessions[0].boundDisplayId = '', value => value.registeredSessions[0].incarnation = '00000000-0000-0000-0000-000000000000',
    value => value.registeredSessions[0].controlConnected = 0, value => delete value.registeredSessions[0].controlConnected,
    value => value.registeredSessions[0].pid = 123, value => value.registeredSessions[0].hwnd = 123,
    value => value.registeredSessions = Array.from({ length: 65 }, () => role()),
  ]) { const value = sessions(); mutate(value); assert.throws(() => readLauncherSessionsSnapshot(value)); assert.throws(() => readLauncherSnapshot({ ...snapshot(), ...value })); }
});
await check('contract', 'Duplicate exact DisplayId or normalized incarnation is rejected, never selected', () => {
  for (const duplicate of [role({ incarnation: replacement }), role({ boundDisplayId: 'another', incarnation: '{' + incarnation.toUpperCase() + '}' })]) {
    assert.throws(() => readLauncherSessionsSnapshot(sessions(1, [role(), duplicate])));
  }
});
await check('contract', 'Close observation preserves previous-epoch generation and exact positive uint64 request string', () => {
  for (const result of ['Received', 'Pending', 'CloseAccepted', 'CloseAborted', 'Rejected', 'Uncertain']) {
    assert.deepEqual(readLauncherSessionsSnapshot(sessions(1, [role({ closeOperation: close(result) })])).registeredSessions[0].closeOperation, close(result));
  }
});
await check('contract', 'Malformed close receipts including live RoleGone fail closed', () => {
  for (const patch of [{ result: 'RoleGone' }, { result: 'CRASHED' }, { request: 1 }, { request: '0' }, { request: '01' },
    { request: '18446744073709551616' }, { request: '+1' }, { request: '1e1' }, { generation: null }, { pid: 2 }]) {
    assert.throws(() => readLauncherSessionsSnapshot(sessions(1, [role({ closeOperation: { ...close('Pending'), ...patch } })])));
  }
});
await check('mapping', 'Registered exact canonical Display resolves to ACTIVE with exact incarnation', () => {
  const [value] = presentation(); assert.equal(value.status, 'ACTIVE'); assert.equal(value.facts.incarnation, incarnation); assert.equal(value.displayResolution, 'resolved');
});
await check('mapping', 'Successful absence is DISCONNECTED; reappearance preserves incarnation', () => {
  const facts = sessions(); const lost = presentation(facts, catalog([]))[0]; assert.equal(lost.status, 'DISCONNECTED');
  assert.equal(lost.facts.incarnation, incarnation); assert.equal(presentation(facts)[0].status, 'ACTIVE');
});
await check('mapping', 'Friendly name, source formatting, display number and index cannot resolve a bound identity', () => {
  assert.equal(presentation(sessions(), catalog([{ ...row, id: 'different exact ID' }]))[0].status, 'DISCONNECTED');
});
await check('mapping', 'ApiFailure, other catalog failure and transport unavailability mean unknown, never DISCONNECTED or ACTIVE', () => {
  for (const value of [null, catalog([], 'ApiFailure'), catalog([], 'IncompletePathMetadata'), catalog([], 'DuplicateIdentity')]) {
    const mapped = presentation(sessions(), value)[0]; assert.equal(mapped.status, 'REGISTERED'); assert.equal(mapped.displayResolution, 'unknown');
  }
});
await check('mapping', 'Control-channel loss retains identity and independent secondary fact', () => {
  const [mapped] = presentation(sessions(1, [role({ controlConnected: false })]));
  assert.equal(mapped.status, 'ACTIVE'); assert.equal(mapped.controlUnavailable, true); assert.equal(mapped.facts.incarnation, incarnation);
});
for (const result of ['Received', 'Pending', 'CloseAccepted']) await check('mapping', result + ' maps CLOSING and preserves role even with bound Display absent', () => {
  const [mapped] = presentation(sessions(1, [role({ closeOperation: close(result) })]), catalog([]));
  assert.equal(mapped.status, 'CLOSING'); assert.equal(mapped.displayResolution, 'unresolved'); assert.equal(mapped.facts.incarnation, incarnation);
});
for (const result of ['CloseAborted', 'Rejected']) await check('mapping', result + ' exits CLOSING without removing role', () => {
  assert.equal(presentation(sessions(1, [role({ closeOperation: close(result) })]))[0].status, 'ACTIVE');
});
await check('mapping', 'Uncertain is an observation warning and does not resolve death or close via timer', () => {
  const [mapped] = presentation(sessions(1, [role({ closeOperation: close('Uncertain') })]));
  assert.equal(mapped.status, 'ACTIVE'); assert.equal(mapped.closeUncertain, true);
});
await check('mapping', 'Registry removal erases exact incarnation; absence makes no availability or synthetic closing claim', () => {
  assert.equal(launcherSessionPresentation(sessions(1, []), catalog()).size, 0);
  const [newRole] = presentation(sessions(2, [role({ incarnation: replacement })])); assert.equal(newRole.facts.incarnation, replacement);
});
await check('ordering', 'Live join and verified removal update controller without Refresh or added commands', async () => {
  const f = fixture(); try { await f.boot(snapshot(sessions(0, []))); f.event(sessions(1)); assert.equal(f.controller.getState().bootstrap.registeredSessions[0].incarnation, incarnation);
    f.event(sessions(2, [])); assert.equal(f.controller.getState().bootstrap.registeredSessions.length, 0); assert.deepEqual(f.bridge.sent.map(x => x.type), ['GetLauncherBootstrapSnapshot']); } finally { f.dispose(); }
});
await check('ordering', 'Older and equal revisions cannot resurrect a removed incarnation', async () => {
  const f = fixture(); try { await f.boot(); f.event(sessions(2, [])); f.event(sessions(1)); f.event(sessions(2));
    assert.equal(f.controller.getState().bootstrap.registeredSessions.length, 0); } finally { f.dispose(); }
});
await check('ordering', 'Newer event before bootstrap beats older returned snapshot', async () => {
  const f = fixture(); try { const p = f.controller.bootstrap(); f.event(sessions(3, [role({ incarnation: replacement })])); f.reply(snapshot(sessions(0))); await p;
    assert.equal(f.controller.getState().bootstrap.sessionRevision, 3); assert.equal(f.controller.getState().bootstrap.registeredSessions[0].incarnation, replacement); } finally { f.dispose(); }
});
await check('ordering', 'Old-page, malformed and duplicate events cannot change current authority', async () => {
  const f = fixture(); try { await f.boot(); f.event(sessions(2, []), { generation: otherEpoch });
    f.event({ ...sessions(3, []), sessionRevision: '3' }); f.event(sessions(4, [role(), role({ incarnation: replacement })]));
    f.event(sessions(5, []), { extra: true }); assert.equal(f.controller.getState().bootstrap.sessionRevision, 0); } finally { f.dispose(); }
});
await check('ordering', 'Unexpected epoch clears old view, blocks old epoch resurrection and requires bootstrap retry', async () => {
  const f = fixture(); try { await f.boot(); f.event(sessions(0, [], otherEpoch)); assert.equal(f.controller.getState().bootstrap, null);
    assert.equal(f.controller.getState().catalog, null); assert(f.controller.getState().bootstrapError);
    f.event(sessions(100)); await f.boot(snapshot(sessions(100))); assert.equal(f.controller.getState().bootstrap, null);
    await f.boot(snapshot(sessions(1, [role({ incarnation: replacement })], otherEpoch)));
    assert.equal(f.controller.getState().bootstrap.coordinatorEpoch, otherEpoch); f.event(sessions(101));
    assert.equal(f.controller.getState().bootstrap.coordinatorEpoch, otherEpoch); } finally { f.dispose(); }
});
await check('ordering', 'Epoch change during pending bootstrap retires its eventual old result', async () => {
  const f = fixture(); try { const p = f.controller.bootstrap(); f.event(sessions()); f.event(sessions(0, [], otherEpoch)); f.reply(snapshot()); await p;
    assert.equal(f.controller.getState().bootstrap, null); assert.equal(f.controller.getState().bootstrapPending, false); } finally { f.dispose(); }
});
await check('ordering', 'First new-epoch event before older bootstrap fences that old epoch and late events', async () => {
  const f = fixture(); try { const p = f.controller.bootstrap(); f.event(sessions(0, [role({ incarnation: replacement })], otherEpoch)); f.reply(snapshot()); await p;
    assert.equal(f.controller.getState().bootstrap, null); f.event(sessions(100));
    await f.boot(snapshot(sessions(1, [role({ incarnation: replacement })], otherEpoch)));
    assert.equal(f.controller.getState().bootstrap.coordinatorEpoch, otherEpoch); assert.equal(f.controller.getState().bootstrap.registeredSessions[0].incarnation, replacement); } finally { f.dispose(); }
});
await check('ordering', 'Refresh rederives resolution while retaining exact authoritative role', async () => {
  const f = fixture(); try { await f.boot(); const refresh = async value => { const p = f.controller.refresh(); f.reply(value); await p; };
    await refresh(catalog([])); assert.equal(presentation(f.controller.getState().bootstrap, f.controller.getState().catalog)[0].status, 'DISCONNECTED');
    await refresh(catalog([], 'ApiFailure')); assert.equal(presentation(f.controller.getState().bootstrap, f.controller.getState().catalog)[0].status, 'REGISTERED');
    await refresh(catalog()); assert.equal(f.controller.getState().bootstrap.registeredSessions[0].incarnation, incarnation); } finally { f.dispose(); }
});
await check('ordering', 'Dispose fences events; new page reconstructs role only from fresh bootstrap', async () => {
  const old = fixture(); await old.boot(); old.dispose(); old.event(sessions(2, [])); assert.equal(old.controller.getState().bootstrap.sessionRevision, 0);
  const fresh = fixture(); try { await fresh.boot(snapshot(sessions(3, [role({ incarnation: replacement })]))); assert.equal(fresh.controller.getState().bootstrap.registeredSessions[0].incarnation, replacement); } finally { fresh.dispose(); }
});
const cache = new URL('../node_modules/.cache/launcher-sessions-a5/', import.meta.url); mkdirSync(cache, { recursive: true });
const { ProductionLauncherView } = await import(launcherRenderModule(new URL('../src/ProductionLauncherShell.tsx', import.meta.url), cache).href + '?run=' + Date.now());
function render(projection = sessions(), displays = catalog(), language = 'en') {
  const value = snapshot(projection); value.language = { ...value.language, committed: language, effective: language };
  return renderToStaticMarkup(React.createElement(ProductionLauncherView, { snapshot: value, catalog: displays,
    selectedId: displays?.displays[0]?.id ?? null, onSelect() {}, onRefresh() {} }));
}
function statusText(html, status) {
  return [...html.matchAll(/data-session-status="([^"]+)"[^>]*><span[^>]*><\/span>([^<]+)<\/span>/g)]
    .filter(match => match[1] === status).map(match => match[2]);
}
for (const language of ['en', 'zh_CN']) await check('presentation', language + ' labels active, idle, disconnected and unknown workspaces without exposing their identifiers', () => {
  const copy = launcherCopy(language);
  assert(statusText(render(sessions(), catalog(), language), 'ACTIVE').includes(copy.active));
  assert(statusText(render(sessions(1, []), catalog(), language), 'noRegisteredSession').includes(copy.noRegisteredSession));
  const disconnected = render(sessions(), catalog([]), language);
  assert.deepEqual(statusText(disconnected, 'DISCONNECTED'), [copy.disconnected]);
  assert(disconnected.includes('data-session-incarnation="' + incarnation + '"'));
  assert(!disconnected.replace(/<[^>]*>/g, '').includes(row.id));
  const unknown = render(sessions(1, [role({ controlConnected: false, closeOperation: close('Uncertain') })]), catalog([], 'ApiFailure'), language);
  for (const text of [copy.registeredSessions, copy.resolutionUnknown, copy.controlUnavailable, copy.closeUncertain]) assert(unknown.includes(text), text);
  assert.deepEqual(statusText(unknown, 'REGISTERED'), [copy.registered]);
  assert.deepEqual(statusText(unknown, 'DISCONNECTED'), []); assert.deepEqual(statusText(unknown, 'ACTIVE'), []);
});
await check('presentation', 'Production contains no lifecycle polling/timers, mock or Close command path', () => {
  for (const name of ['ProductionLauncherController.ts', 'ProductionLauncherShell.tsx', 'launcherSessionPresentation.ts']) {
    const source = readFileSync(new URL('../src/' + name, import.meta.url), 'utf8');
    for (const forbidden of ['setInterval(', 'setTimeout(', 'CloseSession', 'useLauncherPrototype']) assert(!source.includes(forbidden), name + forbidden);
  }
  const html = render(sessions(0, [])); for (const forbidden of ['AVAILABLE', 'STARTING', 'CRASHED', 'data-session-status="CLOSING"']) assert(!html.includes(forbidden));
});
const groups = Object.fromEntries(['contract', 'mapping', 'ordering', 'presentation'].map(group => {
  const list = checks.filter(check => check.group === group); return [group, { passed: list.filter(check => check.pass).length, total: list.length }];
}));
const output = { pass: checks.every(check => check.pass), red: 'Current ProductionLauncherFacade.subscribeSessions was undefined before A5 (runtime assertion failed).', groups, checks };
writeFileSync(new URL('focused-checks.json', cache), JSON.stringify(output, null, 2));
console.log(JSON.stringify({ pass: output.pass, groups, checks: checks.length, failed: checks.filter(check => !check.pass) }, null, 2));
assert(output.pass, 'A5 Session projection focused checks must pass');
