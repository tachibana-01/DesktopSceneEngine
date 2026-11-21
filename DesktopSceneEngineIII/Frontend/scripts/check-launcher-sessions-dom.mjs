import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

// Mounted production React test. The fixture transport is injected only into
// this owned test document; production has no fixture, command or timer switch.
const origin = 'http://127.0.0.1:5173', debuggerOrigin = 'http://127.0.0.1:9393';
const cache = new URL('../node_modules/.cache/launcher-sessions-a5/dom/', import.meta.url); mkdirSync(cache, { recursive: true });
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const epoch = 'dba10904-5b14-43f0-995b-17bc99181d43', otherEpoch = 'd7951708-aa40-497e-9160-37a96e1063e3';
const incarnation = 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2', replacement = 'b902bc98-e64a-43e0-ae66-16e257e5cd5b';
const rows = ['A', 'B'].map((name, index) => ({ id: 'exact opaque ' + name, identityKind: 'TargetDevicePath', persistentOwnerCapable: true,
  friendlyName: 'QA Panel ' + name, sourceDeviceName: 'QA_SOURCE_' + name, desktopX: index * 1920, desktopY: 0,
  pixelWidth: 1920, pixelHeight: 1080, primary: index === 0, displayNumber: index + 1 }));
const catalog = (displays = rows, status = 'Success') => ({ status, displays });
const role = (patch = {}) => ({ boundDisplayId: rows[0].id, incarnation, controlConnected: true, ...patch });
const sessions = (revision, roles = [role()], owner = epoch) => ({ coordinatorEpoch: owner, sessionRevision: revision,
  registeredSessionsScope: 'verified-registered-only', createOperations: [], registeredSessions: roles });
const snapshot = (projection = sessions(0, [])) => ({ ...projection, displayEnumeration: catalog(),
  language: { readStatus: 'Success', committed: 'en', effective: 'en', revision: 0 }, host: { ready: true, launcherRoleReady: true } });
await assert.rejects(fetch(debuggerOrigin + '/json/list'), 'Owned QA port must be free');
const chrome = spawn(chromeExecutable, ['--headless=new', '--disable-gpu', '--no-first-run',
  '--no-default-browser-check', '--disable-background-networking', '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=9393',
  '--user-data-dir=' + fileURLToPath(new URL('profile/', cache)), 'about:blank'], { windowsHide: true, stdio: 'ignore' });
let socket; const checks = [], errors = [], warnings = [];
try {
  let pages;
  for (let n = 0; n < 80; n++) { try { pages = await (await fetch(debuggerOrigin + '/json/list')).json(); if (pages.some(page => page.type === 'page')) break; } catch {} await pause(100); }
  const page = pages?.find(page => page.type === 'page'); assert(page, 'Owned Chrome started'); socket = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
  let serial = 0; const pending = new Map();
  socket.addEventListener('message', async event => {
    const message = JSON.parse(typeof event.data === 'string' ? event.data : await event.data.text());
    if (message.method === 'Runtime.exceptionThrown') errors.push(message.params.exceptionDetails);
    if (message.method === 'Runtime.consoleAPICalled' && ['warning', 'error'].includes(message.params.type)) (message.params.type === 'error' ? errors : warnings).push(message.params);
    if (message.method === 'Log.entryAdded' && ['warning', 'error'].includes(message.params.entry.level)) (message.params.entry.level === 'error' ? errors : warnings).push(message.params.entry);
    const request = pending.get(message.id); if (request) { pending.delete(message.id); message.error ? request.reject(message.error) : request.resolve(message.result); }
  });
  function send(method, params = {}) { return new Promise((resolve, reject) => { const id = ++serial; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); }); }
  async function evaluate(expression) { const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }); assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails)); return result.result.value; }
  async function wait(expression) { for (let n = 0; n < 80; n++) { if (await evaluate(expression)) return; await pause(50); } assert.fail('Mounted condition timed out: ' + expression + ' ' + JSON.stringify({ body: await evaluate('document.body.innerText'), errors, warnings })); }
  async function rest() { await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))'); }
  async function check(name, run) { try { await run(); checks.push({ name, pass: true }); } catch (error) { checks.push({ name, pass: false, error: String(error) }); } }
  async function emit(projection, patch = {}) { await evaluate('window.a5Qa.emit(' + JSON.stringify(projection) + ',' + JSON.stringify(patch) + ')'); await rest(); }
  async function refresh(value) { await evaluate('window.a5Qa.catalog=' + JSON.stringify(value)); await evaluate('document.querySelector("[data-refresh-displays]").click()'); await wait('document.querySelector(".production-launcher")?.dataset.refreshState !== "pending"'); await rest(); }
  async function facts(selector = '.detail-context') { return evaluate(`(() => { const node=document.querySelector(${JSON.stringify(selector)}); return { incarnation:node?.dataset.sessionIncarnation ?? null, bound:node?.dataset.boundDisplayId ?? null,
    status:node?.querySelector('[data-session-status]')?.dataset.sessionStatus ?? null, control:node?.dataset.sessionControlConnected ?? null, close:node?.dataset.sessionCloseResult ?? null,
    statusLabel:node?.querySelector('[data-session-status]')?.textContent ?? null, actionLabel:node?.querySelector('[data-session-action]')?.textContent ?? null,
    unknown:Boolean(node?.querySelector('[data-session-resolution="unknown"]')), warning:Boolean(node?.querySelector('[data-session-close="uncertain"]')) }; })()`); }
  await Promise.all([send('Runtime.enable'), send('Log.enable'), send('Page.enable'), send('Network.enable')]); await send('Network.setCacheDisabled', { cacheDisabled: true });
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  const injection = await send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
    const listeners=new Set(); const sent=[]; const initial=${JSON.stringify(snapshot())};
    const receive=data=>listeners.forEach(listener=>listener({data}));
    window.a5Qa={sent,initial,catalog:initial.displayEnumeration,emit(payload,patch={}){receive({kind:'event',version:1,type:'LauncherSessionsChanged',generation:${JSON.stringify(generation)},payload,...patch});}};
    window.chrome??={}; window.chrome.webview={postMessage(command){sent.push(command);let payload;
      if(command.type==='GetLauncherBootstrapSnapshot')payload=window.a5Qa.initial;
      else if(command.type==='RefreshDisplays')payload=window.a5Qa.catalog;
      else throw new Error('Unexpected production command '+command.type);
      queueMicrotask(()=>receive({kind:'result',version:1,requestId:command.requestId,type:command.type,generation:command.generation,ok:true,payload}));},
      addEventListener(_type,listener){listeners.add(listener);},removeEventListener(_type,listener){listeners.delete(listener);}};
  })();` });
  async function load() { await send('Page.navigate', { url: origin + '/?mode=production&generation=' + generation }); await wait('document.querySelector(".production-launcher")?.dataset.bootstrapState === "ready"'); await rest(); }
  await load();
  await check('Initial registry absence remains neutral with explicit Create intent', async () => {
    const value = await facts(); assert.equal(value.status, 'noRegisteredSession'); assert.equal(value.incarnation, '');
    assert.equal(value.statusLabel, 'Not open'); assert.equal(value.actionLabel, 'Get started');
    assert(await evaluate('document.querySelector("[data-session-action=Create]")?.disabled === false'));
  });
  await check('Live join appears on exact physical Display card without Refresh', async () => {
    await emit(sessions(1)); const value = await facts(); assert.equal(value.incarnation, incarnation); assert.equal(value.bound, rows[0].id); assert.equal(value.status, 'ACTIVE');
    assert.equal(await evaluate('window.a5Qa.sent.length'), 1);
  });
  await check('Control loss remains active registered identity with independent secondary fact', async () => {
    await emit(sessions(2, [role({ controlConnected: false })])); const value = await facts(); assert.equal(value.status, 'ACTIVE'); assert.equal(value.control, 'false');
    assert(await evaluate('document.querySelector(".detail-context [data-session-control=unavailable]")!==null'));
  });
  await check('Successful Display disappearance keeps exact incarnation visibly DISCONNECTED outside missing card', async () => {
    await refresh(catalog([rows[1]])); const value = await facts('.production-session-record'); assert.equal(value.status, 'DISCONNECTED'); assert.equal(value.incarnation, incarnation);
    assert.equal(value.bound, rows[0].id); assert.equal(value.statusLabel, 'Display disconnected');
    assert.equal(await evaluate('document.querySelector(".production-session-bound").textContent'), 'Workspace');
    assert.equal((await facts()).status, 'noRegisteredSession'); assert.equal(await evaluate('document.querySelector(".production-launcher").dataset.selectedDisplayId'), rows[1].id);
  });
  await check('Enumeration failure keeps retained role with unknown resolution, no false DISCONNECTED', async () => {
    await refresh(catalog([], 'ApiFailure')); const value = await facts('.production-session-record'); assert.equal(value.status, 'REGISTERED'); assert.equal(value.unknown, true);
    assert.equal(await evaluate('document.querySelector("[data-session-status=DISCONNECTED]")!==null'), false);
  });
  await check('Exact Display return resolves same incarnation without creating Session', async () => {
    await refresh(catalog()); const value = await facts(); assert.equal(value.incarnation, incarnation); assert.equal(value.status, 'ACTIVE');
  });
  await check('Authoritative Pending outranks Display absence and preserves both facts', async () => {
    await emit(sessions(3, [role({ closeOperation: { generation: otherEpoch, request: '9', result: 'Pending' } })])); await refresh(catalog([rows[1]]));
    const value = await facts('.production-session-record'); assert.equal(value.status, 'CLOSING'); assert.equal(value.close, 'Pending');
    assert(await evaluate('document.querySelector(".production-session-record [data-session-resolution=unresolved]")!==null'));
  });
  await check('CloseAccepted retains live incarnation until verified removal', async () => {
    await emit(sessions(4, [role({ closeOperation: { generation: otherEpoch, request: '9', result: 'CloseAccepted' } })]));
    const value = await facts('.production-session-record'); assert.equal(value.status, 'CLOSING'); assert.equal(value.incarnation, incarnation);
  });
  await check('CloseAborted and Rejected exit CLOSING without erasing Session', async () => {
    await refresh(catalog());
    // A2 correctly preserves selected B when A returns; select A explicitly.
    await evaluate('document.querySelector(\'[aria-label="Go to QA Panel A"]\').click()'); await rest();
    for (const [index,result] of ['CloseAborted','Rejected'].entries()) { await emit(sessions(5+index,[role({closeOperation:{generation:otherEpoch,request:'9',result}})])); assert.equal((await facts()).status,'ACTIVE'); assert.equal((await facts()).incarnation,incarnation); }
  });
  await check('Uncertain is independent warning without guessed ending state', async () => {
    await emit(sessions(7,[role({closeOperation:{generation:otherEpoch,request:'9',result:'Uncertain'}})])); const value=await facts(); assert.equal(value.status,'ACTIVE'); assert.equal(value.warning,true);
  });
  await check('Different incarnation on same Display replaces exact registered role', async () => {
    await emit(sessions(8,[role({incarnation:replacement})])); assert.equal((await facts()).incarnation,replacement);
    assert.equal(await evaluate('document.querySelectorAll(\'[data-session-incarnation="' + incarnation + '"]\').length'),0);
  });
  await check('Old revision, old page, duplicate malformed update cannot remove current role', async () => {
    await emit(sessions(7,[])); await emit(sessions(10,[]),{generation:otherEpoch}); await emit(sessions(11,[role(),role({incarnation:replacement})]));
    assert.equal((await facts()).incarnation,replacement); assert.equal(await evaluate('document.querySelector(".production-launcher").dataset.sessionRevision'),'8');
  });
  await check('Verified registry removal leaves no ghost, fake crash or synthetic CLOSING', async () => {
    await emit(sessions(9,[])); assert.equal((await facts()).status,'noRegisteredSession'); assert.equal((await facts()).incarnation,'');
    assert.equal(await evaluate('document.querySelector(".production-session-record")!==null'),false);
    assert.equal(await evaluate('/CRASHED|STARTING|AVAILABLE/.test(document.body.textContent)'),false);
  });
  await check('Fresh page reconstructs current surviving role from native bootstrap projection', async () => {
    // Replace test-document initial injection, exactly as a new native page gets a fresh snapshot.
    await send('Page.removeScriptToEvaluateOnNewDocument',{identifier:injection.identifier});
    await send('Page.addScriptToEvaluateOnNewDocument',{source:`(() => { const listeners=new Set();const value=${JSON.stringify(snapshot(sessions(10,[role({incarnation:replacement})])))};
      window.chrome??={};window.chrome.webview={postMessage(c){queueMicrotask(()=>listeners.forEach(l=>l({data:{kind:'result',version:1,type:c.type,requestId:c.requestId,generation:c.generation,ok:true,payload:value}})));},addEventListener(t,l){listeners.add(l);},removeEventListener(t,l){listeners.delete(l);}};})();`});
    await load(); assert.equal((await facts()).incarnation,replacement); assert.equal((await facts()).status,'ACTIVE');
  });
  await check('No horizontal overflow, console warning or browser/runtime error', async () => {
    assert.equal(await evaluate('document.documentElement.scrollWidth>innerWidth+1'),false); assert.deepEqual(errors,[]); assert.deepEqual(warnings,[]);
  });
  const output={pass:checks.every(check=>check.pass),scope:'Mounted production React with isolated CDP transport fixtures; native process proof is separate',checks,errors,warnings};
  writeFileSync(new URL('focused-checks.json',cache),JSON.stringify(output,null,2));
  console.log(JSON.stringify({pass:output.pass,checks:checks.length,failed:checks.filter(check=>!check.pass),errors:errors.length,warnings:warnings.length},null,2));
  assert(output.pass,'A5 mounted Session projection checks must pass');
} finally { socket?.close(); chrome.kill(); }
