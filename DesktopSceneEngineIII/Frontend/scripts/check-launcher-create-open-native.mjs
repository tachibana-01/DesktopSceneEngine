// A6: real Coordinator + production WebView host + same-image independent
// Session peers. Only this executable's test composition uses controlled exact
// target IDs/catalog and an isolated INI. Physical production Human is separate.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync, spawn } from 'node:child_process';
import net from 'node:net';

const repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const argument = name => process.argv.find(value => value.startsWith(`${name}=`))?.slice(name.length + 1);
const executable = path.resolve(argument('--executable') ?? path.join(repository, 'build/bin/Debug/DesktopSceneEngine_LauncherCreateOpenLifecycleTest.exe'));
const port = Number(argument('--port') ?? 9398);
const evidence = path.join(repository, 'DesktopSceneEngineIII/evidence/phase-ii-a6', `native-create-open-${new Date().toISOString().replace(/[:.]/g, '-')}`);
await fs.mkdir(evidence, { recursive: true });
const checks = [], errors = [], warnings = [], clients = [];
const report = {
  started: new Date().toISOString(), executable, port, checks, errors, warnings,
  scope: 'Real Coordinator/admission/activation/Join/control/process lifetimes and production Host/React. Test-only controlled synthetic Display catalog and isolated INI preferences; no production-user preference write. Physical Human Gate separate.',
};
const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const quote = value => `'${String(value).replaceAll("'", "''")}'`;
const encoded = value => Buffer.from(value, 'utf16le').toString('base64');
function powershell(source) {
  return execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-EncodedCommand', encoded(`$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\n${source}`)],
    { windowsHide: true, encoding: 'utf8', maxBuffer: 2 ** 20 }).trim();
}
async function until(name, predicate, budget = 15000) {
  const end = Date.now() + budget;
  while (Date.now() < end) { const result = await predicate(); if (result) return result; await delay(70); }
  throw new Error(`Timed out: ${name}`);
}
async function check(name, body) {
  try { const detail = await body(); checks.push({ name, pass: true, ...(detail === undefined ? {} : { detail }) }); }
  catch (error) { checks.push({ name, pass: false, error: error.stack }); throw error; }
}
function jsonLines(output) { return output.split(/\r?\n/).flatMap(line => { try { return [JSON.parse(line)]; } catch { return []; } }); }
let child;
const journal = () => jsonLines(child?.output ?? '');
const hosts = () => journal().filter(value => value.event === 'hostCreated');
const peerRows = () => journal().filter(value => value.event === 'hostPeer');
const projections = () => journal().filter(value => value.event === 'projection').map(value => value.payload);
async function command(value) {
  assert(child && child.exitCode === null && child.signalCode === null, 'Only owned live fixture accepts test command');
  const harness = journal().find(value => value.event === 'harnessReady');
  assert(harness, 'Real test-only native HWND harness');
  const previous = journal().filter(row => row.event === 'harnessCommand' && row.command === value).length;
  const posted = powershell(`Add-Type -TypeDefinition @'\nusing System;using System.Runtime.InteropServices;public static class A6Harness { [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);[DllImport("user32.dll")] static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr a,IntPtr b); public static bool Invoke(long handle,uint expected,int command) { uint pid;GetWindowThreadProcessId(new IntPtr(handle),out pid);return pid==expected&&PostMessage(new IntPtr(handle),0x8000+186,new IntPtr(command),IntPtr.Zero);}}\n'@\n[A6Harness]::Invoke([long]${quote(harness.hwnd)},[uint32]${child.pid},[int]${value})`);
  assert.equal(posted, 'True', 'Command is posted only to the owned PID-verified harness HWND');
  await until(`native harness command ${value} evaluated`, () => journal().filter(row => row.event === 'harnessCommand' && row.command === value).length > previous || (value === 26 && child.exitCode === 0));
}
async function target(previousGeneration) {
  try {
    const result = await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(700) });
    return (await result.json()).find(value => value.type === 'page' && value.url.startsWith('https://launcher.dse.local/')
      && (!previousGeneration || new URL(value.url).searchParams.get('generation') !== previousGeneration));
  } catch { return undefined; }
}
const observer = `(()=>{if(window.__a6Observer)return;window.__a6Observer=true;window.__a6Incoming=[];window.__a6Outgoing=[];chrome.webview.addEventListener('message',event=>window.__a6Incoming.push(event.data));const original=chrome.webview.postMessage.bind(chrome.webview);chrome.webview.postMessage=message=>{window.__a6Outgoing.push(JSON.parse(JSON.stringify(message)));return original(message)}})()`;
async function connect(row) {
  const socket = new WebSocket(row.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
  let serial = 0;
  const pending = new Map();
  socket.addEventListener('message', ({ data }) => {
    const value = JSON.parse(data);
    if (value.method === 'Runtime.exceptionThrown') errors.push(value.params);
    if (value.method === 'Runtime.consoleAPICalled' && ['error', 'warning'].includes(value.params.type)) (value.params.type === 'error' ? errors : warnings).push(value.params);
    if (value.method === 'Log.entryAdded' && ['error', 'warning'].includes(value.params.entry.level)) (value.params.entry.level === 'error' ? errors : warnings).push(value.params.entry);
    if (value.method === 'Console.messageAdded' && ['error', 'warning'].includes(value.params.message.level)) (value.params.message.level === 'error' ? errors : warnings).push(value.params.message);
    const request = pending.get(value.id);
    if (request) { pending.delete(value.id); clearTimeout(request.timer); value.error ? request.reject(new Error(JSON.stringify(value.error))) : request.resolve(value.result); }
  });
  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++serial;
    const timer = setTimeout(() => { pending.delete(id); reject(new Error(`CDP timeout: ${method}`)); }, 10000);
    pending.set(id, { resolve, reject, timer }); socket.send(JSON.stringify({ id, method, params }));
  });
  const evaluate = async expression => {
    const result = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails)); return result.result.value;
  };
  const client = { socket, send, evaluate }; clients.push(client);
  await send('Runtime.enable'); await send('Console.enable'); await send('Log.enable'); await send('Page.enable');
  await send('Page.addScriptToEvaluateOnNewDocument', { source: observer }); await evaluate(observer);
  return client;
}
async function ready(client, previous) {
  await until('production Host ready with current native page generation', async () => {
    try { return await client.evaluate(`document.querySelector('.production-launcher')?.dataset.bootstrapState==='ready'${previous ? `&&new URL(location.href).searchParams.get('generation')!==${JSON.stringify(previous)}` : ''}`); }
    catch (error) { if (/context.*destroyed|Cannot find context|Inspected target navigated/i.test(error.message)) return false; throw error; }
  });
}
async function attach(previousGeneration) {
  const row = await until('trusted real WebView production page', () => target(previousGeneration));
  const client = await connect(row); await ready(client, previousGeneration); return client;
}
async function facts(client) {
  return client.evaluate(`(()=>{const root=document.querySelector('.production-launcher'),detail=document.querySelector('.detail-context');const record=element=>({incarnation:element.dataset.sessionIncarnation??'',boundDisplayId:element.dataset.boundDisplayId??'',control:element.dataset.sessionControlConnected??'',close:element.dataset.sessionCloseResult??'',status:element.querySelector('[data-session-status]')?.dataset.sessionStatus??'',resolution:element.querySelector('[data-session-resolution]')?.dataset.sessionResolution??'',controlUnavailable:!!element.querySelector('[data-session-control="unavailable"]')});return {epoch:root?.dataset.coordinatorEpoch,revision:Number(root?.dataset.sessionRevision),bootstrap:root?.dataset.bootstrapState,generation:new URL(location.href).searchParams.get('generation'),detail:detail?record(detail):null,records:[...document.querySelectorAll('.production-session-record')].map(record),cards:[...document.querySelectorAll('.display-slide:not([aria-hidden="true"])')].map(element=>({displayId:element.querySelector('[data-display-id]')?.dataset.displayId,status:element.querySelector('[data-session-status]')?.dataset.sessionStatus})),states:[...document.querySelectorAll('[data-session-status]')].map(element=>element.dataset.sessionStatus),commands:window.__a6Outgoing??[],sessionEvents:(window.__a6Incoming??[]).filter(value=>value.kind==='event'&&value.type==='LauncherSessionsChanged'),text:document.body.innerText}})()`);
}
async function selectDisplay(client, displayId) {
  assert(await client.evaluate(`(()=>{const button=[...document.querySelectorAll('.display-slide:not([aria-hidden="true"]) [data-display-id]')].find(element=>element.dataset.displayId===${JSON.stringify(displayId)});if(!button)return false;button.click();return true})()`), 'Select exact returned Display through its actual card');
}
async function hasRole(client, incarnation, expected = {}) {
  return until('exact registered role presentation', async () => {
    const value = await facts(client);
    const role = [value.detail, ...value.records].find(row => row?.incarnation === incarnation);
    return role && Object.entries(expected).every(([key, value]) => role[key] === value) ? { ...value, role } : false;
  });
}
async function noRole(client, incarnation) {
  return until('exact removed role disappears from React presentation', async () => {
    const value = await facts(client);
    return ![value.detail, ...value.records].some(row => row?.incarnation === incarnation)
      && !value.states.some(state => ['ACTIVE', 'CLOSING', 'DISCONNECTED', 'REGISTERED'].includes(state)) ? value : false;
  });
}
async function refresh(client) {
  const previous = await client.evaluate(`(window.__a6Incoming??[]).filter(value=>value.kind==='result'&&value.type==='RefreshDisplays').length`);
  assert(await client.evaluate(`(()=>{const button=document.querySelector('[data-refresh-displays]');if(!button||button.disabled)return false;button.click();return true})()`), 'Existing real Refresh button');
  await until('real RefreshDisplays result', () => client.evaluate(`(window.__a6Incoming??[]).filter(value=>value.kind==='result'&&value.type==='RefreshDisplays').length>${previous}`));
}
async function finalShutdown() {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  await command(26);
  await until('owned real Coordinator fixture clean exit', () => child.exitCode !== null);
  assert.equal(child.exitCode, 0, child.output);
  const clean = journal().find(row => row.event === 'cleanShutdown');
  assert(clean && clean.sessionCount === 0, 'No native registered Session left at final shutdown');
}

async function request(client, type, payload, generation) {
  const requestId = `a6_proof_${crypto.randomUUID().replaceAll('-', '')}`;
  await client.evaluate(`chrome.webview.postMessage(${JSON.stringify({ kind: 'command', version: 1, requestId, type, generation: generation ?? (await facts(client)).generation, payload })})`);
  return until(`native ${type} response`, () => client.evaluate(`(window.__a6Incoming??[]).find(value=>value.kind==='result'&&value.requestId===${JSON.stringify(requestId)})`));
}
async function clickAction(client, kind, displayId) {
  assert(await client.evaluate(`(()=>{const button=[...document.querySelectorAll('[data-session-action]')].find(value=>value.dataset.sessionAction===${JSON.stringify(kind)}&&value.dataset.actionDisplayId===${JSON.stringify(displayId)}&&!value.disabled);if(!button)return false;button.click();return true})()`), `Actual production ${kind} button targets exact card identity`);
}
async function starting(client) {
  return until('native pending operation is rendered STARTING', async () => {
    const value = await facts(client); return value.states.includes('STARTING') && !value.states.includes('ACTIVE') ? value : false;
  });
}
function windowState(peer) {
  return JSON.parse(powershell(`Add-Type -TypeDefinition @'\nusing System;using System.Runtime.InteropServices;public static class A6WindowState{[DllImport("user32.dll")]public static extern bool IsIconic(IntPtr h);[DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);public static uint Pid(long h){uint p;GetWindowThreadProcessId(new IntPtr(h),out p);return p;}}\n'@\n$h=[long]${quote(peer.roleHwnd)}\nif([A6WindowState]::Pid($h)-ne[uint32]${peer.pid}){throw 'Owned peer HWND mismatch'}\n@{minimized=[A6WindowState]::IsIconic([IntPtr]$h);maximized=[A6WindowState]::IsZoomed([IntPtr]$h)}|ConvertTo-Json -Compress`));
}

try {
  await check('Isolated production-path same-image Host fixture and private CDP port', async () => {
    await fs.access(executable);
    await new Promise((resolve, reject) => {
      const socket = net.connect({ host: '127.0.0.1', port });
      socket.once('connect', () => { socket.destroy(); reject(new Error(`Private debug port ${port} is occupied`)); }); socket.once('error', resolve);
    });
    child = spawn(executable, ['--host'], { cwd: path.dirname(executable), windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
      env: { ...process.env, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${port} --remote-allow-origins=http://127.0.0.1:${port}`, WEBVIEW2_USER_DATA_FOLDER: path.join(evidence, 'profile') } });
    child.output = ''; child.stdout.on('data', value => { child.output += value; }); child.stderr.on('data', value => { child.output += value; }); child.on('error', error => { child.output += error.stack; });
    await until('isolated actual Coordinator harness ready', () => journal().find(value => value.event === 'harnessReady') || child.exitCode !== null);
    assert.equal(child.exitCode, null, child.output); report.harness = journal().find(value => value.event === 'harnessReady');
  });
  let client = await attach(); const targetId = report.harness.firstDisplayId;
  await check('Unregistered owner-capable card offers Create without AVAILABLE or STARTING', async () => {
    const value = await facts(client); assert.equal(value.detail.incarnation, '');
    assert(value.states.includes('noRegisteredSession')); assert(!value.states.some(state => ['AVAILABLE', 'STARTING'].includes(state)));
    assert(await client.evaluate(`!![...document.querySelectorAll('[data-session-action="Create"]')].find(value=>value.dataset.actionDisplayId===${JSON.stringify(targetId)}&&!value.disabled)`));
  });
  let pendingId;
  await check('React Create launches one actual same-image child and native Pending alone supplies STARTING', async () => {
    await command(20); await clickAction(client, 'Create', targetId);
    await until('one native Create command accepted', () => journal().some(value => value.event === 'createCommand' && value.result.status === 'Accepted'));
    const row = journal().find(value => value.event === 'createCommand'); assert.equal(row.launchCount, 1); pendingId = row.result.operationId;
    const value = await starting(client);
    assert(value.commands.some(row => row.type === 'CreateSession' && row.payload.displayId === targetId));
    assert(!await client.evaluate(`!![...document.querySelectorAll('[data-session-action="Create"]')].find(value=>value.dataset.actionDisplayId===${JSON.stringify(targetId)}&&!value.disabled)`));
    const snapshot = await request(client, 'GetLauncherBootstrapSnapshot', {}); assert.equal(snapshot.ok, true);
    const operation = snapshot.payload.createOperations.find(row => row.displayId === targetId);
    assert.equal(operation.operationId, pendingId); assert.equal(operation.state, 'Pending'); assert.equal(snapshot.payload.registeredSessions.length, 0);
    return { operation, launchCount: row.launchCount };
  });
  let oldGeneration;
  await check('Pending React reload restores same native operation without replaying Create', async () => {
    const previous = await facts(client); oldGeneration = previous.generation;
    await client.send('Page.reload', { ignoreCache: true }); await ready(client, previous.generation);
    const value = await starting(client); assert.notEqual(value.generation, previous.generation);
    assert.equal(journal().filter(row => row.event === 'createCommand').length, 1);
    assert(!value.commands.some(row => row.type === 'CreateSession'));
    const snapshot = await request(client, 'GetLauncherBootstrapSnapshot', {});
    assert.equal(snapshot.payload.createOperations.find(row => row.displayId === targetId).operationId, pendingId);
  });
  await check('Retired page Create cannot execute against current native owner', async () => {
    const before = journal().filter(row => row.event === 'createCommand').length;
    const value = await facts(client);
    await client.evaluate(`chrome.webview.postMessage(${JSON.stringify({ kind: 'command', version: 1, requestId: 'a6_old_page_proof', type: 'CreateSession', generation: oldGeneration, payload: { coordinatorEpoch: value.epoch, displayId: report.harness.secondDisplayId } })})`);
    await request(client, 'GetLauncherBootstrapSnapshot', {});
    assert.equal(journal().filter(row => row.event === 'createCommand').length, before, 'Stale generation is fenced before provider execution');
  });
  await check('Real Launcher Host retire/recreate restores pending operation without second process', async () => {
    const previous = await facts(client); await command(25); await until('second Coordinator Host creation', () => hosts().length === 2);
    client.socket.close(); client = await attach(previous.generation);
    const value = await starting(client); assert.equal(value.epoch, previous.epoch); assert.notEqual(value.generation, previous.generation);
    assert(!value.commands.some(row => row.type === 'CreateSession')); assert.equal(journal().filter(row => row.event === 'createCommand').length, 1);
    const snapshot = await request(client, 'GetLauncherBootstrapSnapshot', {});
    assert.equal(snapshot.payload.createOperations.find(row => row.displayId === targetId).operationId, pendingId);
  });
  await check('Display Refresh preserves pending native operation and registry', async () => {
    await refresh(client); const snapshot = await request(client, 'GetLauncherBootstrapSnapshot', {});
    const operation = snapshot.payload.createOperations.find(row => row.displayId === targetId);
    assert.equal(operation.operationId, pendingId); assert.equal(operation.state, 'Pending'); assert.equal(snapshot.payload.registeredSessions.length, 0);
    assert.equal(journal().filter(row => row.event === 'createCommand').length, 1);
  });
  let first;
  await check('Actual authenticated Session Join ends Pending and live A5 facts render ACTIVE', async () => {
    await command(21); first = peerRows().at(-1); assert(first);
    const value = await hasRole(client, first.incarnation, { status: 'ACTIVE', control: 'true' });
    assert(!value.states.includes('STARTING')); assert.equal(first.displayId, targetId);
    const snapshot = await request(client, 'GetLauncherBootstrapSnapshot', {});
    const operation = snapshot.payload.createOperations.find(row => row.displayId === targetId);
    assert.equal(operation.state, 'Registered'); assert.equal(operation.incarnation, first.incarnation);
    assert.equal(snapshot.payload.registeredSessions[0].incarnation, first.incarnation); return { peer: first, operation };
  });
  await check('Actual React Open targets exact incarnation and restores minimized native Session', async () => {
    await command(23); await until('owned peer is actually minimized', () => windowState(first).minimized);
    await clickAction(client, 'Open', targetId);
    await until('exact native Open provider result', () => journal().some(row => row.event === 'openCommand' && row.result.incarnation === first.incarnation && row.result.status === 'ActivationRequested'));
    await until('production control/show route restored actual Session', () => !windowState(first).minimized);
    const value = await hasRole(client, first.incarnation, { status: 'ACTIVE' });
    assert.equal(journal().filter(row => row.event === 'createCommand').length, 1);
    assert(value.commands.some(row => row.type === 'OpenSession' && row.payload.incarnation === first.incarnation));
  });
  await check('Production Open preserves maximized window state after minimizing', async () => {
    await command(24); await until('actual peer is maximized', () => windowState(first).maximized);
    await command(23); await until('maximized peer is minimized', () => windowState(first).minimized);
    const before = journal().filter(row => row.event === 'openCommand').length; await clickAction(client, 'Open', targetId);
    await until('new actual Open result', () => journal().filter(row => row.event === 'openCommand').length > before);
    await until('restore preserved native maximize', () => { const state = windowState(first); return !state.minimized && state.maximized; });
  });
  await check('Ordinary real Session close removes ACTIVE without fake CLOSING or STARTING', async () => {
    await command(22); const value = await noRole(client, first.incarnation);
    assert(!value.states.some(state => ['STARTING', 'CLOSING', 'CRASHED'].includes(state)));
  });
  let replacement;
  await check('Explicit second Create produces fresh incarnation on same exact Display', async () => {
    await clickAction(client, 'Create', targetId); await starting(client); await command(21); replacement = peerRows().at(-1);
    assert.notEqual(replacement.incarnation, first.incarnation); assert.equal(replacement.displayId, targetId);
    await hasRole(client, replacement.incarnation, { status: 'ACTIVE', control: 'true' });
    assert.equal(journal().filter(row => row.event === 'createCommand').length, 2);
  });
  await check('Stale exact Open rejects without retargeting replacement or Create fallback', async () => {
    const value = await facts(client); const before = journal().filter(row => row.event === 'createCommand').length;
    const result = await request(client, 'OpenSession', { coordinatorEpoch: value.epoch, incarnation: first.incarnation });
    assert.equal(result.ok, true); assert.equal(result.payload.status, 'RoleGone'); assert.equal(result.payload.incarnation, first.incarnation);
    await hasRole(client, replacement.incarnation, { status: 'ACTIVE' });
    assert.equal(journal().filter(row => row.event === 'createCommand').length, before);
  });
  await check('Stale Coordinator epoch Open fails with explicit capability result', async () => {
    const result = await request(client, 'OpenSession', { coordinatorEpoch: crypto.randomUUID(), incarnation: replacement.incarnation });
    assert.equal(result.ok, true); assert.equal(result.payload.status, 'StaleCoordinator');
    await hasRole(client, replacement.incarnation, { status: 'ACTIVE' });
  });
  await check('No Close command is emitted and Create occurs only for two explicit actions', async () => {
    const value = await facts(client);
    assert(value.commands.every(row => ['GetLauncherBootstrapSnapshot', 'RefreshDisplays', 'IdentifyDisplay', 'CommitLanguage', 'CreateSession', 'OpenSession'].includes(row.type)));
    assert.equal(journal().filter(row => row.event === 'createCommand').length, 2);
    assert(journal().filter(row => row.event === 'createCommand').every(row => row.result.status === 'Accepted'));
  });
  await check('Clean actual Host/Coordinator shutdown after all owned child processes exit', async () => {
    await command(22); await noRole(client, replacement.incarnation); await finalShutdown();
    assert.equal(journal().find(row => row.event === 'cleanShutdown').launchCount, 2);
  });
  await check('Observed browser/runtime errors and console warnings are zero', async () => { assert.deepEqual(errors, []); assert.deepEqual(warnings, []); });
} catch (error) { report.failure = error.stack; process.exitCode = 1; }
finally {
  for (const client of clients) client.socket.close();
  report.cleanup = {};
  if (child) {
    if (child.exitCode === null && child.signalCode === null) {
      try {
        await command(29); // Ordinary-close every journalled test-owned role, including pending/untracked peers.
        await finalShutdown();
      } catch (error) {
        report.cleanup.failure = error.message; process.exitCode = 1;
        // Preserve the failure and its live owned PIDs for diagnosis. A forced
        // process kill must not turn a broken shutdown path into a passing run.
        child.unref(); child.stdout.destroy(); child.stderr.destroy();
      }
    }
    report.cleanup.fixturePid = child.pid; report.cleanup.exitCode = child.exitCode;
    report.cleanup.normal = child.exitCode === 0; report.cleanup.ownedPeerPids = peerRows().map(row => row.pid);
    report.cleanup.preferencesIsolated = journal().find(row => row.event === 'harnessReady')?.preferenceFile?.includes('a6-preferences.ini') === true;
    await fs.writeFile(path.join(evidence, 'native-host.log'), child.output);
  }
  report.finished = new Date().toISOString();
  report.pass = checks.length > 0 && checks.every(row => row.pass) && !report.failure && report.cleanup.normal === true && !report.cleanup.failure;
  if (!report.pass) process.exitCode = 1;
  await fs.writeFile(path.join(evidence, 'native-create-open.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ pass: report.pass, checks: checks.length, failed: checks.filter(row => !row.pass).map(row => ({ name: row.name, error: row.error?.split('\n')[0] })), errors: errors.length, warnings: warnings.length, evidence, failure: report.failure?.split('\n')[0] }, null, 2));
}
