import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

// Own headless browser + test-only document. The actual production Shell is
// mounted/unmounted; no harness export or mock switch is added to product code.
const origin = 'http://127.0.0.1:5173', debuggerOrigin = 'http://127.0.0.1:9393';
const cache = new URL('../node_modules/.cache/launcher-hardening-a7/', import.meta.url); mkdirSync(cache, { recursive: true });
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311', epoch = 'dba10904-5b14-43f0-995b-17bc99181d43';
const incarnation = 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2', operationId = '6812eb13-67c4-4fc1-8bae-0216f2472645';
const rows = ['A', 'B'].map((name, index) => ({ id: 'exact opaque A7 target ' + name, identityKind: 'TargetDevicePath', persistentOwnerCapable: true,
  friendlyName: 'QA Panel ' + name, sourceDeviceName: 'QA_SOURCE_' + name, desktopX: index * 1920, desktopY: 0,
  pixelWidth: 1920, pixelHeight: 1080, primary: index === 0, displayNumber: index + 1 }));
const catalog = (displays = rows) => ({ status: 'Success', displays });
const language = (effective = 'en', revision = 0) => ({ readStatus: 'Success', committed: effective, effective, revision });
const role = () => ({ boundDisplayId: rows[0].id, incarnation, controlConnected: true });
const operation = () => ({ displayId: rows[0].id, operationId, state: 'Pending' });
const sessions = (revision = 0, roles = [], operations = []) => ({ coordinatorEpoch: epoch, sessionRevision: revision,
  registeredSessionsScope: 'verified-registered-only', createOperations: operations, registeredSessions: roles });
const snapshot = (facts = sessions(), patch = {}) => ({ ...facts, displayEnumeration: catalog(), language: language(),
  host: { ready: true, launcherRoleReady: true }, ...patch });
writeFileSync(new URL('mounted.html', cache), '<!doctype html><html><head><link rel="icon" href="data:,"></head><body><div id="a7-root"></div><script type="module" src="./mounted.tsx"></script></body></html>');
writeFileSync(new URL('mounted.tsx', cache), `import React,{StrictMode} from 'react';
import {createRoot} from 'react-dom/client';
import {ProductionLauncherShell} from '/src/ProductionLauncherShell.tsx';
let root;window.a7Mount={mount(strict=false){if(root)throw new Error('Unmount before mounting');root=createRoot(document.getElementById('a7-root'));root.render(strict?<StrictMode><ProductionLauncherShell/></StrictMode>:<ProductionLauncherShell/>);},unmount(){root?.unmount();root=null;}};
window.a7Mount.mount();`);
await assert.rejects(fetch(debuggerOrigin + '/json/list'), 'Owned QA port must be free');
const chrome = spawn(chromeExecutable, ['--headless=new', '--disable-gpu', '--no-first-run',
  '--no-default-browser-check', '--disable-background-networking', '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=9393',
  '--user-data-dir=' + fileURLToPath(new URL('profile/', cache)), 'about:blank'], { windowsHide: true, stdio: 'ignore' });
let socket; const checks = [], errors = [], warnings = [], reloadCycles = [], remountCycles = [];
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
  async function navigate(url) {
    const marker = crypto.randomUUID(); await evaluate('window.a7NavigateMarker=' + JSON.stringify(marker));
    await send('Page.navigate', { url });
    await wait('window.a7NavigateMarker!==' + JSON.stringify(marker) + '&&location.href===' + JSON.stringify(url));
  }
  async function check(name, run) { try { await run(); checks.push({ name, pass: true }); } catch (error) { checks.push({ name, pass: false, error: String(error) }); } }
  async function click(selector) { await evaluate('(() => {const n=document.querySelector(' + JSON.stringify(selector) + ');if(!n)throw new Error("Missing click target");n.focus();n.click();})()'); await rest(); }
  const ready = 'document.querySelector(".production-launcher")?.dataset.bootstrapState==="ready"';
  const mountedUrl = origin + '/node_modules/.cache/launcher-hardening-a7/mounted.html?mode=production&generation=' + generation;
  await Promise.all([send('Runtime.enable'), send('Log.enable'), send('Page.enable'), send('Network.enable')]); await send('Network.setCacheDisabled', { cacheDisabled: true });
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  let injection;
  async function install(value = snapshot(), { noBridge = false, failure = false, holds = [] } = {}) {
    if (injection) await send('Page.removeScriptToEvaluateOnNewDocument', { identifier: injection.identifier });
    injection = await send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
      const listeners=new Set(),retired=[],sent=[];const initial=${JSON.stringify(value)};
      const q=window.a7Qa={sent,initial,holds:new Set(${JSON.stringify(holds)}),adds:0,removes:0,maxListeners:0,
        stats(){return {listeners:listeners.size,adds:q.adds,removes:q.removes,maxListeners:q.maxListeners,commands:sent.map(c=>c.type)};},
        receive(data,late=false){for(const fn of late?retired:listeners)fn({data});},
        reply(payload,command=sent.at(-1),patch={},late=false){q.receive({kind:'result',version:1,requestId:command.requestId,type:command.type,generation:command.generation,ok:true,payload,...patch},late);},
        emit(type,payload,gen=${JSON.stringify(generation)},late=false){q.receive({kind:'event',version:1,type,generation:gen,payload},late);}
      };
      if(${noBridge})return;
      window.chrome??={};window.chrome.webview={postMessage(command){sent.push(command);if(q.holds.has(command.type))return;let payload;
       if(command.type==='GetLauncherBootstrapSnapshot'){if(${failure}){queueMicrotask(()=>q.reply(undefined,command,{ok:false,error:'isolated native bootstrap failure'}));return;}payload=q.initial;}
       else if(command.type==='RefreshDisplays')payload=q.initial.displayEnumeration;
       else if(command.type==='IdentifyDisplay')payload={displayId:command.payload.displayId,status:'Shown'};
       else if(command.type==='CommitLanguage'){q.initial.language={readStatus:'Success',committed:command.payload.language,effective:command.payload.language,revision:q.initial.language.revision+1};payload={status:'COMMITTED',language:q.initial.language,warnings:[]};}
       else if(command.type==='OpenSession')payload={...command.payload,status:'ActivationRequested'};
       else if(command.type==='CreateSession')return;
       else throw new Error('Unexpected command '+command.type);
       queueMicrotask(()=>q.reply(payload,command));
      },addEventListener(type,fn){if(type!=='message'||listeners.has(fn))throw new Error('Duplicate bridge handler');listeners.add(fn);q.adds++;q.maxListeners=Math.max(q.maxListeners,listeners.size);},
      removeEventListener(type,fn){if(type!=='message'||!listeners.delete(fn))throw new Error('Unbalanced bridge teardown');retired.push(fn);q.removes++;}};
    })();` });
  }
  async function load(value = snapshot(), options = {}) {
    await install(value, options); await navigate(mountedUrl); await wait('window.a7Mount!==undefined');
    if (!options.holds?.includes('GetLauncherBootstrapSnapshot')) await wait(ready);
    await rest();
  }
  async function remount(value, strict = false) {
    await evaluate('window.a7Mount.unmount()');
    assert.equal((await evaluate('window.a7Qa.stats()')).listeners, 0);
    if (value) await evaluate('window.a7Qa.initial=' + JSON.stringify(value));
    await evaluate('window.a7Mount.mount(' + strict + ')'); await wait(ready); await rest();
  }
  async function state() { return evaluate(`(() => {const shell=document.querySelector('.production-launcher'),detail=document.querySelector('.detail-context');return {status:detail?.querySelector('[data-session-status]')?.dataset.sessionStatus,incarnation:detail?.dataset.sessionIncarnation,operationId:detail?.dataset.createOperationId,language:shell?.dataset.language,selected:shell?.dataset.selectedDisplayId,revision:shell?.dataset.sessionRevision};})()`); }
  const stats = () => evaluate('window.a7Qa.stats()');

  await check('Production mode without Bridge fails explicitly, never shows fixture Displays or Session', async () => {
    await install(snapshot(), { noBridge: true });
    await navigate(origin + '/?mode=production&generation=' + generation + '&launcherScenario=starting');
    await wait('document.querySelector(".production-launcher")?.dataset.bootstrapState==="failed"');
    assert.equal(await evaluate('!!document.querySelector(".display-deck,[data-session-action],.launcher-dev")'), false);
    assert.equal((await stats()).commands.length, 0);
  });
  await check('Explicit production bootstrap failure does not promote fixtures or native readiness', async () => {
    await install(snapshot(), { failure: true }); await navigate(origin + '/?mode=production&generation=' + generation);
    await wait('document.querySelector(".production-launcher")?.dataset.bootstrapState==="failed"');
    assert.equal(await evaluate('!!document.querySelector(".display-deck,.launcher-dev,[data-session-status]")'), false);
    assert.deepEqual((await stats()).commands, ['GetLauncherBootstrapSnapshot']);
  });
  await check('Explicit dev prototype and dev scenario switch remain usable and visibly labelled', async () => {
    await install(snapshot(), { noBridge: true }); await navigate(origin + '/?mode=prototype&view=launcher&launcherScenario=no-displays');
    await wait('document.querySelector(".launcher-dev")!==null');
    assert(await evaluate('document.body.textContent.includes("UI prototype")'));
    assert.equal(await evaluate('!!document.querySelector(".production-launcher,.display-deck")'), false);
    await evaluate('(() => {const s=document.querySelector("#launcher-scenario");s.value="normal";s.dispatchEvent(new Event("change",{bubbles:true}));})()');
    await wait('document.querySelector(".display-deck")!==null');
    assert.equal((await stats()).commands.length, 0);
  });
  await check('Mounted fresh production Shell reconstructs exact native facts and language', async () => {
    await load(snapshot(sessions(4, [role()]), { language: language('zh_CN', 2) }));
    assert.deepEqual(await state(), { status: 'ACTIVE', incarnation, operationId: '', language: 'zh_CN', selected: rows[0].id, revision: '4' });
    assert.deepEqual((await stats()).commands, ['GetLauncherBootstrapSnapshot']);
  });
  await check('50 real React unmount/remount cycles release Bridge listeners and never replay domain commands', async () => {
    for (let index = 0; index < 50; index++) {
      await remount(); const actual = await stats();
      assert.equal(actual.listeners, 1); assert.equal(actual.adds - actual.removes, 1); assert.equal(actual.maxListeners, 1);
      assert(actual.commands.every(command => command === 'GetLauncherBootstrapSnapshot'));
      assert.equal((await state()).incarnation, incarnation); remountCycles.push(index + 1);
    }
    assert.equal(remountCycles.length, 50);
  });
  await check('Immediately after 50 remounts, one click per capability issues one command, including Create', async () => {
    const before = (await stats()).commands.length;
    await click('[data-refresh-displays]'); await click('[data-identify-display]');
    await click('.detail-context [data-session-action]'); // existing exact ACTIVE incarnation -> Open
    await click('[data-settings-open]'); await draftLanguage('en'); await click('[data-settings-apply]');
    await click('[data-settings-cancel]');
    // A native removal, not a Create result, makes this Display eligible again.
    await evaluate('window.a7Qa.emit("LauncherSessionsChanged",' + JSON.stringify(sessions(5)) + ')'); await rest();
    assert.equal((await state()).status, 'noRegisteredSession');
    await click('.detail-context [data-session-action]');
    assert.deepEqual((await stats()).commands.slice(before), ['RefreshDisplays', 'IdentifyDisplay', 'OpenSession', 'CommitLanguage', 'CreateSession']);
    assert.equal((await state()).status, 'noRegisteredSession');
    await evaluate('window.a7Qa.reply(' + JSON.stringify({ coordinatorEpoch: epoch, displayId: rows[0].id, status: 'Accepted', operationId }) + ')');
    await rest(); assert.equal((await state()).status, 'noRegisteredSession');
  });
  await check('StrictMode setup/cleanup replay has one current Bridge listener and no domain command', async () => {
    const before = (await stats()).commands.length; await remount(undefined, true);
    const after = await stats(); assert.equal(after.listeners, 1); assert.equal(after.maxListeners, 1);
    assert.deepEqual(after.commands.slice(before), ['GetLauncherBootstrapSnapshot', 'GetLauncherBootstrapSnapshot']);
    assert.equal((await state()).incarnation, incarnation); await remount();
  });
  await check('20 actual page reloads retain ACTIVE exact incarnation with bootstrap only', async () => {
    for (let index = 0; index < 20; index++) {
      // Wait for the old document to be replaced, not its already-ready DOM.
      const marker = crypto.randomUUID(); await evaluate('window.a7ReloadMarker=' + JSON.stringify(marker));
      await send('Page.reload', { ignoreCache: true });
      await wait('window.a7ReloadMarker!==' + JSON.stringify(marker) + '&&' + ready);
      const actual = await state(); assert.equal(actual.status, 'ACTIVE'); assert.equal(actual.incarnation, incarnation);
      const current = await stats(); assert.equal(current.listeners, 1); assert.deepEqual(current.commands, ['GetLauncherBootstrapSnapshot']);
      reloadCycles.push(index + 1);
    }
    assert.equal(reloadCycles.length, 20);
  });
  await check('Pending Create reboots from native operation and eventual Join without replay', async () => {
    await load(snapshot(sessions(5, [], [operation()]))); assert.equal((await state()).status, 'STARTING');
    await remount(); assert.equal((await state()).operationId, operationId); assert.equal((await state()).status, 'STARTING');
    await evaluate('window.a7Qa.emit("LauncherSessionsChanged",' + JSON.stringify(sessions(6, [role()])) + ')'); await rest();
    assert.equal((await state()).status, 'ACTIVE'); assert.equal((await state()).incarnation, incarnation);
    assert((await stats()).commands.every(command => command === 'GetLauncherBootstrapSnapshot'));
  });

  const lateCases = [
    ['GetLauncherBootstrapSnapshot', null, snapshot()],
    ['RefreshDisplays', '[data-refresh-displays]', catalog([])],
    ['IdentifyDisplay', '[data-identify-display]', { displayId: rows[0].id, status: 'Shown' }],
    ['CommitLanguage', 'language', { status: 'COMMITTED', language: language('zh_CN', 1), warnings: [] }],
    ['CreateSession', '.detail-context [data-session-action]', { coordinatorEpoch: epoch, displayId: rows[0].id, status: 'Accepted', operationId }],
    ['OpenSession', '.detail-context [data-session-action]', { coordinatorEpoch: epoch, incarnation, status: 'ActivationRequested' }],
  ];
  async function draftLanguage(value) {
    await evaluate('(() => {const s=document.querySelector("[data-settings-language]");Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,"value").set.call(s,' + JSON.stringify(value) + ');s.dispatchEvent(new Event("change",{bubbles:true}));})()'); await rest();
  }
  for (const [type, selector, payload] of lateCases) await check('Unmount fences copied late ' + type + ' callback; new Shell uses fresh bootstrap', async () => {
    await load(snapshot(sessions(0, type === 'OpenSession' ? [role()] : [])), { holds: [type] });
    if (selector === 'language') { await click('[data-settings-open]'); await draftLanguage('zh_CN'); await click('[data-settings-apply]'); }
    else if (selector) await click(selector);
    const issued = await evaluate('window.a7Qa.sent.at(-1)'); assert.equal(issued.type, type);
    await evaluate('window.a7Mount.unmount()'); assert.equal((await stats()).listeners, 0);
    const nextGeneration = crypto.randomUUID();
    await evaluate('history.replaceState(null,"",location.pathname+"?mode=production&generation=' + nextGeneration + '");window.a7Qa.holds.clear();window.a7Qa.initial=' + JSON.stringify(snapshot(sessions(10, [role()]), { language: language('zh_CN', 8), displayEnumeration: catalog([rows[1], rows[0]]) })) + ';window.a7Mount.mount();');
    await wait(ready); await rest(); const before = await evaluate('document.querySelector(".production-launcher-root").outerHTML');
    const count = (await stats()).commands.length;
    await evaluate('window.a7Qa.reply(' + JSON.stringify(payload) + ',' + JSON.stringify(issued) + ',{},true);window.a7Qa.reply(' + JSON.stringify(payload) + ',' + JSON.stringify(issued) + ');window.a7Qa.emit("LauncherSessionsChanged",' + JSON.stringify(sessions(999, [])) + ',' + JSON.stringify(generation) + ');window.a7Qa.emit("LauncherLanguageChanged",' + JSON.stringify(language('en', 999)) + ',' + JSON.stringify(generation) + ',true);');
    await rest(); assert.equal(await evaluate('document.querySelector(".production-launcher-root").outerHTML'), before);
    assert.equal((await stats()).commands.length, count); assert.equal((await state()).language, 'zh_CN');
    assert.equal((await state()).selected, rows[1].id);
  });
  await check('After remount, one Refresh click and one Identify click each issue one command', async () => {
    await load(); await remount(); const before = (await stats()).commands.length;
    await click('[data-refresh-displays]'); await click('[data-identify-display]');
    assert.deepEqual((await stats()).commands.slice(before), ['RefreshDisplays', 'IdentifyDisplay']);
    assert.equal((await state()).status, 'noRegisteredSession');
  });
  await check('After remount, one Language Apply issues one commit; draft is not authoritative', async () => {
    const before = (await stats()).commands.length; await click('[data-settings-open]'); await draftLanguage('zh_CN');
    assert.equal((await state()).language, 'en'); await click('[data-settings-apply]');
    assert.equal((await state()).language, 'zh_CN'); assert.deepEqual((await stats()).commands.slice(before), ['CommitLanguage']);
    await click('[data-settings-cancel]');
  });
  await check('After remount, one Create click sends one command without local STARTING or ACTIVE', async () => {
    await remount(); const before = (await stats()).commands.length; await click('.detail-context [data-session-action]');
    assert.deepEqual((await stats()).commands.slice(before), ['CreateSession']);
    assert.equal((await state()).status, 'noRegisteredSession');
    await evaluate('window.a7Qa.emit("LauncherSessionsChanged",' + JSON.stringify(sessions(1, [], [operation()])) + ');window.a7Qa.reply(' + JSON.stringify({ coordinatorEpoch: epoch, displayId: rows[0].id, status: 'Accepted', operationId }) + ');'); await rest();
    assert.equal((await state()).status, 'STARTING');
  });
  await check('After actual page reload, one Open click targets exact incarnation once with no Create fallback', async () => {
    await install(snapshot(sessions(3, [role()]))); const marker = crypto.randomUUID(); await evaluate('window.a7ReloadMarker=' + JSON.stringify(marker));
    await send('Page.reload', { ignoreCache: true });
    await wait('window.a7ReloadMarker!==' + JSON.stringify(marker) + '&&' + ready);
    await rest(); const before = (await stats()).commands.length; await click('.detail-context [data-session-action]');
    assert.deepEqual((await stats()).commands.slice(before), ['OpenSession']);
    const issued = await evaluate('window.a7Qa.sent.at(-1)'); assert.deepEqual(issued.payload, { coordinatorEpoch: epoch, incarnation });
    assert.equal((await state()).status, 'ACTIVE'); assert.equal((await state()).incarnation, incarnation);
  });
  await check('Final real Shell teardown releases one listener, with zero errors or warnings accumulated', async () => {
    await evaluate('window.a7Mount.unmount()'); const actual = await stats();
    assert.equal(actual.listeners, 0); assert.equal(actual.adds, actual.removes); assert.equal(actual.maxListeners, 1);
    assert.deepEqual(errors, []); assert.deepEqual(warnings, []);
  });
  const output = { pass: checks.every(check => check.pass), scope: 'Mounted actual production React + owned transport fixture; native operation/process proof is separate',
    sourceBehaviorChanges: 'NONE', checks, actualPageReloadCycles: reloadCycles.length, realReactUnmountRemountCycles: remountCycles.length, errors, warnings };
  writeFileSync(new URL('dom-checks.json', cache), JSON.stringify(output, null, 2));
  console.log(JSON.stringify({ pass: output.pass, total: checks.length, actualPageReloadCycles: reloadCycles.length, realReactUnmountRemountCycles: remountCycles.length,
    failed: checks.filter(check => !check.pass), errors: errors.length, warnings: warnings.length }, null, 2));
  assert(output.pass, 'A7 mounted frontend lifetime/authority checks must pass');
} finally { socket?.close(); chrome.kill(); }
