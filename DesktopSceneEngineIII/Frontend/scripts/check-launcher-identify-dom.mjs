import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

// Test-only injected transport for mounted production React. No production
// fixture flag or DOM overlay exists. Real native overlay evidence is separate.
const origin = 'http://127.0.0.1:5173', debuggerOrigin = 'http://127.0.0.1:9393';
const cache = new URL('../node_modules/.cache/launcher-identify-a3/dom/', import.meta.url); mkdirSync(cache, { recursive: true });
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const rows = [
  { id: 'opaque A target', identityKind: 'TargetDevicePath', persistentOwnerCapable: true, friendlyName: 'QA Panel A', sourceDeviceName: 'QA_SOURCE_A', desktopX: -1920, desktopY: 0, pixelWidth: 1920, pixelHeight: 1080, primary: false, displayNumber: 1 },
  { id: 'opaque B source', identityKind: 'SourceDeviceName', persistentOwnerCapable: false, friendlyName: 'QA Panel B', sourceDeviceName: 'QA_SOURCE_B', desktopX: 0, desktopY: 0, pixelWidth: 2560, pixelHeight: 1440, primary: true, displayNumber: 2 },
];
const snapshot = { coordinatorEpoch: 'dba10904-5b14-43f0-995b-17bc99181d43', sessionRevision: 0, displayEnumeration: { status: 'Success', displays: rows },
  registeredSessions: [{ boundDisplayId: rows[0].id, incarnation: 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2', controlConnected: true }],
  registeredSessionsScope: 'verified-registered-only', createOperations: [], language: { readStatus: 'Success', committed: 'en', effective: 'en', revision: 0 },
  host: { ready: true, launcherRoleReady: true } };
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
await assert.rejects(fetch(debuggerOrigin + '/json/list'), 'QA debugging port must be unused before this test owns it');
const chrome = spawn(chromeExecutable, [
  '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--disable-background-networking',
  '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=9393',
  '--user-data-dir=' + fileURLToPath(new URL('profile/', cache)), 'about:blank',
], { windowsHide: true, stdio: 'ignore' });
const checks = [], errors = [], warnings = []; let socket;
try {
  let pages;
  for (let attempt = 0; attempt < 80; attempt++) {
    try { pages = await (await fetch(debuggerOrigin + '/json/list')).json(); if (pages.some(page => page.type === 'page')) break; } catch { /* startup */ }
    await pause(100);
  }
  const page = pages?.find(page => page.type === 'page'); assert(page, 'Isolated QA Chrome started');
  socket = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
  const pending = new Map(); let serial = 0;
  socket.addEventListener('message', async event => {
    const message = JSON.parse(typeof event.data === 'string' ? event.data : await event.data.text());
    if (message.method === 'Runtime.exceptionThrown') errors.push(message.params.exceptionDetails);
    if (message.method === 'Runtime.consoleAPICalled' && ['warning', 'error'].includes(message.params.type)) (message.params.type === 'error' ? errors : warnings).push(message.params);
    if (message.method === 'Log.entryAdded' && ['warning', 'error'].includes(message.params.entry.level)) (message.params.entry.level === 'error' ? errors : warnings).push(message.params.entry);
    const request = pending.get(message.id); if (request) { pending.delete(message.id); message.error ? request.reject(message.error) : request.resolve(message.result); }
  });
  function send(method, params = {}) { return new Promise((resolve, reject) => { const id = ++serial; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); }); }
  async function evaluate(expression) {
    const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails)); return result.result.value;
  }
  async function wait(expression) { for (let i = 0; i < 80; i++) { if (await evaluate(expression)) return; await pause(50); } assert.fail(expression); }
  async function rest() {
    await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
    await evaluate('Promise.allSettled(document.getAnimations().map(animation => animation.finished))');
  }
  async function click(selector) { await evaluate('document.querySelector(' + JSON.stringify(selector) + ').click()'); await rest(); }
  async function reply(status, index = -1) {
    await evaluate('window.a3QaBridge.reply(' + JSON.stringify(status) + ',' + index + ')');
    await wait('document.querySelector(".production-launcher")?.dataset.identifyState === "idle"'); await rest();
  }
  async function key(key, code, number) {
    await send('Input.dispatchKeyEvent', { type: 'rawKeyDown', key, code, windowsVirtualKeyCode: number, nativeVirtualKeyCode: number });
    await send('Input.dispatchKeyEvent', { type: 'char', key, code, text: key === 'Enter' ? '\r' : key, unmodifiedText: key === 'Enter' ? '\r' : key, windowsVirtualKeyCode: number, nativeVirtualKeyCode: number });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', key, code, windowsVirtualKeyCode: number, nativeVirtualKeyCode: number }); await rest();
  }
  async function facts() {
    return evaluate(`(() => { const root=document.querySelector('.production-launcher'), card=document.querySelector('[data-selected="true"]'), name=document.querySelector('[data-identify-display]');
      const a=card?.getBoundingClientRect(), b=name?.getBoundingClientRect(); return {selected:root?.dataset.selectedDisplayId,
      target:name?.dataset.identifyTarget, pending:root?.dataset.identifyState, result:root?.dataset.identifyResult,
      registered:document.querySelector('.detail-heading .display-status')?.textContent,
      context:document.querySelector('.detail-context .session-context')?.textContent,
      actionDisabled:document.querySelector('.detail-context [data-session-action]')?.disabled,
      name:name?.textContent, nameCount:document.querySelectorAll('[data-identify-display]').length, nested:Boolean(name?.closest('.display-surface')),
      nameInsideCard:Boolean(a&&b&&b.x>=a.x&&b.right<=a.right&&b.y>=a.y&&b.bottom<=a.bottom),
      deckCenter:a? a.x+a.width/2:null, text:document.querySelector('.launcher-operation')?.textContent,
      commands:window.a3QaBridge.sent.filter(x=>x.type==='IdentifyDisplay').map(x=>x.payload)}; })()`);
  }
  async function check(name, run) { try { await run(); checks.push({ name, pass: true }); } catch (error) { checks.push({ name, pass: false, error: String(error) }); } }
  await Promise.all([send('Runtime.enable'), send('Log.enable'), send('Page.enable'), send('Network.enable')]);
  await send('Network.setCacheDisabled', { cacheDisabled: true });
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
    const listeners = new Set(), sent = [], initial = ${JSON.stringify(snapshot)};
    const receive = (command,payload) => listeners.forEach(listener=>listener({data:{kind:'result',version:1,requestId:command.requestId,type:command.type,generation:command.generation,ok:true,payload}}));
    window.chrome ??= {}; window.chrome.webview={postMessage(command){sent.push(command);if(command.type==='GetLauncherBootstrapSnapshot')queueMicrotask(()=>receive(command,initial));},
      addEventListener(_type,listener){listeners.add(listener);},removeEventListener(_type,listener){listeners.delete(listener);}};
    window.a3QaBridge={sent,reply(status,index=-1){const all=sent.filter(x=>x.type==='IdentifyDisplay'),command=all.at(index);receive(command,{displayId:command.payload.displayId,status});}};
  })();` });
  await send('Page.navigate', { url: origin + '/?mode=production&generation=' + generation });
  await wait('document.querySelector(".production-launcher")?.dataset.bootstrapState === "ready"'); await rest();
  await check('Selected display name is a single visible sibling button over unchanged card geometry', async () => {
    const value = await facts(); assert.equal(value.nameCount, 1); assert.equal(value.nested, false); assert.equal(value.nameInsideCard, true);
    assert.equal(value.target, rows[0].id); assert.equal(value.selected, rows[0].id);
    assert.equal(await evaluate('getComputedStyle(document.querySelector("[data-identify-display]")).cursor'), 'pointer');
  });
  await check('Pointer action sends exact selected opaque ID and disables duplicate activation until result', async () => {
    await click('[data-identify-display]'); await click('[data-identify-display]'); const value = await facts();
    assert.deepEqual(value.commands, [{ displayId: rows[0].id }]); assert.equal(value.pending, 'pending');
    assert.equal(await evaluate('document.querySelector("[data-identify-display]").getAttribute("aria-disabled")'), 'true'); await reply('Shown');
  });
  await check('Enter activates selected display name once without changing selection or registered facts', async () => {
    const before = await facts(); await evaluate('document.querySelector("[data-identify-display]").focus()'); await key('Enter', 'Enter', 13);
    const pending = await facts(); assert.equal(pending.commands.length, before.commands.length + 1); await reply('Shown');
    const after = await facts(); assert.equal(after.selected, before.selected); assert.equal(after.registered, before.registered); assert.equal(after.deckCenter, before.deckCenter);
  });
  await check('Space activates native button and keeps focus on selected name', async () => {
    const before = await facts(); await evaluate('document.querySelector("[data-identify-display]").focus()'); await key(' ', 'Space', 32);
    assert.equal((await facts()).commands.length, before.commands.length + 1); await reply('Shown');
    assert.equal(await evaluate('document.activeElement?.hasAttribute("data-identify-display")'), true);
  });
  await check('Weak display name remains enabled and Shown never changes owner capability or selection', async () => {
    await click('[aria-label="Next display"]'); const before = await facts(); assert.equal(before.selected, rows[1].id);
    await click('[data-identify-display]'); await reply('Shown'); const after = await facts();
    assert.equal(after.selected, rows[1].id); assert.equal(after.context, before.context);
    assert.equal(after.context, 'Workspaces can’t be opened on this display right now.');
    assert.equal(before.actionDisabled, true); assert.equal(after.actionDisabled, true);
    assert.deepEqual(after.commands.at(-1), { displayId: rows[1].id }); assert.equal(after.result, 'Shown');
  });
  for (const [status, text] of [['Unresolved', 'This display cannot currently be located.'], ['InvalidRequest', 'The Identify request is invalid.'], ['ApiFailure', 'Unable to show the Identify marker.']]) {
    await check(status + ' displays distinct feedback with catalog/selection/registration unchanged', async () => {
      const before = await facts(); await click('[data-identify-display]'); await reply(status); const after = await facts();
      assert.equal(after.text, text); assert.equal(after.selected, before.selected); assert.equal(after.registered, before.registered); assert.equal(after.context, before.context);
    });
  }
  await check('Selection changes during request do not put old-target failure on newly selected name', async () => {
    await click('[data-identify-display]'); await click('[aria-label="Next display"]'); await reply('Unresolved'); const value = await facts();
    assert.equal(value.selected, rows[0].id); assert.equal(value.target, rows[0].id); assert.equal(value.result, '');
    assert(!value.text.includes('cannot currently be located')); assert.deepEqual(value.commands.at(-1), { displayId: rows[1].id });
  });
  await check('No DOM Identify overlay or prohibited Session actions are mounted', async () => {
    assert.equal(await evaluate('document.querySelector(".identify-overlay,[data-identify-overlay],[data-session-create],[data-session-open]") !== null'), false);
    assert.equal(await evaluate('document.body.textContent.includes("DEV STATE")'), false);
  });
  await check('Browser runtime errors and console warnings remain zero', async () => { assert.deepEqual(errors, []); assert.deepEqual(warnings, []); });
  const output = { pass: checks.every(item => item.pass), scope: 'Isolated injected transport mounted React QA, not native overlay evidence', checks, errors, warnings };
  writeFileSync(new URL('focused-checks.json', cache), JSON.stringify(output, null, 2));
  console.log(JSON.stringify({ pass: output.pass, checks: checks.length, failed: checks.filter(item => !item.pass), errors: errors.length, warnings: warnings.length }, null, 2));
  assert(output.pass, 'Mounted Launcher Identify checks must pass');
} finally { socket?.close(); chrome.kill(); }
