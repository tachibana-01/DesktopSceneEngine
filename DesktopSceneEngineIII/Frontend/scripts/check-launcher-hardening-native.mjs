// A7: real Coordinator + production WebView host + same-image independent
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
const executable = path.resolve(argument('--executable') ?? path.join(repository, 'build/bin/Debug/DesktopSceneEngine_LauncherHardeningLifecycleTest.exe'));
const port = Number(argument('--port') ?? 9407);
const phase = argument('--phase') ?? 'full';
assert(['full','late'].includes(phase),'Explicit proof filter is full or late; default full retains all repeated cases');
const evidence = path.join(repository, 'DesktopSceneEngineIII/evidence/phase-ii-a7', `native-hardening-${new Date().toISOString().replace(/[:.]/g, '-')}`);
await fs.mkdir(evidence, { recursive: true });
const checks = [], errors = [], warnings = [], clients = [];
const report = {
  started: new Date().toISOString(), executable, port, phase, checks, errors, warnings,
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
  console.log(JSON.stringify({event:'check',name,pass:true}));
  await fs.writeFile(path.join(evidence,'progress.json'),JSON.stringify({checks,report},null,2));
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
  const posted = powershell(`Add-Type -TypeDefinition @'\nusing System;using System.Runtime.InteropServices;public static class A7Harness { [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);[DllImport("user32.dll")] static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr a,IntPtr b); public static bool Invoke(long handle,uint expected,int command) { uint pid;GetWindowThreadProcessId(new IntPtr(handle),out pid);return pid==expected&&PostMessage(new IntPtr(handle),0x8000+187,new IntPtr(command),IntPtr.Zero);}}\n'@\n[A7Harness]::Invoke([long]${quote(harness.hwnd)},[uint32]${child.pid},[int]${value})`);
  assert.equal(posted, 'True', 'Command is posted only to the owned PID-verified harness HWND');
  await until(`native harness command ${value} evaluated`, () => journal().filter(row => row.event === 'harnessCommand' && row.command === value).length > previous || (value === 26 && child.exitCode === 0));
  return journal().filter(row => row.event === 'harnessCommand' && row.command === value).at(-1);
}
async function target(previousGeneration) {
  try {
    const result = await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(700) });
    return (await result.json()).find(value => value.type === 'page' && value.url.startsWith('https://launcher.dse.local/')
      && (!previousGeneration || new URL(value.url).searchParams.get('generation') !== previousGeneration));
  } catch { return undefined; }
}
const observer = `(()=>{
 if(window.__a7Observer)return;window.__a7Observer=true;window.__a7Incoming=[];window.__a7Outgoing=[];
 const transport=chrome.webview,originalAdd=transport.addEventListener.bind(transport),originalRemove=transport.removeEventListener.bind(transport);
 const metrics=window.__a7Metrics={beforeApp:!document.querySelector('.production-launcher'),adds:0,removes:0,active:0,invocations:{}};
 const wrappers=new Map();
 const key=value=>JSON.stringify([value.kind,value.type,value.requestId??null,value.payload?.sessionRevision??value.payload?.revision??null]);
 originalAdd('message',event=>window.__a7Incoming.push(JSON.parse(JSON.stringify(event.data))));
 transport.addEventListener=(type,callback)=>{if(type!=='message')return originalAdd(type,callback);if(wrappers.has(callback))return;const wrapped=event=>{const id=key(event.data);metrics.invocations[id]=(metrics.invocations[id]??0)+1;return typeof callback==='function'?callback(event):callback.handleEvent(event)};wrappers.set(callback,wrapped);metrics.adds++;metrics.active++;return originalAdd(type,wrapped)};
 transport.removeEventListener=(type,callback)=>{if(type!=='message')return originalRemove(type,callback);const wrapped=wrappers.get(callback);if(!wrapped)return;wrappers.delete(callback);metrics.removes++;metrics.active--;return originalRemove(type,wrapped)};
 const original=transport.postMessage.bind(transport);transport.postMessage=message=>{window.__a7Outgoing.push(JSON.parse(JSON.stringify(message)));return original(message)};
})()`;
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
  return client.evaluate(`(()=>{const root=document.querySelector('.production-launcher'),detail=document.querySelector('.detail-context');const record=element=>({incarnation:element.dataset.sessionIncarnation??'',boundDisplayId:element.dataset.boundDisplayId??'',control:element.dataset.sessionControlConnected??'',close:element.dataset.sessionCloseResult??'',status:element.querySelector('[data-session-status]')?.dataset.sessionStatus??'',resolution:element.querySelector('[data-session-resolution]')?.dataset.sessionResolution??'',controlUnavailable:!!element.querySelector('[data-session-control="unavailable"]')});return {epoch:root?.dataset.coordinatorEpoch,revision:Number(root?.dataset.sessionRevision),bootstrap:root?.dataset.bootstrapState,generation:new URL(location.href).searchParams.get('generation'),detail:detail?record(detail):null,records:[...document.querySelectorAll('.production-session-record')].map(record),cards:[...document.querySelectorAll('.display-slide:not([aria-hidden="true"])')].map(element=>({displayId:element.querySelector('[data-display-id]')?.dataset.displayId,status:element.querySelector('[data-session-status]')?.dataset.sessionStatus})),states:[...document.querySelectorAll('[data-session-status]')].map(element=>element.dataset.sessionStatus),commands:window.__a7Outgoing??[],sessionEvents:(window.__a7Incoming??[]).filter(value=>value.kind==='event'&&value.type==='LauncherSessionsChanged'),language:root?.dataset.language,committed:root?.dataset.committedLanguage,languageRevision:Number(root?.dataset.languageRevision),selectedId:root?.dataset.selectedDisplayId,operationId:detail?.dataset.createOperationId,operationState:detail?.dataset.createOperationState,metrics:window.__a7Metrics,incoming:window.__a7Incoming??[],text:document.body.innerText}})()`);
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
  const previous = await client.evaluate(`(window.__a7Incoming??[]).filter(value=>value.kind==='result'&&value.type==='RefreshDisplays').length`);
  assert(await client.evaluate(`(()=>{const button=document.querySelector('[data-refresh-displays]');if(!button||button.disabled)return false;button.click();return true})()`), 'Existing real Refresh button');
  await until('real RefreshDisplays result', () => client.evaluate(`(window.__a7Incoming??[]).filter(value=>value.kind==='result'&&value.type==='RefreshDisplays').length>${previous}`));
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
  const requestId = `a7_proof_${crypto.randomUUID().replaceAll('-', '')}`;
  await client.evaluate(`chrome.webview.postMessage(${JSON.stringify({ kind: 'command', version: 1, requestId, type, generation: generation ?? (await facts(client)).generation, payload })})`);
  return until(`native ${type} response`, () => client.evaluate(`(window.__a7Incoming??[]).find(value=>value.kind==='result'&&value.requestId===${JSON.stringify(requestId)})`));
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
  return JSON.parse(powershell(`Add-Type -TypeDefinition @'\nusing System;using System.Runtime.InteropServices;public static class A7WindowState{[DllImport("user32.dll")]public static extern bool IsIconic(IntPtr h);[DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);public static uint Pid(long h){uint p;GetWindowThreadProcessId(new IntPtr(h),out p);return p;}}\n'@\n$h=[long]${quote(peer.roleHwnd)}\nif([A7WindowState]::Pid($h)-ne[uint32]${peer.pid}){throw 'Owned peer HWND mismatch'}\n@{minimized=[A7WindowState]::IsIconic([IntPtr]$h);maximized=[A7WindowState]::IsZoomed([IntPtr]$h)}|ConvertTo-Json -Compress`));
}

report.reloadCycles=[];report.recreateCycles=[];report.pendingCycles=[];report.pages=[];report.deliveries=[];report.processSnapshots=[];report.lateCallbacks=[];report.pageReloadBoundaries=[];report.hostRetireBoundaries=[];
const counts=()=>command(60);
const commandFields=['refresh','identify','create','open','languagePublications','launchCount'];
function sameCommands(before,after){for(const key of commandFields)assert.equal(after[key],before[key],key+' must not replay');assert.equal(after.coordinatorPid,before.coordinatorPid);}
async function snapshot(client){const row=await request(client,'GetLauncherBootstrapSnapshot',{});assert.equal(row.ok,true);return row.payload;}
async function pageRecord(client,reason){const value=await facts(client);report.pages.push({reason,...value});if(value.metrics?.beforeApp){assert.equal(value.metrics.active,1,'Exactly one real current-page Bridge listener');const actual={};for(const row of value.incoming){const key=JSON.stringify([row.kind,row.type,row.requestId??null,row.payload?.sessionRevision??row.payload?.revision??null]);actual[key]=(actual[key]??0)+1;}assert.deepEqual(value.metrics.invocations,actual,'Every actual facade callback dispatch equals one native message, cumulative across page life');}return value;}
async function oneMutation(client,name,action){
  await snapshot(client);const start=journal().length;const incoming=(await facts(client)).incoming.length;
  await action();await snapshot(client);const value=await facts(client);
  const native=journal().slice(start).filter(row=>row.event==='projection'||row.event==='languageChange');
  const events=value.incoming.slice(incoming).filter(row=>row.kind==='event'&&['LauncherSessionsChanged','LauncherLanguageChanged'].includes(row.type));
  for(const row of native){const type=row.event==='projection'?'LauncherSessionsChanged':'LauncherLanguageChanged';const revision=row.payload.sessionRevision??row.payload.revision;const matching=events.filter(item=>item.type===type&&(item.payload.sessionRevision??item.payload.revision)===revision);assert.equal(matching.length,1,name+' native logical revision delivered exactly once');assert.deepEqual(matching[0].payload,row.payload);}
  assert.equal(events.length,native.length,name+' no extra event delivery');report.deliveries.push({name,native:native.map(row=>({type:row.event,revision:row.payload.sessionRevision??row.payload.revision})),events:events.length});
  return value;
}
async function reload(client){const old=await pageRecord(client,'before-reload');const before=await counts();await client.send('Page.reload',{ignoreCache:true});await ready(client,old.generation);const after=await counts();sameCommands(before,after);const value=await facts(client);assert.equal(value.epoch,old.epoch);assert.notEqual(value.generation,old.generation);assert(value.commands.every(row=>row.type==='GetLauncherBootstrapSnapshot'),'Reload emits only explicit bootstrap');return value;}
async function recreate(client,commandValue=25){const old=await pageRecord(client,'before-host-retirement'),before=await counts(),total=hosts().length;await command(commandValue);await until('new single Coordinator Host creation',()=>hosts().length===total+1);client.socket.close();const next=await attach(old.generation);const after=await counts();sameCommands(before,after);assert.equal((await facts(next)).epoch,old.epoch);return next;}
// Providers are synchronous production interfaces. The controlled nested-loop
// barrier keeps their work pending; WebView serializes DOM evaluation behind its
// current event handler. Request the lifetime transition, release that barrier,
// then await the replacement document. Waiting for new DOM first deadlocks the
// fixture and does not model a valid asynchronous native operation.
async function lateReload(client,old){await client.send('Page.reload',{ignoreCache:true});const boundary=await counts();assert.equal(boundary.heldHostAlive,true);assert.equal(boundary.activeProvider!=="",true);report.pageReloadBoundaries.push({oldGeneration:old.generation,currentGeneration:boundary.currentHostGeneration,nativeGenerationCrossedBeforeCompletion:boundary.currentHostGeneration!==old.generation,boundary});await command(50);await ready(client,old.generation);const value=await facts(client);assert.notEqual(value.generation,old.generation);assert.equal(value.epoch,old.epoch);assert(value.commands.every(row=>row.type==='GetLauncherBootstrapSnapshot'));return value;}
async function lateRecreate(client,old){const total=hosts().length;await command(25);await until('replacement Host factory ran before late completion',()=>hosts().length===total+1);const boundary=await counts();assert.equal(boundary.heldHostAlive,false,'Original native Host destroyed before provider completion');assert.equal(boundary.heldHostReady,false);report.hostRetireBoundaries.push({oldGeneration:old.generation,boundary});await command(50);client.socket.close();const next=await attach(old.generation);assert.equal((await facts(next)).epoch,old.epoch);return next;}

async function clickIdentify(client){assert(await client.evaluate("(()=>{const value=document.querySelector('[data-identify-display]');if(!value||value.disabled)return false;value.click();return true})()"));}
async function applyLanguage(client,language){assert(await client.evaluate("(()=>{const value=document.querySelector('[data-settings-open]');if(!value)return false;value.click();return true})()"));await until('production Settings is open',()=>client.evaluate("!!document.querySelector('[data-production-settings]')"));await client.evaluate('(()=>{const select=document.querySelector(\'[data-settings-language]\');select.value='+JSON.stringify(language)+';select.dispatchEvent(new Event(\'change\',{bubbles:true}));})()');assert(await client.evaluate("(()=>{const value=document.querySelector('[data-settings-apply]');if(!value||value.disabled)return false;value.click();return true})()"));}
async function closeSettings(client){await client.evaluate("document.querySelector('[data-settings-cancel]')?.click()");}
async function processSnapshot(label){const values=JSON.parse(powershell('$rows=@(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq '+quote(executable)+' -or ($_.Name -eq \'msedgewebview2.exe\' -and $_.CommandLine -like '+quote('*'+path.join(evidence,'profile')+'*')+') } | Select-Object ProcessId,ParentProcessId,ExecutablePath,CommandLine)\nConvertTo-Json -InputObject $rows -Depth 3 -Compress'));const rows=Array.isArray(values)?values:[values];report.processSnapshots.push({label,rows});return rows;}
async function pendingProvider(name){await until('controlled native '+name+' provider is in flight',()=>journal().filter(row=>row.event==='providerPending'&&row.provider===name).length>journal().filter(row=>row.event==='providerReleased'&&row.provider===name).length);}
async function providerCompleted(name){await until('controlled native '+name+' provider returned',()=>journal().filter(row=>row.event==='providerReleased'&&row.provider===name).length===journal().filter(row=>row.event==='providerPending'&&row.provider===name).length);}
function onlySingleCoordinator(rows){const coordinators=rows.filter(row=>row.ExecutablePath?.toLowerCase()===executable.toLowerCase()&&!row.CommandLine.includes('--display-session'));assert.equal(coordinators.length,1);assert.equal(coordinators[0].ProcessId,child.pid);}
try {
  await check('Isolated real Coordinator/Host fixture starts with private CDP and no production preference writes',async()=>{
    await fs.access(executable);await new Promise((resolve,reject)=>{const socket=net.connect({host:'127.0.0.1',port});socket.once('connect',()=>{socket.destroy();reject(new Error('Private CDP port occupied'))});socket.once('error',resolve)});
    child=spawn(executable,['--host'],{cwd:path.dirname(executable),windowsHide:true,stdio:['ignore','pipe','pipe'],env:{...process.env,WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:'--remote-debugging-port='+port+' --remote-allow-origins=http://127.0.0.1:'+port,WEBVIEW2_USER_DATA_FOLDER:path.join(evidence,'profile')}});
    child.output='';child.stdout.on('data',value=>child.output+=value);child.stderr.on('data',value=>child.output+=value);child.on('error',error=>child.output+=error.stack);
    await until('real Coordinator test-control ready',()=>journal().some(row=>row.event==='harnessReady')||child.exitCode!==null);assert.equal(child.exitCode,null,child.output);report.harness=journal().find(row=>row.event==='harnessReady');
    onlySingleCoordinator(await processSnapshot('idle-before'));
  });
  let client=await attach();const targetId=report.harness.firstDisplayId;
  if(phase==='full'){
  await check('Idle retirement/recreation reads fresh catalog and language inside existing zero-role drain budget',async()=>{
    const old=await facts(client),before=await counts();client=await recreate(client,63);const current=await snapshot(client);
    assert(!current.displayEnumeration.displays.some(row=>row.id===targetId));assert.notEqual(current.language.effective,old.language);assert.equal(current.registeredSessions.length,0);assert.equal(current.coordinatorEpoch,old.epoch);sameCommands(before,await counts());
    await command(28);await refresh(client);await selectDisplay(client,targetId);
  });
  let first;
  await check('One real Create/Join produces the stable ACTIVE incarnation used for both repeat groups',async()=>{
    await command(20);await oneMutation(client,'initial-native-Create',()=>clickAction(client,'Create',targetId));await starting(client);
    await oneMutation(client,'initial-authenticated-Join',()=>command(21));first=peerRows().at(-1);await hasRole(client,first.incarnation,{status:'ACTIVE',control:'true'});
    const count=await counts();assert.equal(count.create,1);assert.equal(count.launchCount,1);report.stablePeer=first;
  });
  await check('20 real ACTIVE page reloads retain same epoch/incarnation and do not replay commands',async()=>{
    const initial=await counts();for(let cycle=1;cycle<=20;cycle++){
      const value=await reload(client);await hasRole(client,first.incarnation,{status:'ACTIVE',control:'true'});assert.equal(value.detail.incarnation,first.incarnation);assert.equal(value.operationState,'Registered');
      const count=await counts();sameCommands(initial,count);assert.equal(count.sessionCount,1);report.reloadCycles.push({cycle,generation:value.generation,epoch:value.epoch,incarnation:value.detail.incarnation,coordinatorPid:count.coordinatorPid,launchCount:count.launchCount});console.log(JSON.stringify({event:'reload',cycle,pass:true}));
    }onlySingleCoordinator(await processSnapshot('active-after-20-reloads'));
  });
  await check('20 real ACTIVE Host recreations retain one Coordinator and same Session; language updates stay logically once',async()=>{
    const initial=await counts();for(let cycle=1;cycle<=20;cycle++){
      client=await recreate(client);await hasRole(client,first.incarnation,{status:'ACTIVE',control:'true'});
      const before=await counts();const changed=await oneMutation(client,'recreate-'+cycle+'-language',()=>command(32));assert.equal(changed.language,(await snapshot(client)).language.effective);
      const after=await counts();assert.equal(after.languageChanges,before.languageChanges+1);sameCommands(initial,after);assert.equal(after.sessionCount,1);
      await oneMutation(client,'recreate-'+cycle+'-same-language-invalidation',()=>command(61));assert.equal((await counts()).languageChanges,after.languageChanges);
      report.recreateCycles.push({cycle,incarnation:first.incarnation,generation:(await facts(client)).generation,coordinatorPid:after.coordinatorPid,launchCount:after.launchCount,languageChanges:after.languageChanges});console.log(JSON.stringify({event:'recreate',cycle,pass:true}));
    }onlySingleCoordinator(await processSnapshot('active-after-20-recreates'));
  });
  await check('After repeated rebuilds one actual Refresh, Identify, Open and Language Apply each executes once',async()=>{
    const before=await counts();await refresh(client);await clickIdentify(client);await until('one Identify result',()=>client.evaluate("(window.__a7Incoming??[]).some(row=>row.kind==='result'&&row.type==='IdentifyDisplay')"));
    await clickAction(client,'Open',targetId);await until('one actual Open provider',()=>journal().filter(row=>row.event==='openCommand').length===before.open+1);
    const desired=(await facts(client)).language==='en'?'zh_CN':'en';await oneMutation(client,'one-explicit-Language-Apply',()=>applyLanguage(client,desired));await until('language commit settles',()=>client.evaluate("document.querySelector('[data-production-settings]')?.dataset.languagePending==='false'"));await closeSettings(client);
    const after=await counts();for(const key of ['refresh','identify','open','languagePublications'])assert.equal(after[key],before[key]+1,key+' executes once');assert.equal(after.create,before.create);assert.equal(after.launchCount,before.launchCount);report.singleActions={before,after};
  });
  await check('While Host absent external Session Join/death and fresh catalog/language remain native-owned',async()=>{
    await pageRecord(client,'before-absent-external-mutations');const old=await facts(client);await command(30);client.socket.close();await command(27);await command(32);await command(34);const secondary=peerRows().at(-1);assert.notEqual(secondary.incarnation,first.incarnation);await command(22);
    const before=await counts();assert.equal(before.sessionCount,1);await command(31);client=await attach(old.generation);const fresh=await snapshot(client);assert.equal(fresh.registeredSessions.length,1);assert.equal(fresh.registeredSessions[0].incarnation,first.incarnation);assert(!fresh.displayEnumeration.displays.some(row=>row.id===targetId));await hasRole(client,first.incarnation,{status:'DISCONNECTED'});
    assert.equal((await facts(client)).language,fresh.language.effective);await command(28);await refresh(client);await selectDisplay(client,targetId);await hasRole(client,first.incarnation,{status:'ACTIVE'});onlySingleCoordinator(await processSnapshot('active-after-absent-mutations'));
  });
  await check('One verified native death after 40 rebuilds is delivered once with no subscription accumulation',async()=>{
    await oneMutation(client,'stable-Session-final-ordinary-death',()=>command(22));await noRole(client,first.incarnation);assert.equal((await counts()).sessionCount,0);
  });
  await check('20 pending Create page/Host recovery cycles keep one operation and one launch, then real Join and death each deliver once',async()=>{
    for(let cycle=1;cycle<=20;cycle++){
      const before=await counts();await oneMutation(client,'pending-'+cycle+'-Create',()=>clickAction(client,'Create',targetId));const pending=await starting(client);const operationId=pending.operationId;
      assert(operationId);const accepted=await counts();assert.equal(accepted.create,before.create+1);assert.equal(accepted.launchCount,before.launchCount+1);
      const kind=cycle%2?'reload':'recreate';if(kind==='reload')await reload(client);else client=await recreate(client);
      const reconstructed=await starting(client);assert.equal(reconstructed.operationId,operationId);sameCommands(accepted,await counts());const native=await snapshot(client);assert.equal(native.createOperations.find(row=>row.displayId===targetId).operationId,operationId);
      await oneMutation(client,'pending-'+cycle+'-Join',()=>command(21));const peer=peerRows().at(-1);await hasRole(client,peer.incarnation,{status:'ACTIVE',control:'true'});assert.equal((await facts(client)).operationState,'Registered');
      await oneMutation(client,'pending-'+cycle+'-ordinary-death',()=>command(22));await noRole(client,peer.incarnation);const done=await counts();assert.equal(done.sessionCount,0);assert.equal(done.create,before.create+1);assert.equal(done.launchCount,before.launchCount+1);
      report.pendingCycles.push({cycle,kind,operationId,incarnation:peer.incarnation,pid:peer.pid,launchCount:done.launchCount});console.log(JSON.stringify({event:'pending-recovery',cycle,kind,pass:true}));
    }onlySingleCoordinator(await processSnapshot('idle-after-20-pending-recoveries'));
  });
  } // Explicit late-only filter starts here; default full executes every prefix repeat.
  await check('Refresh across requested page reload restores fresh catalog without replay or old-page result',async()=>{
    const old=await pageRecord(client,'before-late-Refresh');await command(40);const before=await counts();assert(await client.evaluate("(()=>{document.querySelector('[data-refresh-displays]').click();return true})()"));await pendingProvider('Refresh');await command(27);await lateReload(client,old);await until('held Refresh native completion',()=>journal().some(row=>row.event==='providerReleased'&&row.provider==='Refresh'));const value=await snapshot(client);assert(!value.displayEnumeration.displays.some(row=>row.id===targetId));assert.equal((await counts()).refresh,before.refresh+1);assert(!(await facts(client)).incoming.some(row=>row.kind==='result'&&row.type==='RefreshDisplays'));report.lateCallbacks.push({provider:'Refresh',boundary:'page reload requested before completion',safe:true});await command(28);await refresh(client);await selectDisplay(client,targetId);
  });

  await check('Refresh provider completes only after original Host destroyed; new Host retains fresh catalog',async()=>{
    const old=await pageRecord(client,'before-true-late-Refresh-retire');await command(40);const before=await counts();assert(await client.evaluate("(()=>{document.querySelector('[data-refresh-displays]').click();return true})()"));await pendingProvider('Refresh');await command(27);client=await lateRecreate(client,old);await providerCompleted('Refresh');const fresh=await snapshot(client);assert(!fresh.displayEnumeration.displays.some(row=>row.id===targetId));assert.equal((await counts()).refresh,before.refresh+1);assert(!(await facts(client)).incoming.some(row=>row.kind==='result'&&row.type==='RefreshDisplays'));report.lateCallbacks.push({provider:'Refresh',boundary:'Host destroyed before completion',safe:true});await command(28);await refresh(client);await selectDisplay(client,targetId);
  });
  await check('Identify across requested page reload does not replay or promote identity',async()=>{
    const old=await pageRecord(client,'before-late-Identify');await command(44);const before=await counts();await clickIdentify(client);await pendingProvider('Identify');await lateReload(client,old);await until('held Identify completion',()=>journal().some(row=>row.event==='providerReleased'&&row.provider==='Identify'));await snapshot(client);assert.equal((await counts()).identify,before.identify+1);assert(!(await facts(client)).incoming.some(row=>row.kind==='result'&&row.type==='IdentifyDisplay'));report.lateCallbacks.push({provider:'Identify',boundary:'page reload',safe:true,physicalOverlayClaim:false});
  });
  await check('Native committed language survives Host teardown while old-page commit feedback is fenced',async()=>{
    const old=await pageRecord(client,'before-late-Language');await command(41);const before=await counts(),desired=(await facts(client)).language==='en'?'zh_CN':'en';await applyLanguage(client,desired);await pendingProvider('Language');client=await lateRecreate(client,old);assert.equal((await facts(client)).language,desired);await until('held language publication completes',()=>journal().some(row=>row.event==='providerReleased'&&row.provider==='Language'));const native=await snapshot(client);assert.equal(native.language.committed,desired);assert.equal((await counts()).languagePublications,before.languagePublications+1);assert(!(await facts(client)).incoming.some(row=>row.kind==='result'&&row.type==='CommitLanguage'));report.lateCallbacks.push({provider:'Language',boundary:'Host teardown',safe:true});
  });
  let finalPeer;
  await check('Native pending Create outlives provider/Host teardown and no result is replayed into recreated page',async()=>{
    const old=await pageRecord(client,'before-late-Create');await command(20);await command(42);const before=await counts();await clickAction(client,'Create',targetId);await pendingProvider('Create');const operation=await until('native pending operation pushed while provider held',()=>projections().at(-1)?.createOperations.find(row=>row.displayId===targetId&&row.state==='Pending'));assert.equal(operation.state,'Pending');client=await lateRecreate(client,old);const current=await starting(client);assert.equal(current.operationId,operation.operationId);await until('held Create provider completion',()=>journal().some(row=>row.event==='providerReleased'&&row.provider==='Create'));await snapshot(client);assert(!(await facts(client)).incoming.some(row=>row.kind==='result'&&row.type==='CreateSession'));assert.equal((await counts()).launchCount,before.launchCount+1);await oneMutation(client,'late-Create-real-Join',()=>command(21));finalPeer=peerRows().at(-1);await hasRole(client,finalPeer.incarnation,{status:'ACTIVE'});report.lateCallbacks.push({provider:'Create',boundary:'Host teardown',safe:true,operationId:operation.operationId});
  });
  await check('Native exact Open activation outlives Host teardown but late result is fenced and never replays',async()=>{
    const old=await pageRecord(client,'before-late-Open');await command(43);const before=await counts();await clickAction(client,'Open',targetId);await pendingProvider('Open');client=await lateRecreate(client,old);await hasRole(client,finalPeer.incarnation,{status:'ACTIVE'});await until('held Open provider completion',()=>journal().some(row=>row.event==='providerReleased'&&row.provider==='Open'));await snapshot(client);const after=await counts();assert.equal(after.open,before.open+1);assert.equal(after.launchCount,before.launchCount);assert(!(await facts(client)).incoming.some(row=>row.kind==='result'&&row.type==='OpenSession'));report.lateCallbacks.push({provider:'Open',boundary:'Host teardown',safe:true});
  });
  await check('Every observed current-page callback is singly dispatched and native/Host final shutdown is clean',async()=>{
    await oneMutation(client,'last-pinned-Session-ordinary-death',()=>command(22));await noRole(client,finalPeer.incarnation);await pageRecord(client,'final-current-page');report.finalCounters=await counts();await finalShutdown();
    const clean=journal().find(row=>row.event==='cleanShutdown');assert.equal(clean.sessionCount,0);const expectedLaunches=phase==='full'?22:1;const expectedPeers=phase==='full'?23:1;assert.equal(clean.launchCount,expectedLaunches);assert.equal(clean.create,expectedLaunches);const peers=journal().find(row=>row.event==='peerJournal').rows;assert.equal(peers.filter(row=>row.event==='started').length,expectedPeers);assert.equal(peers.filter(row=>row.event==='cleanExit').length,expectedPeers);assert.equal(peers.filter(row=>row.event==='occupied').length,0);report.peerPids=[...new Set(peers.map(row=>row.pid))];report.peerJournalSummary={started:expectedPeers,cleanExit:expectedPeers,occupied:0};
  });
  await check('No actual browser/runtime errors or console warnings accumulate across all lifetimes',async()=>{assert.deepEqual(errors,[]);assert.deepEqual(warnings,[]);});
  await check('All owned native and private-profile WebView processes leave zero survivors without forced cleanup',async()=>{
    await until('owned process and private profile process tree empty',async()=>{const rows=await processSnapshot('final-cleanup-observation');return rows.length===0});report.processCleanup={remainingOwnedNative:0,remainingProfileWebView:0,forcedCleanup:false};
  });
} catch(error){report.failure=error.stack;process.exitCode=1;}
finally {
  for(const client of clients)client.socket.close();report.cleanup={};
  if(child){if(child.exitCode===null&&child.signalCode===null){try{await command(50);await command(29);await finalShutdown();}catch(error){report.cleanup.failure=error.message;process.exitCode=1;child.unref();child.stdout.destroy();child.stderr.destroy();}}
    report.cleanup.fixturePid=child.pid;report.cleanup.exitCode=child.exitCode;report.cleanup.normal=child.exitCode===0;report.cleanup.ownedPeerPids=peerRows().map(row=>row.pid);report.cleanup.preferencesIsolated=report.harness?.preferenceFile?.includes('a7-preferences.ini')===true;await fs.writeFile(path.join(evidence,'native-host.log'),child.output);
  }
  report.finished=new Date().toISOString();report.pass=checks.length>0&&checks.every(row=>row.pass)&&!report.failure&&report.cleanup.normal===true&&!report.cleanup.failure;report.counts={reload:report.reloadCycles.length,recreate:report.recreateCycles.length,pending:report.pendingCycles.length,lateCallbacks:report.lateCallbacks.length,logicalDeliveryChecks:report.deliveries.length,instrumentedPageLifetimes:report.pages.filter(row=>row.metrics?.beforeApp).length};if(!report.pass)process.exitCode=1;await fs.writeFile(path.join(evidence,'native-hardening.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({pass:report.pass,checks:checks.length,counts:report.counts,failed:checks.filter(row=>!row.pass).map(row=>({name:row.name,error:row.error?.split('\n')[0]})),errors:errors.length,warnings:warnings.length,evidence,failure:report.failure?.split('\n')[0]},null,2));
}
