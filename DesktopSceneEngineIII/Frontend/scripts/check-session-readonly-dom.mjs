import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {mkdirSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve,sep} from 'node:path';
import {fixturePreviewReply,fixtureRuntimeSample,generation,incarnation,ids,productionSessionUrl,snapshot} from './session-readonly-fixture.mjs';

const origin='http://127.0.0.1:9409',debug='http://127.0.0.1:9395';
const viewOnly=process.argv.includes('--view-deploy');
const cache=new URL('../node_modules/.cache/current-contract/session-dom/',import.meta.url);mkdirSync(cache,{recursive:true});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const profileRoot=mkdtempSync(join(tmpdir(),'dse-b1-frontend-'));
function cleanupProfile(){const target=resolve(profileRoot),base=resolve(tmpdir())+sep;if(!target.startsWith(base)||!target.slice(base.length).startsWith('dse-b1-frontend-'))throw new Error('Unsafe QA profile target');rmSync(target,{recursive:true,force:true,maxRetries:5,retryDelay:100});}

for(const url of [origin,debug+'/json/list'])await assert.rejects(fetch(url),'Private QA port must be free: '+url);
const vite=spawn(process.execPath,[fileURLToPath(new URL('../node_modules/vite/bin/vite.js',import.meta.url)),'--host','127.0.0.1','--port','9409','--strictPort'],{cwd:fileURLToPath(new URL('../',import.meta.url)),windowsHide:true,stdio:['ignore','pipe','pipe']});
let serverLog='';vite.stdout.on('data',b=>serverLog+=b);vite.stderr.on('data',b=>serverLog+=b);
const chrome=spawn(chromeExecutable,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-address=127.0.0.1','--remote-debugging-port=9395','--user-data-dir='+profileRoot,'about:blank'],{windowsHide:true,stdio:['ignore','ignore','pipe']});
let browserLog='';chrome.stderr.on('data',value=>browserLog+=value);
const checks=[],errors=[],warnings=[];let socket,send;
try{
 let pages;for(let n=0;n<100;n++){try{await fetch(origin);pages=await(await fetch(debug+'/json/list')).json();if(pages.some(p=>p.type==='page'))break;}catch{}await pause(100);}
 const page=pages?.find(p=>p.type==='page');assert(page,'Owned browser/server failed to start. '+serverLog+' Browser exit='+chrome.exitCode+' '+browserLog);
 socket=new WebSocket(page.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
 let id=0;const pending=new Map();socket.addEventListener('message',async e=>{const m=JSON.parse(typeof e.data==='string'?e.data:await e.data.text());
  if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails);
  if(m.method==='Runtime.consoleAPICalled'&&['error','warning'].includes(m.params.type))(m.params.type==='error'?errors:warnings).push(m.params);
  if(m.method==='Log.entryAdded'&&['error','warning'].includes(m.params.entry.level))(m.params.entry.level==='error'?errors:warnings).push(m.params.entry);
  const p=pending.get(m.id);if(p){pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result);}
 });
 send=(method,params={})=>new Promise((resolve,reject)=>{const key=++id;pending.set(key,{resolve,reject});socket.send(JSON.stringify({id:key,method,params}));});
 async function evaluate(expression){const r=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});assert(!r.exceptionDetails,JSON.stringify(r.exceptionDetails));return r.result.value;}
 async function wait(expression){for(let n=0;n<100;n++){if(await evaluate('Boolean('+expression+')'))return;await pause(50);}assert.fail('DOM wait: '+expression+' BODY='+await evaluate('document.body.innerText'));}
 async function rest(){await evaluate('new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))');}
 async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
 await Promise.all([send('Page.enable'),send('Runtime.enable'),send('Log.enable'),send('Network.enable')]);await send('Network.setCacheDisabled',{cacheDisabled:true});await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
 let injection;async function load(value=snapshot(),options={}){
  if(injection)await send('Page.removeScriptToEvaluateOnNewDocument',{identifier:injection.identifier});
  injection=await send('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{
   const listeners=new Set();const q=window.b1Qa={initial:${JSON.stringify(value)},sent:[],viewRequests:[],adds:0,removes:0,maxListeners:0,
    finishView(status,patch={}){const c=q.viewRequests.shift();for(const fn of listeners)fn({data:{kind:'result',version:1,requestId:c.requestId,type:c.type,generation:c.generation,sessionIncarnation:c.sessionIncarnation,ok:true,payload:{status},...patch}});},
    chromeState(patch={}){for(const fn of listeners)fn({data:{kind:'shell-chrome-state',version:1,generation:${JSON.stringify(generation)},state:{active:false,maximized:true,minimized:false,hovered:'maximize',pressed:null},...patch}});},
    hideState(patch={}){for(const fn of listeners)fn({data:{kind:'session-hide-state',version:1,generation:${JSON.stringify(generation)},sessionIncarnation:${JSON.stringify(incarnation)},eligible:true,...patch}});},
    event(patch={}){for(const fn of listeners)fn({data:{kind:'event',version:1,type:'MainWindowSnapshotInvalidated',generation:${JSON.stringify(generation)},sessionIncarnation:${JSON.stringify(incarnation)},payload:{},...patch}});},
    stats(){return {listeners:listeners.size,adds:q.adds,removes:q.removes,maxListeners:q.maxListeners,commands:q.sent.map(c=>c.type)};}};
   if(${Boolean(options.noBridge)})return;
   if(${Boolean(options.productChrome)})Object.defineProperty(window,'__dseProductShell',{value:true});
   const replyPreview=(${fixturePreviewReply.toString()})();const runtimeSample=${fixtureRuntimeSample.toString()};
   window.chrome??={};window.chrome.webview={
    postMessage(c){q.sent.push(c);let payload;
     if(${Boolean(options.productChrome)}&&(c.kind.startsWith('shell-')||c.kind==='session-hide-request'))return;
     if(c.type==='GetMainWindowSnapshot')payload=q.initial;
     else if(c.type==='SetPreviewRect')payload=replyPreview(c);
     else if(c.type==='GetRuntimePlaybackSample')payload=runtimeSample(q.initial);
     else if(['SetCanvasBoundaryHighlight','SetDesktopBackground','DeployDesktop','UndeployDesktop','IdentifyBoundDisplay','AcknowledgeSessionNotice'].includes(c.type)){q.viewRequests.push(c);return;}
     else throw new Error('Unexpected production write '+c.type);
     queueMicrotask(()=>{for(const fn of listeners)fn({data:{kind:'result',version:1,requestId:c.requestId,type:c.type,generation:c.generation,sessionIncarnation:c.sessionIncarnation,ok:${!options.failure},...(${Boolean(options.failure)}?{error:'snapshot-unavailable'}:{payload})}});});},
    addEventListener(type,fn){if(type!=='message'||listeners.has(fn))throw new Error('Duplicate bridge handler');listeners.add(fn);q.adds++;q.maxListeners=Math.max(q.maxListeners,listeners.size);},
    removeEventListener(type,fn){if(type!=='message'||!listeners.delete(fn))throw new Error('Unbalanced bridge teardown');q.removes++;}};
  })();`});
  const marker=crypto.randomUUID();await evaluate('window.b1Marker='+JSON.stringify(marker));const url=productionSessionUrl(origin);
  await send('Page.navigate',{url});await wait('window.b1Marker!=='+JSON.stringify(marker)+'&&document.querySelector(".production-session")');await wait('document.querySelector(".production-session")?.dataset.bootstrapState!=="loading"');await rest();
 }
 const ready='document.querySelector(".production-session")?.dataset.bootstrapState==="ready"';
 const view='document.querySelector("[aria-controls=preview-view-options]")';
 const viewInputs='[...document.querySelectorAll("#preview-view-options input")]';
 const deploy='document.querySelector(".deployment-action")';
 const shellState=()=>evaluate(`(()=>{const s=document.querySelector('.production-session').dataset;
  return [s.authoredRevision,s.cleanRevision,s.dirty,s.selectedPlaylistId,s.selectedItemId,s.activePlaylistId,s.activeItemId,s.runtimeBindingToken]})()`);
 const readViewCommands=()=>evaluate('window.b1Qa.sent.filter(c=>["SetCanvasBoundaryHighlight","SetDesktopBackground","DeployDesktop","UndeployDesktop"].includes(c.type)).map(({requestId,...c})=>c)');
 const expectIntent=(type,payload)=>({kind:'command',version:1,type,generation,sessionIncarnation:incarnation,payload});
 if(viewOnly){
  await check('Native product notice survives page recreation and requires the exact explicit acknowledgement',async()=>{
   const value=snapshot();
   value.sessionNotice={id:'1f05d5ee-bbaa-44fa-a7b1-11799b61ea2a',level:'warning',title:'Desktop Background',message:'The current mode was kept.'};
   const notice='document.querySelector("dialog[open] [data-session-notice]")';
   await load(value);await wait(notice);assert.equal(await evaluate(notice+'.textContent'),'The current mode was kept.');
   await send('Input.dispatchKeyEvent',{type:'keyDown',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
   assert(await evaluate('!!'+notice));assert.equal(await evaluate('window.b1Qa.viewRequests.length'),0);
   await load(value);await wait(notice);assert.equal(await evaluate('window.b1Qa.viewRequests.length'),0);
   await evaluate('document.querySelector("[data-notice-ok]").click()');await wait('window.b1Qa.viewRequests.length===1');
   assert.deepEqual(await evaluate('(({requestId,...intent})=>intent)(window.b1Qa.viewRequests[0])'),expectIntent('AcknowledgeSessionNotice',{noticeId:value.sessionNotice.id}));
   assert(await evaluate('!!'+notice),'Wait for native acknowledgement');
   await evaluate('delete window.b1Qa.initial.sessionNotice;window.b1Qa.finishView("applied")');await wait('!'+notice);
  });
  await check('Identify invokes the bound-display command only while the exact bound display is resolved',async()=>{
   const identify='document.querySelector(".preview-actions button")';
   await load();assert(await evaluate(ready));
   assert.equal(await evaluate(identify+'.disabled'),false);
   const before=await shellState();
   await evaluate(identify+'.click()');await wait('window.b1Qa.viewRequests.length===1');
   assert.deepEqual(await evaluate('(({requestId,...intent})=>intent)(window.b1Qa.viewRequests[0])'),expectIntent('IdentifyBoundDisplay',{}));
   await evaluate('window.b1Qa.finishView("shown")');await rest();assert.deepEqual(await shellState(),before);
   await evaluate(identify+'.click()');await wait('window.b1Qa.viewRequests.length===1');
   await evaluate('window.b1Qa.initial.display={status:"success",descriptor:null};window.b1Qa.finishView("unavailable");window.b1Qa.event()');
   await wait(identify+'.disabled');assert.deepEqual(await shellState(),before);
   const failed=snapshot();failed.display={status:'api-failure',descriptor:null};
   await load(failed);assert(await evaluate(ready));assert(await evaluate(identify+'.disabled'));
  });
  await check('Preview View opens and closes without a write; Escape restores focus and window blur closes without refocus',async()=>{
   await load();assert(await evaluate(ready));
   await evaluate(view+'.focus();'+view+'.click()');await wait(view+'.getAttribute("aria-expanded")==="true"');
   await evaluate(viewInputs+'[0].focus()');
   await send('Input.dispatchKeyEvent',{type:'keyDown',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
   await wait('!document.querySelector("#preview-view-options")');assert(await evaluate('document.activeElement==='+view));
   await evaluate(view+'.click()');await wait('document.querySelector("#preview-view-options")');
   await evaluate('window.dispatchEvent(new Event("blur"))');
   await wait('!document.querySelector("#preview-view-options")');
   await evaluate(view+'.click()');await wait('document.querySelector("#preview-view-options")');
   await evaluate('document.querySelector(".session-project-trigger").focus()');
   await wait('!document.querySelector("#preview-view-options")');
   assert(await evaluate('document.activeElement===document.querySelector(".session-project-trigger")'));
   assert.deepEqual(await readViewCommands(),[]);
  });
  await check('Main View sends Highlight off/on and background radios using closed typed commands; geometry and native editor facts remain unchanged',async()=>{
   const native=snapshot();native.preferences.highlight=false;native.preferences.background='cover';
   await load(native);assert(await evaluate(ready));await evaluate(view+'.focus();'+view+'.click()');await rest();
   const chrome='getComputedStyle(document.querySelector(".preview-chrome-frame"),"::after")';
   const bounds=()=>evaluate('JSON.stringify(document.querySelector(".preview-native-surface").getBoundingClientRect().toJSON())');
   const before=await shellState(),initialRect=await bounds();
   assert.equal(await evaluate(chrome+'.content'),'none');
   assert.deepEqual(await evaluate(viewInputs+'.map(e=>[e.checked,e.disabled])'),[[false,false],[true,false],[false,false]]);
   await evaluate(viewInputs+'[0].click()');await wait('window.b1Qa.viewRequests.length===1');
   assert.deepEqual(await readViewCommands(),[expectIntent('SetCanvasBoundaryHighlight',{highlighted:true})]);
   assert.equal(await evaluate(viewInputs+'[0].checked'),false,'Await native truth');
   await evaluate('window.b1Qa.initial.preferences.highlight=true;window.b1Qa.finishView("applied")');
   await wait(viewInputs+'[0].checked');assert.equal(await evaluate(chrome+'.outlineColor'),'rgb(210, 115, 104)');assert.equal(await bounds(),initialRect);
   await evaluate(viewInputs+'[0].click()');await wait('window.b1Qa.viewRequests.length===1');
   await evaluate('window.b1Qa.initial.preferences.highlight=false;window.b1Qa.finishView("applied")');
   await wait('!'+viewInputs+'[0].checked');assert.equal(await evaluate(chrome+'.content'),'none');assert.equal(await bounds(),initialRect);
   await evaluate(viewInputs+'[2].click()');await wait('window.b1Qa.viewRequests.length===1');
   assert.deepEqual(await evaluate(viewInputs+'.map(e=>e.checked)'),[false,true,false],'Old confirmed radio stays selected until native accepts');
   await evaluate('window.b1Qa.initial.preferences.background="reveal";window.b1Qa.finishView("applied")');
   await wait(viewInputs+'[2].checked');assert.deepEqual(await evaluate(viewInputs+'.map(e=>e.checked)'),[false,false,true]);
   await evaluate(viewInputs+'[1].click()');await wait('window.b1Qa.viewRequests.length===1');
   await evaluate('window.b1Qa.initial.preferences.background="cover";window.b1Qa.finishView("applied")');
   await wait(viewInputs+'[1].checked');assert.deepEqual(await evaluate(viewInputs+'.map(e=>e.checked)'),[false,true,false]);
   assert.deepEqual(await readViewCommands(),[expectIntent('SetCanvasBoundaryHighlight',{highlighted:true}),expectIntent('SetCanvasBoundaryHighlight',{highlighted:false}),expectIntent('SetDesktopBackground',{mode:'reveal'}),expectIntent('SetDesktopBackground',{mode:'cover'})]);
   assert.deepEqual(await shellState(),before);
  });
  await check('Gear and Preview View read one native preference; another page restores changed values without a write replay',async()=>{
   const native=snapshot();native.preferences.highlight=false;native.preferences.background='cover';
   await load(native);assert(await evaluate(ready));const gear='document.querySelector("[data-session-settings-trigger]")';
   await evaluate(gear+'.click()');await wait('document.querySelector("[data-settings-command=highlight]")');
   await evaluate('document.querySelector("[data-settings-command=highlight]").click()');await wait('window.b1Qa.viewRequests.length===1');
   await evaluate('window.b1Qa.initial.preferences.highlight=true;window.b1Qa.finishView("applied")');
   await wait('document.querySelector(".preview-workspace").dataset.highlight==="true"');
   await evaluate(gear+'.click()');await wait('document.querySelector("[data-settings-submenu-trigger]")');
   await evaluate('document.querySelector("[data-settings-submenu-trigger]").click()');await wait('document.querySelector("[data-session-settings-submenu]")');
   assert.deepEqual(await evaluate('[...document.querySelectorAll("[data-session-settings-submenu] [role=menuitemradio]")].map(e=>e.getAttribute("aria-checked"))'),['true','false']);
   await evaluate('document.querySelectorAll("[data-session-settings-submenu] [role=menuitemradio]")[1].click()');await wait('window.b1Qa.viewRequests.length===1');
   await evaluate('window.b1Qa.initial.preferences.background="reveal";window.b1Qa.initial.preferences.persistedBackground="reveal";window.b1Qa.finishView("applied")');
   await wait('document.querySelector(".preview-workspace").dataset.background==="reveal"');
   assert.deepEqual(await readViewCommands(),[expectIntent('SetCanvasBoundaryHighlight',{highlighted:true}),expectIntent('SetDesktopBackground',{mode:'reveal'})]);
   const restored=await evaluate('window.b1Qa.initial');await load(restored);const marker=crypto.randomUUID();await evaluate('window.b1Marker='+JSON.stringify(marker));
   await send('Page.reload',{ignoreCache:true});await wait('window.b1Marker!=='+JSON.stringify(marker)+'&&'+ready);
   await evaluate(view+'.click()');await wait('document.querySelector("#preview-view-options")');
   assert.deepEqual(await evaluate(viewInputs+'.map(e=>e.checked)'),[true,false,true]);
   assert.deepEqual(await readViewCommands(),[]);
  });
  await check('An explicit radio reversal during native confirmation keeps the latest choice without optimistic checked state or duplicate sends',async()=>{
   const native=snapshot();native.preferences.background='cover';
   await load(native);assert(await evaluate(ready));await evaluate(view+'.click()');await wait('document.querySelector("#preview-view-options")');
   await evaluate(viewInputs+'[2].click()');await wait('window.b1Qa.viewRequests.length===1');
   await evaluate(viewInputs+'[1].click();'+viewInputs+'[1].click()');await rest();
   assert.deepEqual(await readViewCommands(),[expectIntent('SetDesktopBackground',{mode:'reveal'})]);
   assert.deepEqual((await evaluate(viewInputs+'.map(e=>e.checked)')).slice(1),[true,false]);
   await evaluate('window.b1Qa.initial.preferences.background="reveal";window.b1Qa.finishView("applied")');
   await wait('window.b1Qa.viewRequests.length===1');assert.deepEqual(await readViewCommands(),
    [expectIntent('SetDesktopBackground',{mode:'reveal'}),expectIntent('SetDesktopBackground',{mode:'cover'})]);
   await evaluate('window.b1Qa.initial.preferences.background="cover";window.b1Qa.finishView("applied")');
   await wait(viewInputs+'[1].checked');assert.deepEqual((await evaluate(viewInputs+'.map(e=>e.checked)')).slice(1),[true,false]);
  });
  await check('Reset returns the real workspace to its default splits and leaves the runtime/document unchanged; splitting works again immediately',async()=>{
   await load();assert(await evaluate(ready));const before=await shellState(),workspace='document.querySelector(".session-workspace").dataset';
   await wait(workspace+'.layoutLeft==="230"&&'+workspace+'.layoutRight==="300"');
   async function split(){
    await evaluate('document.querySelector("[data-layout-splitter=left]").focus()');
    await send('Input.dispatchKeyEvent',{type:'keyDown',key:'ArrowRight',code:'ArrowRight',windowsVirtualKeyCode:39});
    await wait(workspace+'.layoutLeft==="240"');
   }
   await split();await evaluate(view+'.focus();'+view+'.click()');await wait('document.querySelector("[data-reset-layout]")');
   await evaluate('document.querySelector("[data-reset-layout]").click()');await wait(workspace+'.layoutLeft==="230"&&'+workspace+'.layoutRight==="300"');
   await split();assert.deepEqual(await shellState(),before);assert.deepEqual(await readViewCommands(),[]);
  });
  await check('Deploy uses native eligibility, resists duplicate activation, does not mislabel transition as failure and follows failure or retained success',async()=>{
   await load();assert(await evaluate(ready));const before=await shellState();
   assert.equal(await evaluate(deploy+'.disabled'),false);
   await evaluate(deploy+'.click();'+deploy+'.click()');await wait('window.b1Qa.viewRequests.length===1');
   assert.deepEqual(await readViewCommands(),[expectIntent('DeployDesktop',{})]);
   await evaluate('Object.assign(window.b1Qa.initial.desktop,{desired:true,busy:true,canDeploy:false});window.b1Qa.event()');
   await wait('document.querySelector(".preview-presentation-state").textContent==="Deploying…"');
   assert.equal(await evaluate(deploy+'.textContent'),'Deploy');assert(await evaluate(deploy+'.disabled'));
   await evaluate('Object.assign(window.b1Qa.initial.desktop,{busy:false,desired:false,canDeploy:true});window.b1Qa.finishView("failed")');
   await wait('document.querySelector(".preview-presentation-state").textContent==="PREVIEW ONLY"');
   assert.match(await evaluate('document.querySelector(".preview-identify-result").textContent'),/could not be deployed/i);
   await evaluate(deploy+'.click()');await wait('window.b1Qa.viewRequests.length===1');
   await evaluate('Object.assign(window.b1Qa.initial.desktop,{desired:true,targetRegistered:true,hostOwned:true,hostHealthy:true,canDeploy:false,canUndeploy:true});window.b1Qa.finishView("completed")');
   await wait('document.querySelector(".preview-presentation-state").textContent==="DEPLOYED"');
   assert.equal(await evaluate(deploy+'.textContent'),'Undeploy');assert.equal(await evaluate(deploy+'.disabled'),false);
   const restored=await evaluate('window.b1Qa.initial');await load(restored);const marker=crypto.randomUUID();await evaluate('window.b1Marker='+JSON.stringify(marker));
   await send('Page.reload',{ignoreCache:true});await wait('window.b1Marker!=='+JSON.stringify(marker)+'&&'+ready);await rest();
   assert.equal(await evaluate(deploy+'.textContent'),'Undeploy');assert.deepEqual(await readViewCommands(),[]);
   await evaluate(deploy+'.click()');await wait('window.b1Qa.viewRequests.length===1');
   await evaluate('Object.assign(window.b1Qa.initial.desktop,{desired:false,targetRegistered:false,hostHealthy:false,canDeploy:false,canUndeploy:true});window.b1Qa.finishView("failed")');
   await wait('document.querySelector(".preview-presentation-state").textContent==="CLEANUP REQUIRED"');
   assert.equal(await evaluate(deploy+'.textContent'),'Undeploy');assert.equal(await evaluate(deploy+'.disabled'),false);
   assert.match(await evaluate('document.querySelector(".preview-identify-result").textContent'),/could not be undeployed/i);
   await evaluate(deploy+'.click()');await wait('window.b1Qa.viewRequests.length===1');
   await evaluate('Object.assign(window.b1Qa.initial.desktop,{desired:false,targetRegistered:false,hostOwned:false,hostHealthy:false,canDeploy:true,canUndeploy:false});window.b1Qa.finishView("completed")');
   await wait('document.querySelector(".preview-presentation-state").textContent==="PREVIEW ONLY"');
   assert.deepEqual(await readViewCommands(),[expectIntent('UndeployDesktop',{}),expectIntent('UndeployDesktop',{})]);assert.deepEqual(await shellState(),before);
  });
  assert.deepEqual(errors,[]);assert.deepEqual(warnings,[]);
  await send('Browser.close');
 }else{
 const stats=()=>evaluate('window.b1Qa.stats()');
 const observationCommands=new Set(['GetMainWindowSnapshot','GetRuntimePlaybackSample','SetPreviewRect']);
 const countReads=(facts,kind='GetMainWindowSnapshot')=>facts.commands.filter(c=>c===kind).length;
 const hideStyle=()=>evaluate(`(()=>{
  const button=document.querySelector('.window-control--hide'), style=getComputedStyle(button);
  const {width,height}=button.getBoundingClientRect();
  return {width,height,color:style.color,background:style.backgroundColor,cursor:style.cursor,
    glyph:button.querySelector('svg')?.outerHTML,tooltip:button.querySelector('[role=tooltip]')?.outerHTML,
    label:button.getAttribute('aria-label'),description:button.getAttribute('aria-describedby'),
    group:button.parentElement.getAttribute('aria-label'),disabled:button.disabled};
 })()`);
 let baselineHideStyle;
 let currentListenerCount=0;
 await check('Actual production entry renders native wire facts through accepted MainWindow surfaces',async()=>{
  await load();assert(await evaluate(ready));assert.equal(await evaluate('document.querySelectorAll(".session-library,.session-inspector,.preview-workspace,.session-timeline,.playback-rail").length'),5);
  assert.equal(await evaluate('document.querySelector(".session-dev-trigger")'),null);assert.match(await evaluate('document.body.innerText'),/Native selected playlist/);
  await wait('document.querySelector(".preview-native-surface")?.dataset.nativePreviewState==="visible"');
  currentListenerCount=(await stats()).listeners;
 });
 await check('Exact uint64 revisions, native selection/active and Output Mute survive rendering',async()=>{
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.authoredRevision'),'18446744073709551614');assert.equal(await evaluate('document.querySelector(".production-session").dataset.selectedPlaylistId'),ids.b);
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.activePlaylistId'),ids.a);assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.authoredRate'),'1.25');assert.equal(await evaluate('document.querySelector("[aria-label=\\"Actual Rate\\"]").textContent'),'—');
  assert.equal(await evaluate('document.querySelector(".rail-mute").getAttribute("aria-pressed")'),'true');
 });
 await check('The single native Session route exposes editor controls without making writes during mounting',async()=>{
  assert.equal(await evaluate('document.querySelector(".entity-select").disabled'),false);
  assert.equal(await evaluate('document.querySelector(".playlist-volume-trigger").disabled'),false);
  assert.equal(await evaluate('document.querySelector(".session-project-trigger").disabled'),false);
  assert((await stats()).commands.every(c=>observationCommands.has(c)));
 });
 await check('A page without the trusted product shell marker never mounts native caption controls and Hide stays unavailable',async()=>{
  assert.equal(await evaluate('document.querySelector(".product-caption-controls,.title-bar .application-identity")'),null);
  baselineHideStyle=await hideStyle();
  await evaluate('window.b1Qa.hideState()');await rest();
  assert(await evaluate('document.querySelector(".window-control--hide").disabled'));
  await evaluate('document.querySelector(".window-control--hide").click()');assert((await stats()).commands.every(c=>observationCommands.has(c)));
 });
 await check('Current native progress and transport remain inside Preview with no mock hover driver or visible POSITION',async()=>{
  assert(await evaluate('Boolean(document.querySelector(".preview-workspace>.session-timeline")&&document.querySelector(".preview-workspace>.playback-rail"))'));
  assert.equal(await evaluate('document.querySelector(".timeline-control").getAttribute("aria-disabled")'),'false');assert.equal(await evaluate('getComputedStyle(document.querySelector(".timeline-line")).gap'),'14px');
  assert(!/\bPOSITION\b/.test(await evaluate('document.body.innerText')));assert.equal(await evaluate('document.querySelector(".timeline-hover-overlay")'),null);
  assert.equal(await evaluate('document.querySelector(".rail-mute").disabled'),false);
 });
 await check('Settings opens native Highlight and Background reads; Reset stays local',async()=>{
  await evaluate('document.querySelector("[data-session-settings-trigger]").click()');await rest();assert.equal(await evaluate('document.querySelector("[data-settings-command=highlight]").getAttribute("aria-checked")'),'true');assert.equal(await evaluate('document.querySelector("[data-settings-command=highlight]").getAttribute("aria-disabled")'),null);
  await evaluate('document.querySelector("[data-settings-submenu-trigger]").click()');await rest();assert.equal(await evaluate('document.querySelector("[data-session-settings-submenu]").dataset.stateAuthority'),'native');
  assert.equal(await evaluate('[...document.querySelectorAll("[data-session-settings-submenu] button")].find(e=>e.getAttribute("aria-checked")==="true").textContent'),'Reveal Wallpaper in Uncovered Areas');
  await evaluate('document.querySelector("[data-settings-command=reset-layout]").click()');await rest();assert((await stats()).commands.every(c=>observationCommands.has(c)));
 });
 await check('Opening Preview View reads the native preferences without automatically writing them',async()=>{
  await evaluate('document.querySelector("[aria-controls=preview-view-options]").click()');await rest();assert(await evaluate('[...document.querySelectorAll("#preview-view-options input")].every(e=>!e.disabled)'));assert(await evaluate('document.querySelector(".preview-highlight-option input").checked'));
  await evaluate('document.querySelector("[aria-controls=preview-view-options]").click()');
 });
 await check('Native invalidation rereads coherent facts once for a synchronous burst',async()=>{
  await evaluate('window.b1Qa.initial.project.playlists[0].alias="Fresh native alias";for(let i=0;i<12;i++)window.b1Qa.event()');await wait('document.body.innerText.includes("Fresh native alias")');const s=await stats();assert(countReads(s)<=3);assert(s.commands.every(c=>observationCommands.has(c)));assert.equal(s.listeners,currentListenerCount);
  await evaluate('window.b1Qa.initial.selection.itemId=null;window.b1Qa.event()');await wait('document.querySelector(".session-inspector")?.dataset.inspectorContext==="playlist"');
  assert.equal(await evaluate('document.querySelector(".session-inspector input[type=range]").value'),'40');
 });
 await check('Eight page reloads mount one current set of bridge listeners without replaying writes',async()=>{
  for(let n=0;n<8;n++){const marker=crypto.randomUUID();await evaluate('window.b1Marker='+JSON.stringify(marker));await send('Page.reload',{ignoreCache:true});await wait('window.b1Marker!=='+JSON.stringify(marker)+'&&'+ready);await rest();const s=await stats();assert.equal(s.listeners,currentListenerCount);assert.equal(s.maxListeners,currentListenerCount);assert.equal(countReads(s),1);assert.equal(countReads(s,'GetRuntimePlaybackSample'),1);assert(s.commands.every(c=>observationCommands.has(c)));assert.equal(await evaluate('document.querySelector(".production-session").dataset.sessionIncarnation'),incarnation);}
 });
 await check('Production Bridge absence fails explicitly and renders no fixture editor',async()=>{
  await load(snapshot(),{noBridge:true});assert.equal(await evaluate('document.querySelector(".production-session").dataset.failureCode'),'BridgeUnavailable');assert.equal(await evaluate('document.querySelector(".session-workspace")'),null);
 });
 await check('Native snapshot unavailability and invalid schema fail explicitly with no fixture fallback',async()=>{
  await load(snapshot(),{failure:true});assert.equal(await evaluate('document.querySelector(".production-session").dataset.failureCode'),'SnapshotUnavailable');await load({});assert.equal(await evaluate('document.querySelector(".production-session").dataset.failureCode'),'SnapshotValidationFailure');assert.equal(await evaluate('document.querySelector(".session-workspace")'),null);
 });
 await check('An earlier snapshot with runtime and mute cannot admit the current editor',async()=>{
  const legacy={...snapshot(),runtime:null,outputMuted:true};
  await load(legacy);
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.failureCode'),'SnapshotValidationFailure');
  assert.equal(await evaluate('document.querySelector(".session-workspace")'),null);
  assert.deepEqual((await stats()).commands,['GetMainWindowSnapshot']);
 });
 await check('Integrated caption reports measured CSS geometry and accepts only the current native window state',async()=>{
  await load(snapshot(),{productChrome:true});
  assert.deepEqual(await evaluate(`Array.from(document.querySelectorAll('.product-caption-controls button'), b => b.getAttribute('aria-label'))`),['Minimize','Maximize','Close']);
  assert(await evaluate(`document.querySelector('.window-control--hide').disabled && !document.querySelector('.product-caption-controls').contains(document.querySelector('.window-control--hide'))`));
  assert.deepEqual(await hideStyle(),baselineHideStyle);
  const spacing=await evaluate(`(()=>{
    const hide=document.querySelector('.window-control--hide').getBoundingClientRect();
    const caption=document.querySelector('[data-chrome-action=minimize]').getBoundingClientRect();
    return {gap:caption.left-hide.right,centerDelta:caption.y+caption.height/2-hide.y-hide.height/2};
  })()`);
  assert.deepEqual(spacing,{gap:20,centerDelta:0});
  const geometry = () => evaluate(`window.b1Qa.sent.filter(m=>m.kind==='shell-chrome-geometry').at(-1)`);
  const first = await geometry();
  assert.equal(first.generation,generation);
  assert.deepEqual(first.viewport,{width:1440,height:900,dpr:1});
  const actual = await evaluate(`Object.fromEntries([['drag',document.querySelector('[data-chrome-drag]')],...Array.from(document.querySelectorAll('[data-chrome-action]'),b=>[b.dataset.chromeAction,b])].map(([key,element])=>{const {x,y,width,height}=element.getBoundingClientRect();return [key,{x,y,width,height}];}))`);
  assert.deepEqual(first.regions,actual);
  assert(actual.drag.x+actual.drag.width<=actual.minimize.x);
  await evaluate(`window.b1Qa.chromeState({generation:'00000000-0000-0000-0000-000000000001'});window.b1Qa.chromeState({state:{active:true,maximized:true,minimized:false,hovered:'hide',pressed:null}})`);
  await rest();
  assert.equal(await evaluate(`document.querySelector('[data-chrome-action=maximize]').getAttribute('aria-label')`),'Maximize');
  await evaluate('window.b1Qa.chromeState()');
  await wait(`document.querySelector('[data-chrome-action=maximize]').getAttribute('aria-label')==='Restore'`);
  assert(await evaluate(`document.querySelector('[data-chrome-active=false] [data-chrome-action=maximize]').dataset.chromeHovered==='true'`));
  await send('Emulation.setDeviceMetricsOverride',{width:1220,height:870,deviceScaleFactor:1.5,mobile:false});
  await wait(`window.b1Qa.sent.some(m=>m.kind==='shell-chrome-geometry'&&m.viewport.width===1220&&m.viewport.dpr===1.5)`);
  assert((await geometry()).regions.close.x<actual.close.x);
  const commands = () => evaluate(`window.b1Qa.sent.filter(m=>m.kind==='shell-chrome-command')`);
  const beforeModal=await geometry();
  await evaluate(`document.querySelector('.session-project-trigger').click()`);
  await wait(`document.querySelector('dialog:modal') && window.b1Qa.sent.filter(m=>m.kind==='shell-chrome-geometry').at(-1)?.regions.maximize===null`);
  assert.deepEqual(await geometry(),{...beforeModal,regions:{drag:null,minimize:null,maximize:null,close:null}});
  await evaluate(`document.querySelector('[data-chrome-action=close]').dispatchEvent(new MouseEvent('click',{bubbles:true,detail:0}))`);
  assert.deepEqual(await commands(),[]);
  await send('Input.dispatchKeyEvent',{type:'keyDown',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
  await send('Input.dispatchKeyEvent',{type:'keyUp',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
  await wait(`!document.querySelector('dialog:modal') && window.b1Qa.sent.filter(m=>m.kind==='shell-chrome-geometry').at(-1)?.regions.maximize`);
  assert.deepEqual(await geometry(),beforeModal);
  await evaluate(`document.querySelector('[data-chrome-action=maximize]').dispatchEvent(new MouseEvent('click',{bubbles:true,detail:1})); document.querySelector('[data-chrome-action=maximize]').focus()`);
  assert.deepEqual(await commands(),[]);
  await send('Input.dispatchKeyEvent',{type:'keyDown',key:'Enter',code:'Enter',windowsVirtualKeyCode:13,text:'\r'});
  await send('Input.dispatchKeyEvent',{type:'keyUp',key:'Enter',code:'Enter',windowsVirtualKeyCode:13});
  assert.deepEqual(await commands(),[{kind:'shell-chrome-command',version:1,generation,action:'maximize'}]);
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
 });
 await check('Frozen Main Hide sends a distinct intent for the current qualified native Session only',async()=>{
  await load(snapshot(),{productChrome:true});
  const hide='document.querySelector(".window-control--hide")';
  const requests=()=>evaluate('window.b1Qa.sent.filter(m=>m.kind==="session-hide-request"||m.kind==="shell-chrome-command")');
  await evaluate(`window.b1Qa.hideState({generation:'00000000-0000-0000-0000-000000000001'});window.b1Qa.hideState({sessionIncarnation:'00000000-0000-0000-0000-000000000001'});window.b1Qa.hideState({version:2});window.b1Qa.hideState({eligible:'true'});window.b1Qa.hideState({extra:true});${hide}.click()`);
  assert.equal(await evaluate(hide+'.disabled'),true);
  assert.deepEqual(await requests(),[]);
  await evaluate('window.b1Qa.hideState()');await wait('!'+hide+'.disabled');
  const activeStyle=await hideStyle();
  for(const property of ['width','height','glyph','label','description','group'])assert.deepEqual(activeStyle[property],baselineHideStyle[property]);
  await evaluate(`${hide}.click();document.querySelector('.session-project-trigger').click()`);
  const expected={kind:'session-hide-request',version:1,generation,sessionIncarnation:incarnation};
  assert.deepEqual(await requests(),[expected]);
  await wait('document.querySelector("dialog:modal")');
  await evaluate(`${hide}.dispatchEvent(new MouseEvent('click',{bubbles:true}))`);
  assert.deepEqual(await requests(),[expected]);
  await send('Input.dispatchKeyEvent',{type:'keyDown',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
  await send('Input.dispatchKeyEvent',{type:'keyUp',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
  await wait('!document.querySelector("dialog:modal")');
  await evaluate('window.b1Qa.hideState({eligible:false})');await wait(hide+'.disabled');
  await evaluate(hide+'.click()');assert.deepEqual(await requests(),[expected]);
  const marker=crypto.randomUUID();await evaluate('window.b1Marker='+JSON.stringify(marker));
  await send('Page.reload',{ignoreCache:true});await wait('window.b1Marker!=='+JSON.stringify(marker)+'&&'+ready);
  assert.equal(await evaluate(hide+'.disabled'),true);
  assert.deepEqual(await requests(),[]);
 });
 await check('Explicit DEV Session retains the accepted prototype and local trial interactions',async()=>{
  await send('Page.navigate',{url:origin+'/?mode=prototype&view=session&sessionScenario=normal'});await wait('document.querySelector(".session-dev-trigger")');const before=await evaluate('document.querySelectorAll("[data-playlist-id]").length');await evaluate(`document.querySelector('button[aria-label="Add Playlist"]').click()`);await rest();assert((await evaluate('document.querySelectorAll("[data-playlist-id]").length'))>before);assert.equal(await evaluate('document.querySelector(".production-session")'),null);
 });
 assert.deepEqual(errors,[]);assert.deepEqual(warnings,[]);
 await send('Browser.close');
 }
}catch(error){checks.push({name:'DOM harness',pass:false,error:error.stack??String(error)});}finally{
 socket?.close();vite.kill();if(chrome.exitCode===null){for(let i=0;i<20&&chrome.exitCode===null;i++)await pause(100);if(chrome.exitCode===null)chrome.kill();}
 cleanupProfile();
 writeFileSync(new URL(viewOnly?'view-deploy-dom-checks.json':'dom-checks.json',cache),JSON.stringify({checks,pass:checks.every(c=>c.pass),errors,warnings,actualPageReloadCycles:viewOnly?2:8,serverLog},null,2));
}
for(const c of checks)console.log((c.pass?'PASS':'FAIL')+' '+c.name+(c.error?'\n'+c.error:''));console.log(checks.filter(c=>c.pass).length+'/'+checks.length);if(checks.some(c=>!c.pass))process.exitCode=1;
