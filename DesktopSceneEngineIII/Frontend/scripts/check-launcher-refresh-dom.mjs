import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

// Isolated mounted-React QA only. Fixtures are injected by CDP before this test
// document loads; there is no source/runtime fixture switch in production.
const origin = 'http://127.0.0.1:5173';
const debuggerOrigin = 'http://127.0.0.1:9393';
const cache = new URL('../node_modules/.cache/launcher-refresh-a2/dom/', import.meta.url);
mkdirSync(cache, { recursive: true });
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const rows = [
  { id: 'opaque A target', identityKind: 'TargetDevicePath', persistentOwnerCapable: true, friendlyName: 'QA Panel A', sourceDeviceName: 'QA_SOURCE_A', desktopX: -1920, desktopY: 0, pixelWidth: 1920, pixelHeight: 1080, primary: false, displayNumber: 1 },
  { id: 'opaque B source', identityKind: 'SourceDeviceName', persistentOwnerCapable: false, friendlyName: 'QA Panel B', sourceDeviceName: 'QA_SOURCE_B', desktopX: 0, desktopY: 0, pixelWidth: 2560, pixelHeight: 1440, primary: true, displayNumber: 2 },
  { id: 'opaque C target', identityKind: 'TargetDevicePath', persistentOwnerCapable: true, friendlyName: 'QA Panel C', sourceDeviceName: 'QA_SOURCE_C', desktopX: 2560, desktopY: 0, pixelWidth: 1080, pixelHeight: 1920, primary: false, displayNumber: 3 },
];
const catalog = (displays = rows, status = 'Success') => ({ status, displays });
const snapshot = { coordinatorEpoch: 'dba10904-5b14-43f0-995b-17bc99181d43', sessionRevision: 0, displayEnumeration: catalog(),
  registeredSessions: [{ boundDisplayId: rows[0].id, incarnation: 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2', controlConnected: true }],
  registeredSessionsScope: 'verified-registered-only', createOperations: [], language: { readStatus: 'Success', committed: 'en', effective: 'en', revision: 0 },
  host: { ready: true, launcherRoleReady: true } };
const sourceUrl = origin + '/src/ProductionLauncherShell.tsx';
const originalSource = await (await fetch(sourceUrl)).text();
const previousExpression = 'return count <= 1 ? 0 : visualPosition + selectedIndex - modulo(visualPosition, count);';
assert(originalSource.includes(previousExpression), 'Counterfactual replaces only the single-card position repair in served JS');
const counterfactualSource = originalSource.replace(previousExpression, 'return visualPosition + selectedIndex - modulo(visualPosition, count);');
await assert.rejects(fetch(debuggerOrigin + '/json/list'), 'The QA debugging port must be unused before this test owns it');
const chrome = spawn(chromeExecutable, [
  '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--disable-background-networking',
  '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=9393',
  '--user-data-dir=' + fileURLToPath(new URL('profile/', cache)), 'about:blank',
], { windowsHide: true, stdio: 'ignore' });
let socket;
const checks = [];
const errors = [];
const warnings = [];
let redEvidence;
try {
  let pages;
  for (let attempt = 0; attempt < 80; attempt++) {
    try { pages = await (await fetch(debuggerOrigin + '/json/list')).json(); if (pages.some(page => page.type === 'page')) break; }
    catch { /* Chrome is still starting. */ }
    await pause(100);
  }
  const page = pages?.find(page => page.type === 'page'); assert(page, 'Isolated QA Chrome started');
  socket = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
  let serial = 0;
  const pending = new Map();
  let useCounterfactual = true;
  socket.addEventListener('message', async event => {
    const message = JSON.parse(typeof event.data === 'string' ? event.data : await event.data.text());
    if (message.method === 'Runtime.exceptionThrown') errors.push(message.params.exceptionDetails);
    if (message.method === 'Runtime.consoleAPICalled' && ['warning', 'error'].includes(message.params.type)) {
      (message.params.type === 'error' ? errors : warnings).push(message.params);
    }
    if (message.method === 'Log.entryAdded' && ['warning', 'error'].includes(message.params.entry.level)) {
      (message.params.entry.level === 'error' ? errors : warnings).push(message.params.entry);
    }
    if (message.method === 'Fetch.requestPaused') {
      const { requestId } = message.params;
      if (useCounterfactual && message.params.request.url.startsWith(sourceUrl)) {
        await send('Fetch.fulfillRequest', { requestId, responseCode: 200,
          responseHeaders: [{ name: 'Content-Type', value: 'text/javascript' }], body: Buffer.from(counterfactualSource).toString('base64') });
      } else await send('Fetch.continueRequest', { requestId });
    }
    const request = pending.get(message.id);
    if (request) { pending.delete(message.id); message.error ? request.reject(message.error) : request.resolve(message.result); }
  });
  function send(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = ++serial; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params }));
    });
  }
  async function evaluate(expression) {
    const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails)); return result.result.value;
  }
  async function wait(expression) {
    for (let attempt = 0; attempt < 80; attempt++) { if (await evaluate(expression)) return; await pause(50); }
    assert.fail('Mounted page condition did not become true: ' + expression);
  }
  async function rest() {
    await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
    await evaluate('Promise.allSettled(document.getAnimations().map(animation => animation.finished))');
  }
  async function click(selector) { await evaluate('document.querySelector(' + JSON.stringify(selector) + ').click()'); await rest(); }
  async function refresh(value, focusCard = false) {
    await click('[data-refresh-displays]');
    if (focusCard) await evaluate('document.querySelector(\'[data-selected="true"]\').focus()');
    await evaluate('window.a2QaBridge.reply(' + JSON.stringify(value) + ')');
    await wait('document.querySelector(\'.production-launcher\')?.dataset.refreshState !== "pending"'); await rest();
  }
  async function geometry() {
    return evaluate(`(() => {
      const root = document.querySelector('.production-launcher');
      const card = document.querySelector('[data-selected="true"]');
      const view = document.querySelector('.deck-viewport');
      const a = card?.getBoundingClientRect(), b = view?.getBoundingClientRect();
      return { selectedId: root?.dataset.selectedDisplayId, count: document.querySelectorAll('[data-selected="true"]').length,
        position: document.querySelector('.display-deck')?.dataset.position,
        centered: Boolean(a && b && Math.abs(a.x + a.width / 2 - b.x - b.width / 2) < 1),
        selectedButtonId: card?.dataset.displayId, focusRefresh: document.activeElement?.hasAttribute('data-refresh-displays'),
        focusedId: document.activeElement?.dataset.displayId, detail: document.querySelector('#detail-title')?.textContent,
        displayCount: document.querySelector('.display-deck')?.dataset.displayCount,
        overflow: document.documentElement.scrollWidth > innerWidth + 1 };
    })()`);
  }
  async function check(name, run) {
    try { await run(); checks.push({ name, pass: true }); }
    catch (error) { checks.push({ name, pass: false, error: String(error) }); }
  }
  function assertCard(value, id, count) {
    assert.equal(value.selectedId, id); assert.equal(value.selectedButtonId, id); assert.equal(value.count, 1);
    assert.equal(value.displayCount, String(count)); assert.equal(value.centered, true); assert.equal(value.overflow, false);
  }
  await Promise.all([send('Runtime.enable'), send('Log.enable'), send('Page.enable'), send('Network.enable')]);
  await send('Network.setCacheDisabled', { cacheDisabled: true });
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await send('Fetch.enable', { patterns: [{ urlPattern: sourceUrl + '*', requestStage: 'Request' }] });
  await send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
    const listeners = new Set(); const sent = []; const initial = ${JSON.stringify(snapshot)};
    const receive = command => payload => listeners.forEach(listener => listener({ data: {
      kind: 'result', version: 1, requestId: command.requestId, type: command.type,
      generation: command.generation, ok: true, payload } }));
    window.chrome ??= {}; window.chrome.webview = {
      postMessage(command) { sent.push(command); if (command.type === 'GetLauncherBootstrapSnapshot') queueMicrotask(() => receive(command)(initial)); },
      addEventListener(_type, listener) { listeners.add(listener); },
      removeEventListener(_type, listener) { listeners.delete(listener); }
    };
    window.a2QaBridge = { sent, reply(payload) { receive(sent.at(-1))(payload); } };
  })();` });
  async function load() {
    await send('Page.navigate', { url: origin + '/?mode=production&generation=' + generation });
    await wait('document.querySelector(\'.production-launcher\')?.dataset.bootstrapState === "ready"'); await rest();
  }
  await load();
  for (let index = 0; index < 5; index++) await click('[aria-label="Next display"]');
  await refresh(catalog([rows[2]]));
  redEvidence = await geometry();
  assert.equal(redEvidence.selectedId, rows[2].id);
  assert.equal(redEvidence.count, 0, 'Counterfactual original position bug removes selected card after many→one');
  assert.equal(redEvidence.centered, false);
  useCounterfactual = false;
  await load();

  await check('Initial production page selects and centers first authoritative row', async () => assertCard(await geometry(), rows[0].id, 3));
  await check('Many→single keeps surviving exact ID and restores selected card to center at position zero', async () => {
    for (let index = 0; index < 5; index++) await click('[aria-label="Next display"]');
    await refresh(catalog([rows[2]])); const value = await geometry(); assertCard(value, rows[2].id, 1); assert.equal(value.position, '0');
  });
  await check('Single→many keeps selection and follows new exact-ID array position', async () => {
    await refresh(catalog([rows[1], rows[0], rows[2]])); assertCard(await geometry(), rows[2].id, 3);
  });
  await check('Metadata/reorder preserves exact ID and updates selected detail and geometry', async () => {
    const changed = { ...rows[2], friendlyName: 'QA C metadata changed', pixelWidth: 1920, pixelHeight: 1200, primary: true, displayNumber: 19 };
    await refresh(catalog([changed, rows[0], rows[1]])); const value = await geometry(); assertCard(value, rows[2].id, 3);
    assert.equal(value.detail, changed.friendlyName);
  });
  await check('Removed focused selected card chooses first remaining row and repairs focus to Refresh', async () => {
    await refresh(catalog([rows[0]]), true); const value = await geometry(); assertCard(value, rows[0].id, 1);
    assert.equal(value.focusRefresh, true);
  });
  await check('Weak row remains visible/selectable without persistent-owner promotion', async () => {
    await refresh(catalog()); await click('[aria-label="Next display"]');
    assertCard(await geometry(), rows[1].id, 3);
    assert(await evaluate('document.body.textContent.includes("Workspaces can’t be opened on this display right now.")'));
    assert.equal(await evaluate('document.querySelector(".detail-context [data-session-action]").disabled'), true);
    assert.equal(await evaluate('document.querySelector(".production-source,.production-identity,.production-language,.production-readonly") !== null'), false);
    assert.equal(await evaluate('document.querySelector("[data-identify-display]")?.disabled'), false);
  });
  await check('Keyboard arrow follows opaque selection and roving focus after refresh', async () => {
    await evaluate(`(() => { const card = document.querySelector('[data-selected="true"]'); card.focus(); card.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true })); })()`);
    await rest(); const value = await geometry(); assertCard(value, rows[2].id, 3); assert.equal(value.focusedId, rows[2].id);
  });
  await check('Enumeration failure removes stale deck, clears selection and offers retry', async () => {
    await refresh(catalog([], 'IncompletePathMetadata'), true);
    assert.equal(await evaluate('document.querySelector(".display-deck") !== null'), false);
    assert.equal(await evaluate('document.querySelector(".production-launcher").dataset.selectedDisplayId'), '');
    assert.equal(await evaluate('document.querySelector(".production-launcher").dataset.enumerationStatus'), 'IncompletePathMetadata');
    assert(await evaluate('document.body.textContent.includes("Windows hasn’t finished identifying the displays.")'));
    assert(await evaluate('document.querySelector("[data-refresh-retry]") !== null'));
    assert.equal((await geometry()).focusRefresh, true);
    await refresh(catalog()); assertCard(await geometry(), rows[0].id, 3);
  });
  await check('Success empty differs from failure and Refresh recovers current catalog', async () => {
    await refresh(catalog([]));
    assert(await evaluate('document.body.textContent.includes("No displays found")'));
    assert.equal(await evaluate('document.body.textContent.includes("Couldn’t find displays")'), false);
    await refresh(catalog()); assertCard(await geometry(), rows[0].id, 3);
  });
  await check('Browser runtime errors and console warnings remain zero', async () => {
    assert.deepEqual(errors, []); assert.deepEqual(warnings, []);
  });
  const output = { pass: checks.every(check => check.pass), scope: 'Isolated CDP fixture QA, not real-OS or human verification',
    redCounterfactual: { selectedCardCount: redEvidence.count, centered: redEvidence.centered }, checks, errors, warnings };
  writeFileSync(new URL('focused-checks.json', cache), JSON.stringify(output, null, 2));
  console.log(JSON.stringify({ pass: output.pass, checks: checks.length, failed: checks.filter(check => !check.pass),
    redCounterfactual: output.redCounterfactual, errors: errors.length, warnings: warnings.length }, null, 2));
  assert(output.pass, 'Mounted production Launcher A2 checks must pass');
} finally {
  socket?.close();
  chrome.kill();
}
