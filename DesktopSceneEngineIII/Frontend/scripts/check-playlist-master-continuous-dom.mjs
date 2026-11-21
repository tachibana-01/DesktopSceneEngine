import { chromeExecutable } from './chrome-executable.mjs';
// Controlled wire-peer DOM proof only. Native authored/Engine/persistence proof
// is a separate real production runtime check, never inferred from this peer.
import assert from 'node:assert/strict';import fs from 'node:fs/promises';import os from 'node:os';import path from 'node:path';import{spawn}from'node:child_process';import{fileURLToPath}from'node:url';
import{fixturePreviewReply,fixtureRuntimeSample,generation,incarnation,ids,productionSessionUrl,snapshot}from'./session-readonly-fixture.mjs';
const root=fileURLToPath(new URL('../',import.meta.url)),out=fileURLToPath(new URL('../node_modules/.cache/current-contract/playlist-master/',import.meta.url)),red=process.argv.includes('--red');await fs.mkdir(out,{recursive:true});
const origin='http://127.0.0.1:9437',debug='http://127.0.0.1:9397',pause=ms=>new Promise(r=>setTimeout(r,ms));for(const u of[origin,debug+'/json/list'])await assert.rejects(fetch(u),'QA port must be free');
const profile=await fs.mkdtemp(path.join(os.tmpdir(),'dse-master-dom-')),logs={server:'',browser:''},checks=[],errors=[],warnings=[];
const server=spawn(process.execPath,[path.join(root,'node_modules/vite/bin/vite.js'),'--host','127.0.0.1','--port','9437','--strictPort'],{cwd:root,windowsHide:true,stdio:['ignore','pipe','pipe']});server.stdout.on('data',v=>logs.server+=v);server.stderr.on('data',v=>logs.server+=v);
const browser=spawn(chromeExecutable,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-port=9397','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:['ignore','pipe','pipe']});browser.stderr.on('data',v=>logs.browser+=v);
let socket,serial=0,injection;const pending=new Map();
const base=()=>{const s=snapshot();s.document={...s.document,authoredRevision:'11',cleanRevision:'11',dirty:false};s.documentOperation={pending:null,last:null};s.mediaImport={pending:null,last:null};s.selection={playlistId:ids.b,itemId:null};return s;};
async function until(fn){for(let i=0;i<120;i++){const r=await fn();if(r)return r;await pause(40);}throw Error('DOM condition timeout');}
async function check(name,run){try{const detail=await run();checks.push({name,pass:true,detail});}catch(e){checks.push({name,pass:false,error:e.stack});}console.log(JSON.stringify(checks.at(-1)));}
const call=(method,params={})=>new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});
async function evaluate(expression){const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});assert(!r.exceptionDetails,JSON.stringify(r.exceptionDetails));return r.result.value;}
const row='[data-playlist-id="'+ids.b+'"] .playlist-volume-trigger',slider='[data-playlist-master-slider]';
const q=s=>'document.querySelector('+JSON.stringify(s)+')',rest=()=>evaluate('new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))');
async function point(selector,y=.5){return evaluate(`(()=>{const r=${q(selector)}.getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height*${y}}})()`);}
async function pointer(type,p,down=false){await call('Input.dispatchMouseEvent',{type,...p,button:type==='mouseMoved'?(down?'left':'none'):'left',buttons:down?1:0,clickCount:type==='mouseMoved'?0:1});await rest();}
async function input(value){await evaluate(`(()=>{const e=${q(slider)};Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(e,${value});e.dispatchEvent(new Event('input',{bubbles:true}));})()`);await rest();}
async function load(hold=false){if(injection)await call('Page.removeScriptToEvaluateOnNewDocument',{identifier:injection.identifier});
 injection=await call('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{const listeners=new Set(),q=window.masterQa={value:${JSON.stringify(base())},sent:[],hold:${hold},held:[],fail:false,transportFail:false,release(i=0){q.held.splice(i,1)[0]?.();},emit(){for(const f of listeners)f({data:{kind:'event',version:1,type:'MainWindowSnapshotInvalidated',generation:${JSON.stringify(generation)},sessionIncarnation:${JSON.stringify(incarnation)},payload:{}}});}};
 function deliver(c,payload,error){const data={kind:'result',version:1,type:c.type,requestId:c.requestId,generation:c.generation,sessionIncarnation:c.sessionIncarnation,ok:!error,...(error?{error}:{payload})};for(const f of listeners)f({data});}
 const replyPreview=(${fixturePreviewReply.toString()})();const runtimeSample=${fixtureRuntimeSample.toString()};window.chrome??={};window.chrome.webview={postMessage(c){q.sent.push(c);if(c.type==='GetMainWindowSnapshot'){const v=structuredClone(q.value);queueMicrotask(()=>deliver(c,v));return;}if(c.type==='SetPreviewRect'){queueMicrotask(()=>deliver(c,replyPreview(c)));return;}if(c.type==='GetRuntimePlaybackSample'){queueMicrotask(()=>deliver(c,runtimeSample(q.value)));return;}if(c.type==='NegotiateAudioEffects'){queueMicrotask(()=>deliver(c,structuredClone(q.value.audioEffects)));return;}if(c.type==='SetPlaylistMaster'){if(c.audioEffects?.version!==1||c.audioEffects.documentIncarnation!==q.value.audioEffects.documentIncarnation)throw Error('Missing authoritative document stamp');const p=q.value.project.playlists.find(p=>p.id===c.payload.playlistId),fail=q.fail,transport=q.transportFail;q.fail=q.transportFail=false;const changed=!fail&&!transport&&p.masterVolume!==c.payload.value;if(changed){p.masterVolume=c.payload.value;q.value.document.authoredRevision=String(BigInt(q.value.document.authoredRevision)+1n);q.value.document.dirty=true;}const r={status:fail?'invalid-value':changed?'applied':'no-change',playlistId:p.id,itemId:null,token:null,authored:fail?'rejected':changed?'committed':'unchanged',revision:q.value.document.authoredRevision,dirty:q.value.document.dirty,runtime:'not-required',positionRestore:'not-required',warnings:[],sources:[],playlistMasterVolume:p.masterVolume};const fn=()=>deliver(c,r,transport?'authored-capability-unavailable':undefined);if(q.hold)q.held.push(fn);else queueMicrotask(fn);return;}throw Error('Unexpected test command '+c.type);},addEventListener(_,f){listeners.add(f);},removeEventListener(_,f){listeners.delete(f);}};})();`});
 await call('Page.navigate',{url:productionSessionUrl(origin)});await until(()=>evaluate('document.querySelector(".production-session")?.dataset.bootstrapState==="ready"'));await rest();
 await pointer('mouseMoved',await point(row));await until(()=>evaluate('!!'+q(slider)));await evaluate(`window.masterRange=${q(slider)};window.masterPointer=null;window.masterTrace=[];for(const type of ['pointerdown','gotpointercapture','lostpointercapture','pointerup'])window.masterRange.addEventListener(type,e=>{if(type==='pointerdown')window.masterPointer=e.pointerId;window.masterTrace.push({type,id:e.pointerId,capture:window.masterRange.hasPointerCapture(e.pointerId)})})`);await pointer('mousePressed',await point(slider,.6),true);await pointer('mouseMoved',await point(slider,.6),true);
}
const facts=()=>evaluate(`({thumb:Number(${q(slider)}?.value),row:${q(row)}?.dataset.masterPercent,outputMuted:${q('.production-session')}.dataset.outputMuted,rangeSame:window.masterRange===${q(slider)},disabled:${q(slider)}?.disabled,capture:window.masterRange.hasPointerCapture(window.masterPointer),pointerId:window.masterPointer,trace:window.masterTrace,native:structuredClone(window.masterQa.value),masters:window.masterQa.sent.filter(c=>c.type==='SetPlaylistMaster'),reads:window.masterQa.sent.filter(c=>c.type==='GetMainWindowSnapshot').length,held:window.masterQa.held.length})`);
async function release(){await pointer('mouseReleased',await point(slider,.15));await rest();}
try{
 const page=await until(async()=>{try{await fetch(origin);return(await(await fetch(debug+'/json/list')).json()).find(p=>p.type==='page');}catch{return false;}});socket=new WebSocket(page.webSocketDebuggerUrl);await new Promise((r,j)=>{socket.addEventListener('open',r,{once:true});socket.addEventListener('error',j,{once:true});});socket.onmessage=e=>{const m=JSON.parse(e.data);if(m.method==='Runtime.exceptionThrown')errors.push(m.params);if(m.method==='Runtime.consoleAPICalled'&&['error','warning'].includes(m.params.type))(m.params.type==='error'?errors:warnings).push(m.params);if(m.method==='Log.entryAdded'&&['error','warning'].includes(m.params.entry.level))(m.params.entry.level==='error'?errors:warnings).push(m.params.entry);if(pending.has(m.id)){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}};
 await call('Runtime.enable');await call('Log.enable');await call('Page.enable');await call('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});await call('Emulation.setFocusEmulationEnabled',{enabled:true});
 await check('Pointer-held20→35→50→70→85 sends and projects every value before release with no full read',async()=>{await load();const before=await facts(),values=[];assert.equal(before.outputMuted,'true');try{for(const v of[20,35,50,70,85]){await input(v);const f=await facts();values.push({input:v,native:f.native.project.playlists[1].masterVolume,thumb:f.thumb,revision:f.native.document.authoredRevision});assert.equal(f.native.project.playlists[1].masterVolume,v/100,'Native peer must receive intermediate intent before release');assert.equal(f.thumb,v);assert(f.rangeSame&&!f.disabled&&f.trace.some(e=>e.type==='gotpointercapture')&&!f.trace.some(e=>e.type==='lostpointercapture'||e.type==='pointerup'),JSON.stringify({same:f.rangeSame,capture:f.capture,disabled:f.disabled,thumb:f.thumb,held:f.held,pointerId:f.pointerId,trace:f.trace}));assert.equal(f.reads,before.reads);assert.equal(f.native.project.playlists[0].masterVolume,before.native.project.playlists[0].masterVolume);assert.equal(f.outputMuted,before.outputMuted);}await release();const after=await facts();assert.equal(after.thumb,85);assert.equal(after.masters.length-before.masters.length,5);assert.equal(after.row,'85');return{beforeReads:before.reads,afterReads:after.reads,values,dragBridgeCommands:5};}finally{await pointer('mouseReleased',{x:700,y:100});}});
 if(!red){
 await check('One admitted scalar and one latest-unsent draft preserve capture and final projection',async()=>{
  await load(true);
  try{
   const start=await facts(),admitted=start.masters.at(-1).payload.value;
   assert.equal(start.held,1,'The real initial pointer press has already admitted one value');
   for(const v of [20,35,50,70,85]){
    await input(v);const f=await facts();
    assert.equal(f.thumb,v);
    assert(f.rangeSame&&!f.disabled&&f.trace.some(e=>e.type==='gotpointercapture')&&!f.trace.some(e=>e.type==='lostpointercapture'||e.type==='pointerup'));
   }
   let f=await facts();assert.equal(f.held,1);assert.equal(f.native.project.playlists[1].masterVolume,admitted);
   assert.equal(f.masters.length,start.masters.length);assert.equal(await evaluate(q(row)+'.disabled'),true);
   await evaluate(q(row)+'.click()');assert.equal((await facts()).masters.length,f.masters.length,'Busy mute cannot overwrite pending Master');
   await release();
   await evaluate('window.masterQa.release(0)');await until(async()=>(await facts()).masters.length===f.masters.length+1);
   f=await facts();assert.equal(f.held,1);assert.equal(f.native.project.playlists[1].masterVolume,.85);
   assert.deepEqual(f.masters.slice(start.masters.length-1).map(c=>c.payload.value),[admitted,.85]);
   await evaluate('window.masterQa.release(0)');await rest();f=await facts();
   assert.equal(f.thumb,85);assert.equal(f.row,'85');assert.equal(await evaluate(q(row)+'.disabled'),false);
   assert.equal(f.reads,start.reads);return {admittedValues:[admitted,.85],final:f.row};
  }finally{await pointer('mouseReleased',{x:700,y:100});}
 });
 await check('Rejected intermediate command reconciles thumb to native truth, not an unaccepted local value',async()=>{await load();const before=await facts();await evaluate('window.masterQa.fail=true');await input(85);await rest();const f=await facts();assert.equal(f.native.project.playlists[1].masterVolume,before.native.project.playlists[1].masterVolume);assert.equal(f.thumb,Math.round(before.native.project.playlists[1].masterVolume*100));assert.equal(f.row,before.row);assert.equal(f.native.document.authoredRevision,before.native.document.authoredRevision);assert.equal(f.native.document.dirty,before.native.document.dirty);await pointer('mouseReleased',{x:700,y:100});});
 await check('Transport failure refreshes real peer truth and clears unaccepted draft',async()=>{await load();const before=await facts();await evaluate('window.masterQa.transportFail=true');await input(85);await rest();const f=await facts();assert.equal(f.thumb,before.thumb);assert.equal(f.row,before.row);assert(f.reads>before.reads);await pointer('mouseReleased',{x:700,y:100});});
 await check('A foreign release cannot end an owned drag across the popover boundary',async()=>{
  try{
   await load();const before=await facts(),outside={x:25,y:120};
   assert(before.trace.some(event=>event.type==='gotpointercapture'));
   await evaluate(`window.masterRange.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,pointerId:${before.pointerId+100},clientX:25,clientY:120}))`);
   await pointer('mouseMoved',outside,true);
   await pause(150);
   const held=await facts();
   assert(held.rangeSame,'The popover closed while its real pointer was still held');
   await pointer('mouseReleased',outside);
   await until(()=>evaluate('!'+q(slider)));
   await pointer('mouseMoved',await point(row));await until(()=>evaluate('!!'+q(slider)));
   const drag=await point(slider,.25);
   await pointer('mousePressed',drag,true);await pointer('mouseMoved',await point(slider,.4),true);
   await pointer('mouseReleased',await point(slider,.4));
   assert((await facts()).masters.length>before.masters.length,'A fresh drag is accepted after outside release');
  }finally{await pointer('mouseReleased',{x:25,y:120});}
 });
 await check('A new press keeps the latest displayed Master while native admission is pending',async()=>{
  try{
   await load(true);await input(85);await release();
   const before=await facts();assert.equal(before.thumb,85);assert.notEqual(before.row,'85');
   // Prevent a new track value so the observed change can only come from the press handler.
   await evaluate(`document.addEventListener('pointerdown',e=>e.preventDefault(),{once:true})`);
   await pointer('mousePressed',await point(slider,.2),true);
   assert.equal((await facts()).thumb,85,'Press reset the current thumb to a stale acknowledged value');
   await pointer('mouseReleased',await point(slider,.2));
   await evaluate('window.masterQa.release(0)');await until(async()=>(await facts()).held===1);
   await evaluate('window.masterQa.release(0)');await until(async()=>(await facts()).row==='85');
  }finally{await pointer('mouseReleased',{x:25,y:120});}
 });
 await check('Cancel and lost capture retire trailing browser input without undoing admitted values',async()=>{
  for(const type of ['pointercancel','lostpointercapture']){
   try{
    await load();const before=await facts();
    await evaluate(`window.masterRange.dispatchEvent(new PointerEvent(${JSON.stringify(type)},{bubbles:true,pointerId:${before.pointerId},button:0}))`);
    await pointer('mouseMoved',await point(slider,.25),true);
    assert.equal((await facts()).masters.length,before.masters.length,'Canceled gesture sent a trailing authored edit');
    await pointer('mouseReleased',await point(slider,.25));
    await pointer('mouseMoved',await point(row));await until(()=>evaluate('!!'+q(slider)));
    const newPoint=await point(slider,.7);
    await pointer('mousePressed',newPoint,true);await pointer('mouseReleased',newPoint);
    assert((await facts()).masters.length>before.masters.length);
   }finally{await pointer('mouseReleased',{x:25,y:120});}
  }
 });
 await check('Replacing the native document retires the captured row even when its Playlist ID is reused',async()=>{
  try{
   await load();const before=await facts();
   await evaluate(`window.masterQa.value.audioEffects.documentIncarnation='effe0000-0000-4000-8000-000000000001';window.masterQa.emit()`);
   await rest();
   assert(await evaluate('!window.masterRange.isConnected'));
   await pointer('mouseMoved',{x:25,y:120},true);await pointer('mouseReleased',{x:25,y:120});
   assert.equal((await facts()).masters.length,before.masters.length);
  }finally{await pointer('mouseReleased',{x:25,y:120});}
 });
 await check('Same-value native no-op and Inspector projection preserve revision/dirty and integer formatting',async()=>{await load();await input(85);const first=await facts();await input(85);await release();const f=await facts();assert.equal(f.native.document.authoredRevision,first.native.document.authoredRevision);assert.equal(f.native.document.dirty,true);assert.equal(await evaluate(q('input[aria-label="Playlist Master Volume"]')+'.value'),'85');assert.equal(await evaluate('document.querySelector(".playlist-volume-percent").textContent'),'85%');});
 await check('No uncaught browser/React warnings',()=>{assert.deepEqual(errors,[]);assert.deepEqual(warnings,[]);});
 }
}catch(e){checks.push({name:'Harness',pass:false,error:e.stack});}
finally{socket?.close();browser.kill();server.kill();await Promise.all([new Promise(r=>browser.exitCode!==null?r():browser.once('exit',r)),new Promise(r=>server.exitCode!==null?r():server.once('exit',r))]);const p=await fs.realpath(profile);assert.equal(path.dirname(p).toLowerCase(),os.tmpdir().toLowerCase());assert(path.basename(p).startsWith('dse-master-dom-'));await fs.rm(p,{recursive:true});const r={pass:checks.length>0&&checks.every(c=>c.pass),checks,errors,warnings,scope:'Production React interaction against controlled wire peer ONLY; native/Engine/persistence have independent proof',flicker:'NOT AUTOMATICALLY QUALIFIED; Human visible flicker NOT OBSERVED, not this gate',profileRemoved:true};await fs.writeFile(path.join(out,red?'frontend-red.json':'frontend-green.json'),JSON.stringify(r,null,2)+'\n');console.log(JSON.stringify({pass:r.pass,checks:checks.length}));if(!r.pass)process.exitCode=1;}
