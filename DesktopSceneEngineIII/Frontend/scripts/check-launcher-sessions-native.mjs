// A5: real Coordinator + production WebView host + same-image independent
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
const executable = path.resolve(argument('--executable') ?? path.join(repository, 'build/bin/Debug/DesktopSceneEngine_LauncherSessionLifecycleTest.exe'));
const port = Number(argument('--port') ?? 9397);
const evidence = path.join(repository, 'DesktopSceneEngineIII/evidence/phase-ii-a5', `native-sessions-${new Date().toISOString().replace(/[:.]/g, '-')}`);
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
const peerRows = () => journal().filter(value => value.event === 'hostPeerReady');
const projections = () => journal().filter(value => value.event === 'projection').map(value => value.payload);
async function command(value) {
  assert(child && child.exitCode === null && child.signalCode === null, 'Only owned live fixture accepts test command');
  const harness = journal().find(value => value.event === 'harnessReady');
  assert(harness, 'Real test-only native HWND harness');
  const previous = journal().filter(row => row.event === 'harnessCommand' && row.command === value).length;
  const posted = powershell(`Add-Type -TypeDefinition @'\nusing System;using System.Runtime.InteropServices;public static class A5Harness { [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);[DllImport("user32.dll")] static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr a,IntPtr b); public static bool Invoke(long handle,uint expected,int command) { uint pid;GetWindowThreadProcessId(new IntPtr(handle),out pid);return pid==expected&&PostMessage(new IntPtr(handle),0x8000+185,new IntPtr(command),IntPtr.Zero);}}\n'@\n[A5Harness]::Invoke([long]${quote(harness.hwnd)},[uint32]${child.pid},[int]${value})`);
  assert.equal(posted, 'True', 'Command is posted only to the owned PID-verified harness HWND');
  await until(`native harness command ${value} evaluated`, () => journal().filter(row => row.event === 'harnessCommand' && row.command === value).length > previous || (value === 15 && child.exitCode === 0));
}
async function target(previousGeneration) {
  try {
    const result = await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(700) });
    return (await result.json()).find(value => value.type === 'page' && value.url.startsWith('https://launcher.dse.local/')
      && (!previousGeneration || new URL(value.url).searchParams.get('generation') !== previousGeneration));
  } catch { return undefined; }
}
const observer = `(()=>{if(window.__a5Observer)return;window.__a5Observer=true;window.__a5Incoming=[];window.__a5Outgoing=[];chrome.webview.addEventListener('message',event=>window.__a5Incoming.push(event.data));const original=chrome.webview.postMessage.bind(chrome.webview);chrome.webview.postMessage=message=>{window.__a5Outgoing.push(JSON.parse(JSON.stringify(message)));return original(message)}})()`;
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
  return client.evaluate(`(()=>{const root=document.querySelector('.production-launcher'),detail=document.querySelector('.detail-context');const record=element=>({incarnation:element.dataset.sessionIncarnation??'',boundDisplayId:element.dataset.boundDisplayId??'',control:element.dataset.sessionControlConnected??'',close:element.dataset.sessionCloseResult??'',status:element.querySelector('[data-session-status]')?.dataset.sessionStatus??'',resolution:element.querySelector('[data-session-resolution]')?.dataset.sessionResolution??'',controlUnavailable:!!element.querySelector('[data-session-control="unavailable"]')});return {epoch:root?.dataset.coordinatorEpoch,revision:Number(root?.dataset.sessionRevision),bootstrap:root?.dataset.bootstrapState,generation:new URL(location.href).searchParams.get('generation'),detail:detail?record(detail):null,records:[...document.querySelectorAll('.production-session-record')].map(record),cards:[...document.querySelectorAll('.display-slide:not([aria-hidden="true"])')].map(element=>({displayId:element.querySelector('[data-display-id]')?.dataset.displayId,status:element.querySelector('[data-session-status]')?.dataset.sessionStatus})),states:[...document.querySelectorAll('[data-session-status]')].map(element=>element.dataset.sessionStatus),commands:window.__a5Outgoing??[],sessionEvents:(window.__a5Incoming??[]).filter(value=>value.kind==='event'&&value.type==='LauncherSessionsChanged'),text:document.body.innerText}})()`);
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
  const previous = await client.evaluate(`(window.__a5Incoming??[]).filter(value=>value.kind==='result'&&value.type==='RefreshDisplays').length`);
  assert(await client.evaluate(`(()=>{const button=document.querySelector('[data-refresh-displays]');if(!button||button.disabled)return false;button.click();return true})()`), 'Existing real Refresh button');
  await until('real RefreshDisplays result', () => client.evaluate(`(window.__a5Incoming??[]).filter(value=>value.kind==='result'&&value.type==='RefreshDisplays').length>${previous}`));
}
async function finalShutdown() {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  await command(15);
  await until('owned real Coordinator fixture clean exit', () => child.exitCode !== null);
  assert.equal(child.exitCode, 0, child.output);
  const clean = journal().find(row => row.event === 'cleanShutdown');
  assert(clean && clean.sessionCount === 0, 'No native registered Session left at final shutdown');
}

try {
  await check('Private test fixture and CDP port available without real user preference access', async () => {
    await fs.access(executable);
    await new Promise((resolve, reject) => {
      const socket = net.connect({ host: '127.0.0.1', port });
      socket.once('connect', () => { socket.destroy(); reject(new Error(`Private debug port ${port} is occupied`)); }); socket.once('error', resolve);
    });
    child = spawn(executable, ['--host'], { cwd: path.dirname(executable), windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
      env: { ...process.env, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${port} --remote-allow-origins=http://127.0.0.1:${port}`, WEBVIEW2_USER_DATA_FOLDER: path.join(evidence, 'profile') } });
    child.output = ''; child.stdout.on('data', value => { child.output += value; }); child.stderr.on('data', value => { child.output += value; }); child.on('error', error => { child.output += error.stack; });
    await until('isolated real Coordinator test harness ready', () => journal().find(row => row.event === 'harnessReady') || child.exitCode !== null);
    assert.equal(child.exitCode, null, child.output);
    report.harness = journal().find(row => row.event === 'harnessReady');
  });
  let client = await attach();
  await check('Empty real registry says no registered Session and never AVAILABLE/STARTING', async () => {
    const value = await facts(client);
    assert.equal(value.bootstrap, 'ready'); assert(Number.isSafeInteger(value.revision)); assert(value.epoch);
    assert(value.states.includes('noRegisteredSession')); assert(!value.states.some(state => ['AVAILABLE', 'STARTING'].includes(state)));
    assert.equal(value.detail.incarnation, ''); return value;
  });
  let first;
  await check('Same-image actual Session joins and React updates live without reload or Display Refresh', async () => {
    const previous = await facts(client);
    await command(10); first = peerRows().at(-1);
    const value = await hasRole(client, first.incarnation, { status: 'ACTIVE', control: 'true' });
    assert.equal(value.epoch, previous.epoch); assert(value.revision > previous.revision);
    assert.equal(value.generation, previous.generation);
    assert(!value.commands.some(command => command.type === 'RefreshDisplays'));
    assert(value.sessionEvents.some(event => event.payload.registeredSessions.some(role => role.incarnation === first.incarnation && role.boundDisplayId === first.boundDisplayId)));
    return { peer: first, role: value.role, epoch: value.epoch, revision: value.revision };
  });
  await check('React reload reconstructs same native Session incarnation under a fresh page generation', async () => {
    const previous = await facts(client);
    await client.send('Page.reload', { ignoreCache: true }); await ready(client, previous.generation);
    const value = await hasRole(client, first.incarnation, { status: 'ACTIVE', control: 'true' });
    assert.equal(value.epoch, previous.epoch); assert.equal(peerRows().length, 1, 'Reload did not launch another Session');
    assert.notEqual(value.generation, previous.generation); return { previous: previous.generation, current: value.generation, incarnation: value.role.incarnation };
  });
  await check('Real control-channel stop retains same living incarnation and ACTIVE display semantics', async () => {
    await command(12);
    const value = await hasRole(client, first.incarnation, { status: 'ACTIVE', control: 'false', controlUnavailable: true });
    assert(!value.states.includes('DISCONNECTED')); assert.equal(value.role.close, ''); return value.role;
  });
  await check('Display Refresh leaves real registered Session authority unchanged', async () => {
    const previous = await facts(client); await refresh(client);
    const value = await hasRole(client, first.incarnation, { status: 'ACTIVE', control: 'false' });
    assert.equal(value.revision, previous.revision); assert.equal(value.epoch, previous.epoch); return value.role;
  });
  await check('Coordinator retires/recreates real Launcher while independent Session survives', async () => {
    const previous = await facts(client); await command(14);
    await until('second real Coordinator-owned host creation', () => hosts().length === 2);
    client.socket.close(); client = await attach(previous.generation);
    const value = await hasRole(client, first.incarnation, { status: 'ACTIVE', control: 'false' });
    assert.notEqual(value.generation, previous.generation); assert.equal(value.epoch, previous.epoch);
    assert.equal(peerRows().length, 1); return { hosts: hosts(), incarnation: value.role.incarnation };
  });
  await check('Actual ordinary child Close removes exact Session after verified death without ghost CLOSING', async () => {
    await command(11); const value = await noRole(client, first.incarnation);
    assert(projections().some(snapshot => snapshot.registeredSessions.length === 0));
    assert(!/CRASHED|RECENTLY CLOSED/.test(value.text)); assert(!value.states.includes('CLOSING')); return { revision: value.revision, states: value.states };
  });
  let replacement;
  await check('New actual Session on same exact Display replaces only the incarnation', async () => {
    await command(10); replacement = peerRows().at(-1);
    assert.notEqual(replacement.incarnation, first.incarnation); assert.equal(replacement.boundDisplayId, first.boundDisplayId);
    const value = await hasRole(client, replacement.incarnation, { status: 'ACTIVE', control: 'true' });
    assert(![value.detail, ...value.records].some(role => role?.incarnation === first.incarnation)); return replacement;
  });
  await check('Successful controlled catalog loss derives narrow DISCONNECTED without removing Session', async () => {
    const previous = await facts(client); await command(17); await refresh(client);
    const value = await hasRole(client, replacement.incarnation, { status: 'DISCONNECTED', control: 'true' });
    assert.equal(value.revision, previous.revision); assert.equal(value.role.boundDisplayId, replacement.boundDisplayId);
    return { role: value.role, nativeProjection: projections().at(-1), environment: 'controlled synthetic Display catalog, not physical unplug' };
  });
  await check('Exact controlled Display return restores ACTIVE with same Session incarnation', async () => {
    await command(18); await refresh(client);
    await until('returned exact Display card derives ACTIVE without changing selection', async () =>
      (await facts(client)).cards.some(card => card.displayId === replacement.boundDisplayId && card.status === 'ACTIVE'));
    await selectDisplay(client, replacement.boundDisplayId);
    const value = await hasRole(client, replacement.incarnation, { status: 'ACTIVE', control: 'true' }); return value.role;
  });
  await check('Catalog ApiFailure yields unknown resolution without false DISCONNECTED', async () => {
    const previous = await facts(client); await command(19); await refresh(client);
    const value = await hasRole(client, replacement.incarnation, { status: 'REGISTERED', resolution: 'unknown', control: 'true' });
    assert.equal(value.revision, previous.revision); assert(!value.states.includes('DISCONNECTED'));
    await command(18); await refresh(client); await hasRole(client, replacement.incarnation, { status: 'ACTIVE' }); return value.role;
  });
  await check('Killed owned independent process is removed through real verified native death observation', async () => {
    await command(13); const value = await noRole(client, replacement.incarnation);
    assert(!value.states.some(state => ['CRASHED', 'DEAD', 'CLOSING', 'AVAILABLE', 'STARTING'].includes(state)));
    assert(projections().at(-1).registeredSessions.length === 0); return { revision: value.revision, states: value.states };
  });
  await check('Passive A5 lifecycle scenario does not issue Create/Open commands', async () => {
    const value = await facts(client);
    assert(value.commands.every(row => ['GetLauncherBootstrapSnapshot', 'RefreshDisplays', 'IdentifyDisplay', 'CommitLanguage'].includes(row.type)));
    for (const snapshot of projections()) {
      assert(snapshot.coordinatorEpoch === value.epoch); assert(Number.isSafeInteger(snapshot.sessionRevision));
      const ids = snapshot.registeredSessions.map(role => role.boundDisplayId), incarnations = snapshot.registeredSessions.map(role => role.incarnation);
      assert.equal(new Set(ids).size, ids.length); assert.equal(new Set(incarnations).size, incarnations.length);
    }
  });
  await check('Real host and owned peer processes finish with zero registered roles', async () => { await finalShutdown(); return journal().find(row => row.event === 'cleanShutdown'); });
  await check('Observed browser/runtime errors and console warnings are zero', async () => { assert.deepEqual(errors, []); assert.deepEqual(warnings, []); });
} catch (error) { report.failure = error.stack; process.exitCode = 1; }
finally {
  for (const client of clients) client.socket.close();
  report.cleanup = {};
  if (child) {
    if (child.exitCode === null && child.signalCode === null) {
      try {
        const last = projections().at(-1);
        if (last?.registeredSessions.length) await command(13);
        await finalShutdown();
      } catch (error) {
        report.cleanup.failure = error.message;
        // Only this spawned test fixture is terminated; unrelated native apps are
        // never selected or killed. Any forced cleanup keeps this run failed.
        child.kill(); process.exitCode = 1;
      }
    }
    report.cleanup.fixturePid = child.pid; report.cleanup.exitCode = child.exitCode;
    report.cleanup.normal = child.exitCode === 0;
    report.cleanup.ownedPeerPids = peerRows().map(row => row.pid);
    report.cleanup.preferencesIsolated = journal().find(row => row.event === 'harnessReady')?.preferenceFile?.includes('a5-preferences.ini') === true;
    await fs.writeFile(path.join(evidence, 'native-host.log'), child.output);
  }
  report.finished = new Date().toISOString();
  report.pass = checks.length > 0 && checks.every(row => row.pass) && !report.failure && report.cleanup.normal === true && !report.cleanup.failure;
  if (!report.pass) process.exitCode = 1;
  await fs.writeFile(path.join(evidence, 'native-sessions.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ pass: report.pass, checks: checks.length, failed: checks.filter(row => !row.pass).map(row => ({ name: row.name, error: row.error?.split('\n')[0] })), errors: errors.length, warnings: warnings.length, evidence, failure: report.failure?.split('\n')[0] }, null, 2));
}
