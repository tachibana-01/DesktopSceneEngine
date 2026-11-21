import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

// Mounted production React test. The fixture transport is injected only into
// this owned test document; production has no fixture, command or timer switch.
const origin = 'http://127.0.0.1:5173', debuggerOrigin = 'http://127.0.0.1:9393';
const cache = new URL('../node_modules/.cache/launcher-create-open-a6/dom/', import.meta.url); mkdirSync(cache, { recursive: true });
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const generation = '24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const epoch = 'dba10904-5b14-43f0-995b-17bc99181d43', otherEpoch = 'd7951708-aa40-497e-9160-37a96e1063e3';
const incarnation = 'ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2', replacement = 'b902bc98-e64a-43e0-ae66-16e257e5cd5b';
const rows = ['A', 'B', 'Weak'].map((name, index) => ({ id: 'exact opaque ' + name, identityKind: index === 2 ? 'SourceDeviceName' : 'TargetDevicePath', persistentOwnerCapable: index !== 2,
  friendlyName: 'QA Panel ' + name, sourceDeviceName: 'QA_SOURCE_' + name, desktopX: index * 1920, desktopY: 0,
  pixelWidth: 1920, pixelHeight: 1080, primary: index === 0, displayNumber: index + 1 }));
const catalog = (displays = rows, status = 'Success') => ({ status, displays });
const role = (patch = {}) => ({ boundDisplayId: rows[0].id, incarnation, controlConnected: true, ...patch });
const operationId = '6812eb13-67c4-4fc1-8bae-0216f2472645';
const operation = (patch = {}) => ({ displayId: rows[0].id, operationId, state: 'Pending', ...patch });
const sessions = (revision, roles = [], operations = [], owner = epoch) => ({ coordinatorEpoch: owner, sessionRevision: revision,
  registeredSessionsScope: 'verified-registered-only', createOperations: operations, registeredSessions: roles });
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
  async function emit(projection, patch = {}) { await evaluate('window.a6Qa.emit(' + JSON.stringify(projection) + ',' + JSON.stringify(patch) + ')'); await rest(); }
  async function refresh(value) { await evaluate('window.a6Qa.catalog=' + JSON.stringify(value)); await evaluate('document.querySelector("[data-refresh-displays]").click()'); await wait('document.querySelector(".production-launcher")?.dataset.refreshState !== "pending"'); await rest(); }
  await Promise.all([send('Runtime.enable'), send('Log.enable'), send('Page.enable'), send('Network.enable')]); await send('Network.setCacheDisabled', { cacheDisabled: true });
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  let injection;
  async function install(value=snapshot(), productShell=false) {
    if(injection) await send('Page.removeScriptToEvaluateOnNewDocument',{identifier:injection.identifier});
    injection=await send('Page.addScriptToEvaluateOnNewDocument',{source:`(() => {
      const listeners=new Set(),sent=[];const initial=${JSON.stringify(value)};
      const receive=data=>listeners.forEach(listener=>listener({data}));
      window.a6Qa={sent,initial,catalog:initial.displayEnumeration,receive,
       emit(payload,patch={}){receive({kind:'event',version:1,type:'LauncherSessionsChanged',generation:${JSON.stringify(generation)},payload,...patch});},
       reply(payload,command=sent.at(-1),patch={}){receive({kind:'result',version:1,requestId:command.requestId,type:command.type,generation:command.generation,ok:true,payload,...patch});},
       language(effective,revision){receive({kind:'event',version:1,type:'LauncherLanguageChanged',generation:${JSON.stringify(generation)},payload:{readStatus:'Success',committed:effective,effective,revision}});}};
      window.__dseProductShell=${JSON.stringify(productShell)};
      window.chrome??={};window.chrome.webview={postMessage(command){sent.push(command);let payload;
       if(command.kind!=='command')return;
       if(command.type==='CreateSession'||command.type==='OpenSession')return;
       if(command.type==='GetLauncherBootstrapSnapshot')payload=window.a6Qa.initial;
       else if(command.type==='RefreshDisplays')payload=window.a6Qa.catalog;
       else throw new Error('Unexpected command '+command.type);
       queueMicrotask(()=>window.a6Qa.reply(payload,command));},addEventListener(t,l){listeners.add(l);},removeEventListener(t,l){listeners.delete(l);}};
    })();`});
  }
  async function load(value=snapshot(), productShell=false) { await install(value,productShell);await send('Page.navigate',{url:origin+'/?mode=production&generation='+generation});await wait('document.querySelector(".production-launcher")?.dataset.bootstrapState==="ready"');await rest(); }
  async function select(index) { await evaluate('document.querySelector('+JSON.stringify('[aria-label="Go to '+rows[index].friendlyName+'"]')+').click()');await rest(); }
  async function action() {return evaluate(`(() => { const n=document.querySelector('.detail-context [data-session-action]');return {kind:n?.dataset.sessionAction,disabled:n?.disabled,busy:n?.getAttribute('aria-busy'),displayId:n?.dataset.actionDisplayId,incarnation:n?.dataset.actionIncarnation,status:document.querySelector('.detail-context [data-session-status]')?.dataset.sessionStatus};})()`);}
  async function click() {await evaluate('document.querySelector(".detail-context [data-session-action]").click()');await rest();}
  async function reply(payload,patch={}) {await evaluate('window.a6Qa.reply('+JSON.stringify(payload)+',undefined,'+JSON.stringify(patch)+')');await rest();}
  const create=(status='Accepted',patch={})=>({coordinatorEpoch:epoch,displayId:rows[0].id,status,...(['Accepted','AlreadyPending','LaunchFailed'].includes(status)?{operationId}:{}),...(status==='AlreadyRegistered'?{incarnation}:{}),...patch});
  const opened=(status='ActivationRequested',patch={})=>({coordinatorEpoch:epoch,incarnation,status,...patch});
  const sent=()=>evaluate('window.a6Qa.sent');
  await load();
  await check('Owner-capable absence presents explicit Create and neutral registration, never AVAILABLE',async()=>{
    const a=await action();assert.equal(a.kind,'Create');assert.equal(a.disabled,false);assert.equal(a.displayId,rows[0].id);assert.equal(a.status,'noRegisteredSession');
    assert.equal(await evaluate('/AVAILABLE|STARTING|CRASHED/.test(document.body.textContent)'),false);
  });
  await check('Weak identity keeps disabled Create without Identify promotion',async()=>{
    await select(2);const a=await action();assert.equal(a.kind,'Create');assert.equal(a.disabled,true);assert.equal(a.displayId,rows[2].id);await click();assert.equal((await sent()).length,1);await select(0);
  });
  await check('Click alone is transport pending, no STARTING, and exact target cannot double-submit',async()=>{
    await click();const a=await action();assert.equal(a.status,'noRegisteredSession');assert.equal(a.disabled,true);assert.equal(a.busy,'true');
    await click();const commands=(await sent()).filter(c=>c.type==='CreateSession');assert.equal(commands.length,1);assert.deepEqual(commands[0].payload,{coordinatorEpoch:epoch,displayId:rows[0].id});
    assert.equal(await evaluate('document.querySelector("[data-session-status=STARTING]")!==null'),false);
  });
  await check('Native Pending supplies STARTING; Accepted result does not complete it or insert a role',async()=>{
    await emit(sessions(1,[],[operation()]));await reply(create());const a=await action();assert.equal(a.status,'STARTING');assert.equal(a.disabled,true);assert.equal(a.incarnation,'');
    assert.equal(await evaluate('document.querySelector("[data-session-status=ACTIVE]")!==null'),false);
  });
  await check('Committed-language change translates native STARTING without command replay',async()=>{
    await evaluate('window.a6Qa.language("zh_CN",1)');await rest();assert.equal((await action()).status,'STARTING');assert(await evaluate('document.querySelector(".detail-context").textContent.includes("正在启动")'));
    assert.equal((await sent()).filter(c=>c.type==='CreateSession').length,1);await evaluate('window.a6Qa.language("en",2)');await rest();
  });
  await check('Fresh page bootstrap restores pending operation without replaying Create',async()=>{
    await load(snapshot(sessions(2,[],[operation()])));assert.equal((await action()).status,'STARTING');assert.equal((await action()).disabled,true);
    assert.deepEqual((await sent()).map(c=>c.type),['GetLauncherBootstrapSnapshot']);
  });
  await check('Authoritative Join replaces STARTING with exact registered ACTIVE/Open',async()=>{
    await emit(sessions(3,[role()],[operation({state:'Registered',incarnation})]));const a=await action();assert.equal(a.status,'ACTIVE');assert.equal(a.kind,'Open');assert.equal(a.incarnation,incarnation);assert.equal(a.disabled,false);
    assert.equal(await evaluate('document.querySelector("[data-session-status=STARTING]")!==null'),false);
  });
  await check('Open sends exact displayed incarnation and explicit result leaves lifecycle unchanged',async()=>{
    await click();assert.equal((await action()).disabled,true);await click();const commands=(await sent()).filter(c=>c.type==='OpenSession');assert.equal(commands.length,1);assert.deepEqual(commands[0].payload,{coordinatorEpoch:epoch,incarnation});
    await reply(opened());assert.equal((await action()).status,'ACTIVE');assert.equal((await action()).incarnation,incarnation);assert.equal((await action()).disabled,false);
    assert.equal(await evaluate('/opened successfully|foreground guaranteed/i.test(document.body.textContent)'),false);
  });
  await check('Stale Open outcome never retargets replacement or creates a new Session',async()=>{
    await click();await emit(sessions(4,[role({incarnation:replacement})]));await reply(opened('RoleGone'));assert.equal((await action()).incarnation,replacement);assert.equal((await action()).status,'ACTIVE');
    assert.equal((await sent()).filter(c=>c.type==='CreateSession').length,0);assert.equal(await evaluate('document.body.textContent.includes("This workspace is no longer available.")'),false);
  });
  await check('No-role removal clears ACTIVE even while terminal Registered receipt remains',async()=>{
    await emit(sessions(5,[],[operation({state:'Registered',incarnation:replacement})]));const a=await action();assert.equal(a.status,'noRegisteredSession');assert.equal(a.kind,'Create');assert.equal(a.disabled,false);
    assert.equal(await evaluate('document.querySelector("[data-session-status=ACTIVE]")!==null'),false);
  });
  await check('Native launch failure and uncertain receipts give truthful feedback without fake readiness',async()=>{
    await click();await reply(create('LaunchFailed'));assert(await evaluate(`document.body.textContent.includes("Couldn’t open a workspace.")`));assert.equal((await action()).status,'noRegisteredSession');
    await load(snapshot(sessions(6,[],[operation({state:'Uncertain'})])));assert(await evaluate('document.body.textContent.includes("Check whether a workspace opened before trying again.")'));assert.equal((await action()).status,'noRegisteredSession');assert.equal((await action()).disabled,false);
  });
  await check('Transport failure is not a native launch outcome and does not manufacture Session state',async()=>{
    await load();await click();await reply(undefined,{ok:false,error:'isolated fixture transport failed'});assert(await evaluate('document.body.textContent.includes("Opening the workspace didn’t finish.")'));assert.equal((await action()).status,'noRegisteredSession');assert.equal((await action()).disabled,false);
  });
  await check('Failure feedback stays on requested exact Display after selection change',async()=>{
    await load();await click();await select(1);await reply(create('LaunchFailed'));assert.equal((await action()).displayId,rows[1].id);assert.equal(await evaluate(`document.body.textContent.includes("Couldn’t open a workspace.")`),false);
  });
  await check('Display loss keeps exact registered Session and management Open on retained row',async()=>{
    await load(snapshot(sessions(1,[role()])));await refresh(catalog([rows[1]]));assert(await evaluate('document.querySelector(".production-session-record [data-session-status=DISCONNECTED]")!==null'));
    const a=await evaluate('(() => {const n=document.querySelector(".production-session-record [data-session-action=Open]");return {disabled:n?.disabled,incarnation:n?.dataset.actionIncarnation};})()');assert.equal(a.disabled,false);assert.equal(a.incarnation,incarnation);
  });
  await check('Control loss and native CLOSING independently disable Open without removing identity',async()=>{
    await load(snapshot(sessions(1,[role({controlConnected:false})])));assert.equal((await action()).kind,'Open');assert.equal((await action()).disabled,true);assert.equal((await action()).incarnation,incarnation);
    await emit(sessions(2,[role({closeOperation:{generation:epoch,request:'1',result:'Pending'}})]));assert.equal((await action()).status,'CLOSING');assert.equal((await action()).disabled,true);
  });
  await check('Stale Coordinator result fails closed without auto Create replay',async()=>{
    await load();await click();await reply(create('StaleCoordinator',{coordinatorEpoch:otherEpoch}));await wait('document.querySelector(".production-launcher")?.dataset.bootstrapState==="failed"');assert.equal((await sent()).filter(c=>c.type==='CreateSession').length,1);assert.equal(await evaluate('document.querySelector("[data-session-action]")!==null'),false);
  });
  await check('Narrow approved layout has no horizontal overflow or browser/runtime/console issues',async()=>{
    await load();await send('Emulation.setDeviceMetricsOverride',{width:460,height:900,deviceScaleFactor:1,mobile:false});await rest();assert.equal(await evaluate('document.documentElement.scrollWidth>innerWidth+1'),false);assert.deepEqual(errors,[]);assert.deepEqual(warnings,[]);
  });
  await check('Normal Chinese Launcher fits the supported window sizes and exposes the entire action without masking overflow',async()=>{
    const value=snapshot();
    value.language={readStatus:'Success',committed:'zh_CN',effective:'zh_CN',revision:0};
    value.displayEnumeration.displays=[
      {...rows[0],friendlyName:'',sourceDeviceName:String.raw`\\.\DISPLAY1`,pixelWidth:2560,pixelHeight:1600},
      {...rows[1],friendlyName:'24G40E',sourceDeviceName:String.raw`\\.\DISPLAY2`},
    ];
    await load(value,true);
    for(const [width,height,deviceScaleFactor] of [[1120,760,1],[720,540,1],[1440,540,1],[1280,850,1.5]]) {
      await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor,mobile:false});await rest();
      const facts=await evaluate(`(()=>{
        const button=document.querySelector('.detail-context [data-session-action]');
        const rect=button.getBoundingClientRect();
        return {height:document.documentElement.scrollHeight, width:document.documentElement.scrollWidth,
          top:rect.top,bottom:rect.bottom,hit:button.contains(document.elementFromPoint(rect.x+rect.width/2,rect.y+rect.height/2)),
          overflow:[document.documentElement,document.body,document.querySelector('.production-launcher'),document.querySelector('.launcher-main')].map(node=>getComputedStyle(node).overflowY),
          text:document.querySelector('.production-launcher').innerText};
      })()`);
      assert(facts.height<=height && facts.width<=width && facts.top>=0 && facts.bottom<=height && facts.hit,
        JSON.stringify({width,height,facts}));
      assert(facts.overflow.every(value=>value!=='hidden'&&value!=='clip'));
      for (const label of ['显示器 1','24G40E','尚未打开','开始使用']) assert(facts.text.includes(label), label);
      assert(!/DISPLAY1|经过验证|持久会话归属|当前语言/.test(facts.text));
    }
  });
  await check('Frozen Hide is outside the caption group and only dispatches for the eligible exact page',async()=>{
    await load(snapshot(),true);
    const inspectHide=()=>evaluate(`(()=>{
      const hide=document.querySelector('.window-control--hide'), rect=hide.getBoundingClientRect();
      const min=document.querySelector('[data-chrome-action="minimize"]').getBoundingClientRect();
      const svg=hide.querySelector('svg');
      return {disabled:hide.disabled,size:[rect.width,rect.height],gap:min.left-rect.right,
        aligned:min.y===rect.y,grouped:hide.closest('.product-caption-controls')!==null,
        glyph:[svg.getAttribute('width'),svg.getAttribute('height'),svg.querySelector('path').getAttribute('d')]};
    })()`);
    assert.deepEqual(await inspectHide(),{disabled:true,size:[42,34],gap:20,aligned:true,grouped:false,glyph:['14','14','m3.5 5.75 4.5 4.5 4.5-4.5']});
    const notify=async (eligible, page=generation)=>{
      await evaluate('window.a6Qa.receive('+JSON.stringify({kind:'launcher-hide-state',version:1,generation:page,eligible})+')');
      await rest();
    };
    await notify(true,replacement);assert.equal((await inspectHide()).disabled,true);
    await notify(true);assert.equal((await inspectHide()).disabled,false);
    const clickHide=()=>evaluate('document.querySelector(".window-control--hide").click()');
    await clickHide();
    const hidePackets=async()=> (await sent()).filter(packet=>packet.kind==='launcher-hide-request');
    assert.deepEqual(await hidePackets(),[{kind:'launcher-hide-request',version:1,generation}]);
    await evaluate('document.querySelector("[data-settings-open]").click()');await rest();
    assert.equal((await inspectHide()).disabled,true);await clickHide();assert.equal((await hidePackets()).length,1);
    await load(snapshot(),true);assert.equal((await inspectHide()).disabled,true);
    await notify(true);await notify(false);assert.equal((await inspectHide()).disabled,true);
  });
  const output={pass:checks.every(c=>c.pass),scope:'Mounted real production React with owned CDP transport fixture; actual native proof is separate',checks,errors,warnings};
  writeFileSync(new URL('focused-checks.json',cache),JSON.stringify(output,null,2));console.log(JSON.stringify({pass:output.pass,total:checks.length,failed:checks.filter(c=>!c.pass),errors:errors.length,warnings:warnings.length},null,2));assert(output.pass,'A6 mounted checks must pass');
} finally {socket?.close();chrome.kill();}
