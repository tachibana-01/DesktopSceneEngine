import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {mkdirSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve,sep} from 'node:path';
import {fixturePreviewReply,fixtureRuntimeSample,generation,incarnation,productionSessionUrl,snapshot} from './session-readonly-fixture.mjs';

// This checks the real production React route against a controlled wire peer.
// It is frontend geometry/lifetime proof, not native Engine/renderer proof.
const red=process.argv.includes('--red'),origin='http://127.0.0.1:9419',debug='http://127.0.0.1:9396';
const evidence=new URL('../node_modules/.cache/current-contract/preview-dom/',import.meta.url);mkdirSync(evidence,{recursive:true});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const profile=mkdtempSync(join(tmpdir(),'dse-b5a-frontend-'));
for(const url of [origin,debug+'/json/list'])await assert.rejects(fetch(url),'Private QA port must be free: '+url);
const vite=spawn(process.execPath,[fileURLToPath(new URL('../node_modules/vite/bin/vite.js',import.meta.url)),'--host','127.0.0.1','--port','9419','--strictPort'],{cwd:fileURLToPath(new URL('../',import.meta.url)),windowsHide:true,stdio:['ignore','pipe','pipe']});
let serverLog='';vite.stdout.on('data',b=>serverLog+=b);vite.stderr.on('data',b=>serverLog+=b);
const chrome=spawn(chromeExecutable,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-address=127.0.0.1','--remote-debugging-port=9396','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:'ignore'});
const checks=[],errors=[],warnings=[];let socket,send,fitSamples=[];
try{
 let pages;for(let n=0;n<100;n++){try{await fetch(origin);pages=await(await fetch(debug+'/json/list')).json();if(pages.some(p=>p.type==='page'))break;}catch{}await pause(100);}
 const page=pages?.find(p=>p.type==='page');assert(page,'Owned browser/server started: '+serverLog);
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
 async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
 await Promise.all([send('Page.enable'),send('Runtime.enable'),send('Log.enable'),send('Network.enable')]);await send('Network.setCacheDisabled',{cacheDisabled:true});await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
 await send('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{
  const listeners=new Set(),replyPreview=(${fixturePreviewReply.toString()})();const q=window.b5Qa={sent:[],fitSamples:[],failure:null,current:null,
   emit(patch={}){for(const fn of listeners)fn({data:{kind:'event',version:1,type:'PreviewGeometryInvalidated',generation:${JSON.stringify(generation)},sessionIncarnation:${JSON.stringify(incarnation)},payload:{},...patch}});},
   retire(){q.emit({type:'PreviewPresentationState',payload:{acceptedThrough:q.current.revision,accepted:null,lifecycleValid:false,reason:'test-native-retirement',apertureOwner:'host-composition-clip'}});},
   stats(){return {listeners:listeners.size,commands:q.sent.map(c=>c.type)};}};
  window.chrome??={};window.chrome.webview={
   postMessage(c){q.sent.push(c);let payload;
    if(c.type==='SetPreviewRect'){
     const e=document.querySelector('.preview-native-surface'),v=e.closest('.preview-viewport').getBoundingClientRect();
     q.fitSamples.push({visible:c.payload.visible,fit:[Number(e.dataset.previewFitWidth),Number(e.dataset.previewFitHeight)],current:[Math.floor(v.width),Math.floor(v.height)],viewport:c.payload.viewport,rect:c.payload.rect});
     if(!q.failure){payload=replyPreview(c);q.current=payload.accepted;}
    }else if(c.type==='GetMainWindowSnapshot')payload=${JSON.stringify(snapshot())};
    else if(c.type==='GetRuntimePlaybackSample')payload=${JSON.stringify(fixtureRuntimeSample(snapshot()))};
    else throw new Error('Unexpected domain command '+c.type);
    const error=c.type==='SetPreviewRect'?q.failure:null;
    queueMicrotask(()=>{for(const fn of listeners)fn({data:{kind:'result',version:1,requestId:c.requestId,type:c.type,generation:c.generation,sessionIncarnation:c.sessionIncarnation,ok:!error,...(error?{error}:{payload})}});});
   },
   addEventListener(type,fn){if(type!=='message'||listeners.has(fn))throw new Error('Duplicate bridge handler');listeners.add(fn);},
   removeEventListener(type,fn){if(type!=='message'||!listeners.delete(fn))throw new Error('Unbalanced bridge teardown');}};
 })();`});
 async function load(){const marker=crypto.randomUUID();await evaluate('window.b5Marker='+JSON.stringify(marker));await send('Page.navigate',{url:productionSessionUrl(origin)});await wait('window.b5Marker!=='+JSON.stringify(marker)+'&&document.querySelector(".production-session")?.dataset.bootstrapState==="ready"');}
 const geom='window.b5Qa.sent.filter(c=>c.type==="SetPreviewRect")';
 await load();
 await check('Current production measures the real native slot and sends one observation',async()=>{
  await wait(geom+'.length>0');
  const actual=await evaluate('(()=>{const r=document.querySelector(".preview-native-surface").getBoundingClientRect();return {rect:{x:r.x,y:r.y,width:r.width,height:r.height},viewport:{width:innerWidth,height:innerHeight},rasterizationScale:devicePixelRatio,visible:true,occlusion:{modalActive:false,rects:[]}};})()');
  assert.deepEqual(await evaluate(geom+'.at(-1).payload'),actual);await wait('document.querySelector("[data-native-preview-status]")?.dataset.nativePreviewStatus==="visible"');
  assert(!/INTEGRATION PENDING/.test(await evaluate('document.querySelector(".preview-caption").textContent')));
 });
 if(!red){
 await check('Resize and local splitter layout update exact CSS bounds without scale multiplication',async()=>{
  const before=await evaluate(geom+'.length');await send('Emulation.setDeviceMetricsOverride',{width:1240,height:850,deviceScaleFactor:1.5,mobile:false});await wait(geom+'.length>'+before+'&&'+geom+'.at(-1).payload.rasterizationScale===1.5');
  await pause(100);const actual=await evaluate('(()=>{const r=document.querySelector(".preview-native-surface").getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};})()');assert.deepEqual(await evaluate(geom+'.at(-1).payload.rect'),actual);assert.equal(await evaluate(geom+'.at(-1).payload.viewport.width'),1240);
  await evaluate('document.querySelector("[data-layout-splitter=left]").dispatchEvent(new KeyboardEvent("keydown",{key:"ArrowRight",bubbles:true,cancelable:true}))');
  await wait(geom+'.at(-1).payload.rect.x!=='+actual.x);await pause(70);
  const moved=await evaluate('(()=>{const r=document.querySelector(".preview-native-surface").getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};})()');assert.deepEqual(await evaluate(geom+'.at(-1).payload.rect'),moved);
 });
 await check('Native geometry invalidation forces unchanged bounds resend; stale generations are ignored',async()=>{
  const n=await evaluate(geom+'.length');await evaluate('window.b5Qa.emit({generation:crypto.randomUUID()})');await pause(80);assert.equal(await evaluate(geom+'.length'),n);
  await evaluate('for(let i=0;i<20;i++)window.b5Qa.emit()');await wait(geom+'.length>'+n);await pause(80);assert.equal(await evaluate(geom+'.length'),n+1);
 });
 await check('Local View header expansion remeasures a same-sized surface after committed layout moves',async()=>{
  const before=await evaluate(geom+'.at(-1).payload.rect');
  await evaluate('document.querySelector("[aria-controls=preview-view-options]").click()');
  await wait(geom+'.at(-1).payload.rect.y!=='+before.y);await pause(70);
  const actual=await evaluate('(()=>{const r=document.querySelector(".preview-native-surface").getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};})()');assert.deepEqual(await evaluate(geom+'.at(-1).payload.rect'),actual);
  await evaluate('document.querySelector("[aria-controls=preview-view-options]").click()');await wait(geom+'.at(-1).payload.rect.y==='+before.y);
 });
 await check('Every admitted visible rect uses current committed fit inputs across responsive resize',async()=>{
  await evaluate('window.b5Qa.fitSamples=[]');
  for(const width of [1337,1184,1100,1337]){
   await send('Emulation.setDeviceMetricsOverride',{width,height:810,deviceScaleFactor:1.5,mobile:false});
   await wait(geom+'.at(-1).payload.viewport.width==='+width);await pause(50);
  }
  fitSamples=await evaluate('window.b5Qa.fitSamples');assert(fitSamples.length>=4);
  for(const sample of fitSamples.filter(s=>s.visible))assert.deepEqual(sample.fit,sample.current,'pre-fit intermediate DOM sample reached native');
 });
 await check('Zero desired extent preserves the native current presentation until lifecycle retirement',async()=>{
  const accepted=await evaluate('document.querySelector(".preview-native-surface").dataset.modelCAccepted');
  await evaluate('document.querySelector(".preview-native-surface").style.width="0px"');
  await wait(geom+'.at(-1).payload.rect.width===0&&document.querySelector(".preview-native-surface").dataset.modelCDesiredOutcome==="invalid-request"');
  assert.equal(await evaluate(geom+'.at(-1).payload.visible'),true);
  assert.equal(await evaluate('document.querySelector(".preview-native-surface").dataset.modelCAccepted'),accepted);
  await evaluate('window.b5Qa.retire()');await wait('document.querySelector("[data-native-preview-status]")?.dataset.nativePreviewStatus==="suspended"');
  await evaluate('document.querySelector(".preview-native-surface").style.removeProperty("width")');await wait(geom+'.at(-1).payload.rect.width>0&&document.querySelector("[data-native-preview-status]")?.dataset.nativePreviewStatus==="visible"');
 });
 await check('Correlated native geometry failure remains explicit; later native validation can recover',async()=>{
  await evaluate('window.b5Qa.retire();window.b5Qa.failure="engine-attach-failure";window.b5Qa.emit()');await wait('document.querySelector("[data-native-preview-status]")?.dataset.nativePreviewStatus==="failure"');assert.match(await evaluate('document.querySelector(".preview-caption").textContent'),/engine-attach-failure/);
  await evaluate('window.b5Qa.failure=null;window.b5Qa.emit()');await wait('document.querySelector("[data-native-preview-status]")?.dataset.nativePreviewStatus==="visible"');
 });
 await check('Four page reloads each create one current geometry subscription with no domain replay',async()=>{
  const listenerCount=await evaluate('window.b5Qa.stats().listeners');
  for(let i=0;i<4;i++){await load();await wait(geom+'.length>0');await pause(70);assert.equal(await evaluate('window.b5Qa.stats().listeners'),listenerCount);assert.equal(await evaluate('window.b5Qa.sent.filter(c=>c.type==="GetMainWindowSnapshot").length'),1);assert(await evaluate('window.b5Qa.sent.every(c=>c.generation==='+JSON.stringify(generation)+'&&c.sessionIncarnation==='+JSON.stringify(incarnation)+')'));}
 });
 }
 assert.deepEqual(errors,[]);assert.deepEqual(warnings,[]);await send('Browser.close');
}catch(error){checks.push({name:'DOM harness',pass:false,error:error.stack??String(error)});}finally{
 socket?.close();vite.kill();for(let i=0;i<20&&chrome.exitCode===null;i++)await pause(100);if(chrome.exitCode===null)chrome.kill();
 const target=resolve(profile),base=resolve(tmpdir())+sep;if(!target.startsWith(base)||!target.slice(base.length).startsWith('dse-b5a-frontend-'))throw new Error('Unsafe profile cleanup');rmSync(target,{recursive:true,force:true,maxRetries:5,retryDelay:100});
 writeFileSync(new URL(red?'dom-red.json':'dom-checks.json',evidence),JSON.stringify({checks,pass:checks.every(c=>c.pass),errors,warnings,serverLog,fitSamples,proofScope:'production React route + controlled wire peer; not native Engine proof'},null,2));
}
for(const c of checks)console.log((c.pass?'PASS':'FAIL')+' '+c.name+(c.error?'\n'+c.error:''));console.log(checks.filter(c=>c.pass).length+'/'+checks.length);if(checks.some(c=>!c.pass))process.exitCode=1;
