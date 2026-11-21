import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { ProductionLauncherFacade, readLauncherLanguage, readLauncherLanguageResult } from '../src/ProductionLauncherFacade.ts';
import { ProductionLauncherController } from '../src/ProductionLauncherController.ts';
import { launcherLocale, canonicalLauncherLanguage, launcherCopy, launcherLanguageFeedback } from '../src/launcherLanguage.ts';
import { launcherRenderModule } from './launcher-render-module.mjs';

// Observed RED before implementation: commitLanguage was undefined on the real
// production facade. These GREEN checks exercise that facade and controller.
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const otherGeneration = 'd7951708-aa40-497e-9160-37a96e1063e3';
const projection = (committed = 'en', effective = committed, revision = 0) => ({ readStatus: 'Success', committed, effective, revision });
const failedRead = (effective = 'en', revision = 1) => ({ readStatus: 'Failure', effective, revision });
const row = { id: 'opaque target', identityKind: 'TargetDevicePath', persistentOwnerCapable: true, friendlyName: '24G40E',
  sourceDeviceName: '\\\\.\\DISPLAY1', desktopX: 2560, desktopY: -660, pixelWidth: 1920, pixelHeight: 1080, primary: true, displayNumber: 2,
  refreshRate: { numerator: 60, denominator: 1 } };
const snapshot = language => ({ coordinatorEpoch: 'dba10904-5b14-43f0-995b-17bc99181d43', sessionRevision: 0,
  displayEnumeration: { status: 'Success', displays: [row] }, registeredSessions: [], registeredSessionsScope: 'verified-registered-only', createOperations: [],
  language, host: { ready: true, launcherRoleReady: true } });
const result = (status, language = projection('zh_CN', 'zh_CN', 1), warnings = []) => ({ status, language, warnings });
const checks = [];
async function check(group, name, run) {
  try { await run(); checks.push({ group, name, pass: true }); }
  catch (error) { checks.push({ group, name, pass: false, error: String(error) }); }
}
function transport() {
  const listeners = new Set();
  return { sent: [], postMessage(command) { this.sent.push(command); },
    addEventListener(_type, listener) { listeners.add(listener); }, removeEventListener(_type, listener) { listeners.delete(listener); },
    receive(value) { for (const listener of listeners) listener({ data: value }); } };
}
const response = (command, payload, overrides = {}) => ({ kind: 'result', version: 1, requestId: command.requestId,
  type: command.type, generation: command.generation, ok: true, payload, ...overrides });
const event = (payload, overrides = {}) => ({ kind: 'event', version: 1, type: 'LauncherLanguageChanged', generation, payload, ...overrides });
async function page(run, initial = projection()) {
  const bridge = transport(), facade = new ProductionLauncherFacade(bridge, generation), controller = new ProductionLauncherController(facade);
  try {
    const pending = controller.bootstrap(); bridge.receive(response(bridge.sent[0], snapshot(initial))); await pending;
    await run({ bridge, facade, controller, state: () => controller.getState(), receive: value => bridge.receive(value) });
  } finally { controller.dispose(); }
}
await check('contract', 'Canonical mapping is explicit and reversible', () => {
  assert.equal(launcherLocale('en'), 'en'); assert.equal(launcherLocale('zh_CN'), 'zh-CN');
  assert.equal(canonicalLauncherLanguage('en'), 'en'); assert.equal(canonicalLauncherLanguage('zh-CN'), 'zh_CN');
});
await check('contract', 'One projection parser keeps committed, effective, unknown read and revision separate', () => {
  for (const value of [projection(), projection('zh_CN', 'en', 7), failedRead('zh_CN', 8)]) assert.deepEqual(readLauncherLanguage(value), value);
  for (const value of [{ ...projection(), revision: -1 }, { ...projection(), revision: 1.5 }, { ...projection(), revision: Number.MAX_SAFE_INTEGER + 1 },
    { ...projection(), committed: 'zh-CN' }, { ...projection(), effective: ['en'] }, { readStatus: 'Failure', committed: 'en', effective: 'en', revision: 1 },
    { readStatus: 'Success', effective: 'en', revision: 1 }, { ...projection(), extra: true }, { readStatus: 'Success', committed: 'en', effective: 'en' }]) {
    assert.throws(() => readLauncherLanguage(value));
  }
});
await check('contract', 'Only canonical language commands reach native, with exact typed payload', async () => page(async ({ facade, bridge }) => {
  for (const requested of ['en', 'zh_CN']) {
    const pending = facade.commitLanguage(requested), command = bridge.sent.at(-1);
    assert.equal(command.type, 'CommitLanguage'); assert.deepEqual(command.payload, { language: requested });
    assert.equal(command.generation, generation); assert.match(command.requestId, /^[A-Za-z0-9_-]{1,128}$/);
    bridge.receive(response(command, result('COMMITTED', projection(requested, requested, 1)))); await pending;
  }
  const count = bridge.sent.length;
  for (const invalid of ['zh-CN', 'fr', '', null, ['en']]) await assert.rejects(facade.commitLanguage(invalid));
  assert.equal(bridge.sent.length, count);
}));
for (const [status, language, warnings] of [
  ['UNCHANGED', projection(), []], ['READ_FAILURE', failedRead(), []], ['SAVE_FAILURE', projection(), []],
  ['COMMITTED', projection('zh_CN', 'zh_CN', 1), []], ['COMMITTED', projection('zh_CN', 'en', 1), ['TranslationFallback']],
  ['COMMITTED', projection('zh_CN', 'zh_CN', 1), ['NotificationFailure']],
  ['COMMITTED', projection('zh_CN', 'en', 1), ['TranslationFallback', 'NotificationFailure']],
]) await check('contract', status + ' / ' + warnings.join(',') + ' is a capability result separate from transport', async () => page(async ({ facade, bridge }) => {
  const value = result(status, language, warnings), pending = facade.commitLanguage(status === 'UNCHANGED' ? 'en' : 'zh_CN');
  bridge.receive(response(bridge.sent.at(-1), value)); assert.deepEqual(await pending, value);
}));
await check('contract', 'Malformed union, warning, status and committed request mismatch reject', async () => {
  for (const invalid of [{ ...result('COMMITTED'), status: 'SUCCESS' }, { ...result('COMMITTED'), warnings: ['unknown'] },
    result('SAVE_FAILURE', projection(), ['NotificationFailure']), result('READ_FAILURE', projection()),
    result('COMMITTED', failedRead()), result('COMMITTED', projection(), ['NotificationFailure', 'NotificationFailure']),
    { ...result('COMMITTED'), extra: true }]) assert.throws(() => readLauncherLanguageResult(invalid));
  await page(async ({ facade, bridge }) => {
    const pending = facade.commitLanguage('zh_CN'), rejected = assert.rejects(pending, /match the request/);
    bridge.receive(response(bridge.sent.at(-1), result('COMMITTED', projection()))); await rejected;
  });
});
await check('contract', 'Transport rejection is never converted to preference failure or success', async () => page(async ({ facade, bridge }) => {
  const pending = facade.commitLanguage('zh_CN'), rejected = assert.rejects(pending, /native rejected/);
  const value = response(bridge.sent.at(-1), null, { ok: false, error: 'native rejected' }); delete value.payload;
  bridge.receive(value); await rejected;
}));
await check('contract', 'Wrong generation/request cannot complete commit; malformed events have no authority', async () => page(async ({ facade, bridge }) => {
  let settled = false, updates = 0; facade.subscribeLanguage(() => updates++);
  const pending = facade.commitLanguage('zh_CN').then(value => { settled = true; return value; }), command = bridge.sent.at(-1);
  bridge.receive(response(command, result('COMMITTED'), { generation: otherGeneration }));
  bridge.receive(response(command, result('COMMITTED'), { requestId: 'unrelated' }));
  for (const invalid of [event(projection(), { generation: otherGeneration }), event(projection(), { version: 2 }),
    event(projection(), { type: 'LanguageChanged' }), event({ ...projection(), effective: 'zh-CN' }), event(projection(), { extra: true })]) bridge.receive(invalid);
  await Promise.resolve(); assert.equal(settled, false); assert.equal(updates, 0);
  bridge.receive(response(command, result('COMMITTED'))); await pending;
}));

await check('settings', 'Open seeds committed, not fallback effective; draft change never retranslates; Cancel sends no command', async () => page(async ({ controller, state, bridge }) => {
  controller.openSettings(); assert.equal(state().draftLanguage, 'zh_CN'); assert.equal(state().bootstrap.language.effective, 'en');
  controller.draftLanguage('en'); assert.equal(state().draftLanguage, 'en'); assert.equal(state().bootstrap.language.committed, 'zh_CN');
  controller.cancelSettings(); assert.equal(state().settingsOpen, false); assert.equal(state().draftLanguage, null); assert.equal(bridge.sent.length, 1);
  controller.openSettings(); assert.equal(state().draftLanguage, 'zh_CN');
}, projection('zh_CN', 'en')));
await check('settings', 'Failed initial read never invents a saved language or submits an unset draft', async () => page(async ({ controller, state, bridge }) => {
  controller.openSettings(); assert.equal(state().draftLanguage, null); await controller.commitLanguage(false); assert.equal(bridge.sent.length, 1);
  controller.draftLanguage('zh_CN'); assert.equal(state().draftLanguage, 'zh_CN');
}, failedRead()));
for (const close of [false, true]) for (const status of ['UNCHANGED', 'COMMITTED', 'READ_FAILURE', 'SAVE_FAILURE']) {
  await check('settings', (close ? 'OK' : 'Apply') + ' / ' + status + ' applies correct close and authoritative rules', async () => page(async ({ controller, state, bridge }) => {
    controller.openSettings(); controller.draftLanguage(status === 'UNCHANGED' ? 'en' : 'zh_CN');
    const before = state().bootstrap.language, pending = controller.commitLanguage(close), command = bridge.sent.at(-1);
    assert.equal(state().languagePending, true); assert.deepEqual(state().bootstrap.language, before);
    await controller.commitLanguage(close); await controller.commitLanguage(!close); controller.cancelSettings();
    assert.equal(bridge.sent.length, 2); assert.equal(state().settingsOpen, true);
    const authoritative = status === 'READ_FAILURE' ? failedRead() : status === 'COMMITTED' ? projection('zh_CN', 'zh_CN', 1) : projection();
    bridge.receive(response(command, result(status, authoritative))); await pending;
    assert.equal(state().languagePending, false); assert.equal(state().settingsOpen, !(close && ['UNCHANGED', 'COMMITTED'].includes(status)));
    assert.deepEqual(state().bootstrap.language, authoritative);
    if (['READ_FAILURE', 'SAVE_FAILURE'].includes(status)) { assert.equal(state().draftLanguage, 'zh_CN'); assert.equal(state().bootstrap.language.effective, 'en'); }
  }));
}
for (const close of [false, true]) await check('settings', (close ? 'OK' : 'Apply') + ' preserves committed fallback/notification warnings and correct feedback location', async () => page(async ({ controller, state, bridge }) => {
  controller.openSettings(); controller.draftLanguage('zh_CN'); const pending = controller.commitLanguage(close);
  const value = result('COMMITTED', projection('zh_CN', 'en', 1), ['TranslationFallback', 'NotificationFailure']);
  bridge.receive(response(bridge.sent.at(-1), value)); await pending;
  assert.equal(state().bootstrap.language.committed, 'zh_CN'); assert.equal(state().bootstrap.language.effective, 'en');
  assert.equal(state().settingsOpen, !close); assert.deepEqual(close ? state().languageFeedback : state().settingsFeedback, value);
  const feedback = launcherLanguageFeedback(value, 'en'); assert(feedback.includes('Preference saved.')); assert(!feedback.includes('not committed'));
}));
await check('settings', 'Transport failure leaves dialog/draft and current authoritative facts intact', async () => page(async ({ controller, state, bridge }) => {
  controller.openSettings(); controller.draftLanguage('zh_CN'); const before = state().bootstrap.language, pending = controller.commitLanguage(true);
  bridge.receive(response(bridge.sent.at(-1), {})); await pending;
  assert.equal(state().settingsOpen, true); assert.equal(state().draftLanguage, 'zh_CN'); assert.equal(state().languagePending, false);
  assert.equal(state().settingsFeedback.status, 'TransportFailure'); assert.equal(state().bootstrap.language, before);
}));
await check('invalidation', 'External update while closed retranslates and next open seeds fresh committed; no frontend write', async () => page(async ({ controller, state, bridge }) => {
  bridge.receive(event(projection('zh_CN', 'zh_CN', 1))); assert.deepEqual(state().bootstrap.language, projection('zh_CN', 'zh_CN', 1));
  controller.openSettings(); assert.equal(state().draftLanguage, 'zh_CN'); assert.equal(bridge.sent.length, 1);
}));
await check('invalidation', 'External update while open preserves local draft and updates committed/effective independently', async () => page(async ({ controller, state, bridge }) => {
  controller.openSettings(); controller.draftLanguage('en'); bridge.receive(event(projection('zh_CN', 'zh_CN', 2)));
  assert.equal(state().draftLanguage, 'en'); assert.equal(state().bootstrap.language.effective, 'zh_CN');
  assert.equal(state().bootstrap.language.committed, 'zh_CN'); assert.equal(bridge.sent.length, 1);
}));
await check('invalidation', 'Newer external revision wins over late commit result and older event', async () => page(async ({ controller, state, bridge }) => {
  controller.openSettings(); controller.draftLanguage('zh_CN'); const pending = controller.commitLanguage(false), command = bridge.sent.at(-1);
  bridge.receive(event(projection('zh_CN', 'zh_CN', 1))); bridge.receive(event(projection('en', 'en', 2)));
  bridge.receive(response(command, result('COMMITTED', projection('zh_CN', 'zh_CN', 1)))); await pending;
  bridge.receive(event(projection('zh_CN', 'zh_CN', 1)));
  assert.deepEqual(state().bootstrap.language, projection('en', 'en', 2)); assert.equal(state().draftLanguage, 'zh_CN');
  assert.equal(state().settingsFeedback, null); assert.equal(state().languageFeedback, null);
}));
await check('invalidation', 'New external truth clears stale prior success feedback while preserving local draft; same revision is idempotent', async () => page(async ({ controller, state, bridge }) => {
  controller.openSettings(); controller.draftLanguage('zh_CN'); let pending = controller.commitLanguage(false);
  const committed = result('COMMITTED', projection('zh_CN', 'zh_CN', 1));
  bridge.receive(response(bridge.sent.at(-1), committed)); await pending;
  assert.deepEqual(state().settingsFeedback, committed);
  bridge.receive(event(projection('zh_CN', 'zh_CN', 1))); assert.deepEqual(state().settingsFeedback, committed);
  bridge.receive(event(projection('en', 'en', 2)));
  assert.equal(state().settingsFeedback, null); assert.equal(state().languageFeedback, null);
  assert.equal(state().draftLanguage, 'zh_CN'); assert.equal(state().settingsOpen, true);
  pending = controller.commitLanguage(true);
  bridge.receive(response(bridge.sent.at(-1), result('COMMITTED', projection('zh_CN', 'zh_CN', 3), ['NotificationFailure']))); await pending;
  assert.equal(state().languageFeedback.warnings[0], 'NotificationFailure');
  bridge.receive(event(projection('en', 'en', 4))); assert.equal(state().languageFeedback, null);
}));
await check('invalidation', 'Event received during bootstrap beats its older snapshot', async () => {
  const bridge = transport(), controller = new ProductionLauncherController(new ProductionLauncherFacade(bridge, generation));
  try {
    const boot = controller.bootstrap(), command = bridge.sent[0]; bridge.receive(event(projection('zh_CN', 'zh_CN', 4)));
    bridge.receive(response(command, snapshot(projection()))); await boot;
    assert.equal(controller.getState().bootstrap.language.effective, 'zh_CN'); assert.equal(controller.getState().bootstrap.language.revision, 4);
  } finally { controller.dispose(); }
});
await check('invalidation', 'Retired commit cannot update replacement page; new bootstrap reads saved truth', async () => page(async ({ controller, bridge, state }) => {
  controller.openSettings(); controller.draftLanguage('zh_CN'); const pending = controller.commitLanguage(true), old = bridge.sent.at(-1), before = state();
  controller.dispose(); await pending; assert.equal(state(), before);
  const replacement = new ProductionLauncherController(new ProductionLauncherFacade(bridge, otherGeneration));
  try {
    const boot = replacement.bootstrap(), command = bridge.sent.at(-1);
    bridge.receive(response(old, result('COMMITTED'))); bridge.receive(event(projection('zh_CN', 'zh_CN', 1)));
    assert.equal(replacement.getState().bootstrap, null);
    bridge.receive(response(command, snapshot(projection('zh_CN', 'zh_CN', 1)))); await boot;
    assert.equal(replacement.getState().bootstrap.language.committed, 'zh_CN'); assert.equal(replacement.getState().settingsOpen, false);
  } finally { replacement.dispose(); }
}));

const cache = new URL('../node_modules/.cache/launcher-language-a4/', import.meta.url); mkdirSync(cache, { recursive: true });
const { ProductionLauncherView, ProductionLauncherShell, LauncherEntryFailure } = await import(launcherRenderModule(new URL('../src/ProductionLauncherShell.tsx', import.meta.url), cache));
const { ProductionLauncherSettings } = await import(launcherRenderModule(new URL('../src/ProductionLauncherSettings.tsx', import.meta.url), cache));
const render = (language, extras = {}) => renderToStaticMarkup(React.createElement(ProductionLauncherView, {
  snapshot: snapshot(language), catalog: snapshot(language).displayEnumeration, selectedId: row.id,
  onSelect() {}, onRefresh() {}, onSettings() {}, ...extras }));
await check('coverage', 'Launcher dictionaries have exact nonempty EN/ZH key coverage and distinct translated copy', () => {
  const english = launcherCopy('en'), chinese = launcherCopy('zh_CN'); assert.deepEqual(Object.keys(english).sort(), Object.keys(chinese).sort());
  for (const key of Object.keys(english)) {
    assert(english[key].trim()); assert(chinese[key].trim());
    if (key !== 'ok') assert.notEqual(english[key], chinese[key], key);
  }
});
for (const language of ['en', 'zh_CN']) await check('coverage', language + ' shell metadata, a11y, catalog/Identify errors, Settings and warnings use effective dictionary', () => {
  const copy = launcherCopy(language), html = render(projection(language));
  for (const value of [copy.displays, copy.refresh, copy.settings, copy.primary, copy.sessionUnknown, copy.noRegisteredSession, copy.create]) assert(html.includes(value), value);
  assert(html.includes(row.friendlyName)); assert(html.includes('1920'));
  assert(!html.includes(row.sourceDeviceName)); assert(!html.includes('(2560, -660)'));
  assert.doesNotMatch(html, /production-(?:language|source|identity|readonly)(?:["\s])/);
  assert(html.includes('lang="' + launcherLocale(language) + '"'));
  for (const [status, key] of [['ApiFailure', 'apiFailure'], ['IncompletePathMetadata', 'metadataFailure'], ['DuplicateIdentity', 'duplicateFailure']]) {
    const failure = render(projection(language), { catalog: { status, displays: [] }, selectedId: null }); assert(failure.includes(copy.enumerateFailed)); assert(failure.includes(copy[key]));
  }
  assert(render(projection(language), { catalog: { status: 'Success', displays: [] }, selectedId: null }).includes(copy.noDisplays));
  const transportFailure = render(projection(language), { catalog: null, selectedId: null, refreshError: 'Technical English diagnostic' });
  assert(transportFailure.includes(copy.refreshInvalid)); assert(!transportFailure.includes('Technical English diagnostic'));
  for (const [status, key] of [['Unresolved', 'identifyUnresolved'], ['InvalidRequest', 'identifyInvalid'], ['ApiFailure', 'identifyFailure'], ['TransportFailure', 'identifyTransport']]) {
    assert(render(projection(language), { identifyFeedback: { displayId: row.id, status } }).includes(copy[key]));
  }
  const settings = renderToStaticMarkup(React.createElement(ProductionLauncherSettings, { language: projection(language), draft: language, pending: false,
    feedback: null, onDraft() {}, onCancel() {}, onCommit() {} }));
  assert.match(settings, new RegExp('<dialog[^>]*lang="' + launcherLocale(language) + '"'));
  for (const value of [copy.settings, copy.language, copy.effective, copy.committed, copy.cancel, copy.apply, copy.ok, copy.changesOnApply]) assert(settings.includes(value), value);
  for (const feedback of [result('READ_FAILURE', failedRead(language)), result('SAVE_FAILURE', projection(language)),
    result('COMMITTED', projection('zh_CN', 'en'), ['TranslationFallback', 'NotificationFailure']), { status: 'TransportFailure' }]) {
    assert(launcherLanguageFeedback(feedback, language).length > 0);
  }
  if (language === 'zh_CN') for (const leakage of ['REGISTRATION UNKNOWN', 'Primary', 'Display navigation', 'Source identity', 'Effective language', 'Settings']) assert(!html.includes(leakage), leakage);
});
await check('coverage', 'Reloaded committed/effective mismatch visibly explains fallback without changing committed selection', () => {
  const html = render(projection('zh_CN', 'en', 3)); assert(html.includes(launcherCopy('en').translationFallback));
  assert(html.includes('data-committed-language="zh_CN"')); assert(html.includes('data-language="en"'));
  const settings = renderToStaticMarkup(React.createElement(ProductionLauncherSettings, { language: projection('zh_CN', 'en', 3), draft: 'zh_CN',
    pending: false, feedback: null, onDraft() {}, onCancel() {}, onCommit() {} }));
  assert.match(settings, /<dialog[^>]*lang="en"/);
  assert(settings.includes(launcherCopy('en').translationFallback)); assert(settings.includes('<option value="zh_CN" selected="">'));
});
await check('coverage', 'Prebootstrap loading and host-entry failures use strict native locale hint without authority or persistence', () => {
  const previousWindow = globalThis.window;
  try {
    for (const [hint, effective] of [['en', 'en'], ['zh_CN', 'zh_CN'], ['zh-CN', 'en'], ['fr', 'en']]) {
      globalThis.window = { location: { search: '?mode=production&generation=' + generation + '&language=' + hint } };
      const copy = launcherCopy(effective);
      const loading = renderToStaticMarkup(React.createElement(ProductionLauncherShell));
      const missing = renderToStaticMarkup(React.createElement(LauncherEntryFailure));
      assert(loading.includes(copy.connecting)); assert(loading.includes('data-bootstrap-state="loading"'));
      assert(!loading.includes('production-readonly'));
      assert(missing.includes(copy.hostRequired)); assert(missing.includes(copy.hostExplanation));
    }
  } finally { if (previousWindow === undefined) delete globalThis.window; else globalThis.window = previousWindow; }
});
await check('coverage', 'Production source owns no mock timer, browser persistence, or prototype language path', () => {
  for (const name of ['ProductionLauncherController.ts', 'ProductionLauncherShell.tsx', 'ProductionLauncherSettings.tsx', 'launcherLanguage.ts']) {
    const source = readFileSync(new URL('../src/' + name, import.meta.url), 'utf8');
    for (const forbidden of ['localStorage', 'sessionStorage', 'setTimeout(', "'./LauncherSettings.tsx'", 'SessionLanguageProvider', 'useSessionText']) assert(!source.includes(forbidden), name + ':' + forbidden);
  }
});
const groups = Object.fromEntries(['contract', 'settings', 'invalidation', 'coverage'].map(group => {
  const matching = checks.filter(item => item.group === group); return [group, { passed: matching.filter(item => item.pass).length, total: matching.length }];
}));
const output = { pass: checks.every(item => item.pass), red: 'commitLanguage was undefined on production facade before implementation', groups, checks };
writeFileSync(new URL('focused-checks.json', cache), JSON.stringify(output, null, 2));
console.log(JSON.stringify({ pass: output.pass, groups, checks: checks.length, failed: checks.filter(item => !item.pass) }, null, 2));
assert(output.pass, 'Production Launcher A4 language checks must pass');
