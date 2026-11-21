import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {mkdirSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve,sep} from 'node:path';
import {fixtureRuntimeSample,generation,productionSessionUrl,snapshot} from './session-readonly-fixture.mjs';

// Actual production React/DOM with a controlled native wire peer. This proves
// frontend semantics only; native composition and real media have separate tests.
const origin='http://127.0.0.1:9471',debug='http://127.0.0.1:9472';
const evidence=new URL(process.env.DSE_MODEL_C_DOM_EVIDENCE??'../node_modules/.cache/current-contract/',import.meta.url);mkdirSync(evidence,{recursive:true});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
for(const url of [origin,debug+'/json/list'])await assert.rejects(fetch(url),'Private QA port must be free: '+url);
const profile=mkdtempSync(join(tmpdir(),'dse-model-c-m2-frontend-'));
const vite=spawn(process.execPath,[fileURLToPath(new URL('../node_modules/vite/bin/vite.js',import.meta.url)),'--host','127.0.0.1','--port','9471','--strictPort'],{cwd:fileURLToPath(new URL('../',import.meta.url)),windowsHide:true,stdio:['ignore','pipe','pipe']});
let serverLog='';vite.stdout.on('data',value=>serverLog+=value);vite.stderr.on('data',value=>serverLog+=value);
const chrome=spawn(chromeExecutable,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-address=127.0.0.1','--remote-debugging-port=9472','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:['ignore','pipe','pipe']});
let browserLog='';chrome.stderr.on('data',value=>browserLog+=value);
const checks=[],errors=[];let socket,send;
try{
 let pages;for(let i=0;i<100;i++){try{await fetch(origin);pages=await(await fetch(debug+'/json/list')).json();if(pages.some(value=>value.type==='page'))break;}catch{}await pause(100);}
 const page=pages?.find(value=>value.type==='page');assert(page,'Owned browser/server started: '+serverLog+' Browser exit='+chrome.exitCode+' '+browserLog);
 socket=new WebSocket(page.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
 let serial=0;const pending=new Map();socket.addEventListener('message',async event=>{const message=JSON.parse(typeof event.data==='string'?event.data:await event.data.text());if(message.method==='Runtime.exceptionThrown')errors.push(message.params.exceptionDetails);const request=pending.get(message.id);if(request){pending.delete(message.id);message.error?request.reject(message.error):request.resolve(message.result);}});
 send=(method,params={})=>new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});
 async function evaluate(expression){const result=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});assert(!result.exceptionDetails,JSON.stringify(result.exceptionDetails));return result.result.value;}
 async function wait(expression){for(let i=0;i<100;i++){if(await evaluate('Boolean('+expression+')'))return;await pause(50);}assert.fail('DOM wait: '+expression);}
 async function rest(){await evaluate('new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))');}
 async function check(name,fn){if(process.env.DSE_MODEL_C_DOM_FILTER&&!name.includes(process.env.DSE_MODEL_C_DOM_FILTER))return;try{const detail=await fn();checks.push({name,pass:true,detail});}catch(error){checks.push({name,pass:false,error:error.stack??error.message??String(error)});}}
 await Promise.all([send('Page.enable'),send('Runtime.enable'),send('Network.enable')]);await send('Network.setCacheDisabled',{cacheDisabled:true});
 // Deterministically hold only the layout hook's observation. CSS still reacts
 // to the real viewport, exposing the legal old-React/new-CSS intermediate state.
 await send('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{const Native=ResizeObserver;window.ResizeObserver=class extends Native{constructor(callback){super((entries,observer)=>{if(!window.qaHoldResponsive||!this.workspace)callback(entries,observer);});}observe(target,options){if(target.matches?.('.session-workspace'))this.workspace=true;super.observe(target,options);}};})();`});
 const fixture=snapshot();
 await send('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{
  const listeners=new Set(),query=new URLSearchParams(location.search),page=query.get('generation'),session=query.get('session');
  const q=window.modelCQa={sent:[],held:[],hold:false,outcome:'accepted',current:null,revision:0,contentGeneration:1,
   emit(type,payload,patch={}){for(const fn of listeners)fn({data:{kind:'event',version:1,type,generation:page,sessionIncarnation:session,payload,...patch}});},
   project(){q.emit('PreviewPresentationState',{acceptedThrough:String(q.revision),accepted:q.current,lifecycleValid:Boolean(q.current),reason:q.current?'accepted':'page-retired',apertureOwner:'host-composition-clip'});},
   invalidate(){q.current=null;q.project();},
   replace(){if(!q.current)throw new Error('No accepted content');q.current={...q.current,revision:String(++q.revision),contentGeneration:String(++q.contentGeneration)};q.project();},
   reply(c,outcome=q.outcome){if(c.type==='SetPreviewRect'&&outcome==='accepted'){const scale=c.payload.rasterizationScale,rect=Object.fromEntries(Object.entries(c.payload.rect).map(([key,value])=>[key,Math.round(value*scale)]));q.current={revision:String(++q.revision),page,binding:'1',requestId:c.requestId,rect,contentGeneration:String(q.contentGeneration),deviceGeneration:'1'};q.project();}
    const payload=c.type==='GetMainWindowSnapshot'?${JSON.stringify(fixture)}:c.type==='GetRuntimePlaybackSample'?${JSON.stringify(fixtureRuntimeSample(fixture))}:{status:q.current?'visible':'suspended',desiredAccepted:outcome==='accepted',desiredRequestId:c.requestId,requestOutcome:outcome,reason:outcome,accepted:q.current,lifecycleValid:Boolean(q.current),apertureOwner:'host-composition-clip'};
    for(const fn of listeners)fn({data:{kind:'result',version:1,requestId:c.requestId,type:c.type,generation:c.generation,sessionIncarnation:c.sessionIncarnation,ok:true,payload}});
   },stats(){return{listeners:listeners.size,snapshots:q.sent.filter(c=>c.type==='GetMainWindowSnapshot').length,geometry:q.sent.filter(c=>c.type==='SetPreviewRect').length};}};
  window.chrome??={};window.chrome.webview={postMessage(c){q.sent.push(c);if(!['GetMainWindowSnapshot','GetRuntimePlaybackSample','SetPreviewRect'].includes(c.type))throw new Error('Unexpected domain write '+c.type);if(c.type==='SetPreviewRect'&&q.hold)q.held.push(c);else queueMicrotask(()=>q.reply(c));},addEventListener(_,fn){listeners.add(fn);},removeEventListener(_,fn){listeners.delete(fn);}};
 })();`});
 const surface='document.querySelector(".preview-native-surface")',geometry='window.modelCQa.sent.filter(c=>c.type==="SetPreviewRect")';
 async function load(page=generation){await send('Page.navigate',{url:productionSessionUrl(origin,page)});await wait('document.querySelector(".production-session")?.dataset.bootstrapState==="ready"');await wait(surface+'?.dataset.nativePreviewState==="visible"');await rest();}
 await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
 await check('Retired Session routes never mount an editor, send commands, or enable Model C paint',async()=>{
  const url=productionSessionUrl(origin);
  for(const oldUrl of [url.replace('model-c-production','legacy-windowed'),url.replace('model-c-production','composition-b4'),url+'&preview=native-b5a&documents=native-b2']){
   await send('Page.navigate',{url:oldUrl});await wait('document.querySelector(".production-session")?.dataset.failureCode==="InvalidPageContext"');
   assert.equal(await evaluate('document.documentElement.dataset.sessionComposition'),undefined);
   assert.equal(await evaluate('document.querySelectorAll(".session-workspace,.caption-controls").length'),0);
   assert.deepEqual(await evaluate('window.modelCQa.sent'),[]);
  }
 });
 await load();
 await check('Model C has no desired-shaped DOM hole, keeps opaque UI and clears only native-valid backing',async()=>{
  assert.equal(await evaluate('document.documentElement.dataset.sessionComposition'),'model-c-production');
  assert.equal(await evaluate('getComputedStyle(document.querySelector(".preview-viewport"),"::before").clipPath'),'none');
  assert.equal(await evaluate('getComputedStyle('+surface+').backgroundColor'),'rgba(0, 0, 0, 0)');
  assert(await evaluate(`['.title-bar','.session-library','.session-inspector','.preview-context','.preview-caption','.session-timeline','.playback-rail'].every(selector=>getComputedStyle(document.querySelector(selector)).backgroundColor!=='rgba(0, 0, 0, 0)')`));
  assert.equal(await evaluate(surface+'.dataset.modelCApertureOwner'),'host-composition-clip');
 });
 await check('Responsive/sidebar desired changes may pend or reject while accepted native presentation stays visible',async()=>{
  const revision=await evaluate(surface+'.dataset.modelCAcceptedThrough');await evaluate('window.modelCQa.outcome="transient-layout-pending"');
  await send('Emulation.setDeviceMetricsOverride',{width:1240,height:850,deviceScaleFactor:1.5,mobile:false});await wait(geometry+'.at(-1).payload.viewport.width===1240');await wait(surface+'.dataset.modelCDesiredOutcome==="transient-layout-pending"');
  assert.equal(await evaluate(surface+'.dataset.modelCAcceptedThrough'),revision);assert.equal(await evaluate('getComputedStyle('+surface+').backgroundColor'),'rgba(0, 0, 0, 0)');
  await evaluate('window.modelCQa.outcome="invalid-request";document.querySelector("[data-layout-splitter=left]").dispatchEvent(new KeyboardEvent("keydown",{key:"ArrowRight",bubbles:true,cancelable:true}))');await wait(surface+'.dataset.modelCDesiredOutcome==="invalid-request"');assert.equal(await evaluate(surface+'.dataset.modelCAcceptedThrough'),revision);
  const actual=await evaluate('(()=>{const r='+surface+'.getBoundingClientRect();return{x:r.x,y:r.y,width:r.width,height:r.height};})()');assert.deepEqual(await evaluate(geometry+'.at(-1).payload.rect'),actual);assert.equal(await evaluate(geometry+'.at(-1).payload.rasterizationScale'),1.5);
  await evaluate('window.modelCQa.outcome="accepted";window.modelCQa.emit("PreviewGeometryInvalidated",{})');await wait(surface+'.dataset.modelCDesiredOutcome==="accepted"');
 });
 await check('Real production menu paints over retained Preview with focus, click and Escape, without suspension',async()=>{
  await evaluate('window.modelCQa.outcome="transient-layout-pending"');const point=await evaluate('(()=>{const r=document.querySelector(".session-project-trigger").getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2};})()');
  await send('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...point});await send('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...point});await wait('document.querySelector("dialog[open]")');
  await evaluate('(()=>{const r='+surface+'.getBoundingClientRect(),dialog=document.querySelector("dialog[open]");dialog.style.left=(r.x+20)+"px";dialog.style.top=(r.y+20)+"px";})()');await rest();
  const facts=await evaluate('(()=>{const d=document.querySelector("dialog[open]"),r=d.getBoundingClientRect();return{point:document.elementFromPoint(r.x+20,r.y+20)?.closest("dialog")===d,focus:d.contains(document.activeElement),opaque:getComputedStyle(d).backgroundColor};})()');assert(facts.point&&facts.focus);assert.notEqual(facts.opaque,'rgba(0, 0, 0, 0)');
  await wait(geometry+'.at(-1).payload.occlusion.rects.length>0');assert.equal(await evaluate(geometry+'.at(-1).payload.visible'),true);assert.equal(await evaluate(surface+'.dataset.nativePreviewState'),'visible');assert.equal(await evaluate(surface+'.dataset.modelCLifecycleValid'),'true');
  await send('Input.dispatchKeyEvent',{type:'keyDown',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});await send('Input.dispatchKeyEvent',{type:'keyUp',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});await wait('!document.querySelector("dialog[open]")');assert.equal(await evaluate('document.activeElement?.classList.contains("session-project-trigger")'),true);assert.equal(await evaluate(surface+'.dataset.nativePreviewState'),'visible');
  await wait(geometry+'.at(-1).payload.occlusion.rects.length===0');await rest();
 });
 await check('Content replacement and lifecycle invalidation update real DOM without geometry or full snapshots',async()=>{
  const before=await evaluate('window.modelCQa.stats()');await evaluate('window.modelCQa.replace()');await rest();assert.equal(JSON.parse(await evaluate(surface+'.dataset.modelCAccepted')).contentGeneration,'2');assert.deepEqual(await evaluate('window.modelCQa.stats()'),before);
  await evaluate('window.modelCQa.invalidate()');await wait(surface+'.dataset.modelCLifecycleValid==="false"');assert.equal(await evaluate(surface+'.dataset.nativePreviewState'),'suspended');assert.equal(await evaluate('getComputedStyle('+surface+').backgroundColor'),'rgb(18, 21, 26)');assert.deepEqual(await evaluate('window.modelCQa.stats()'),before);
  await evaluate('window.modelCQa.outcome="accepted";window.modelCQa.emit("PreviewGeometryInvalidated",{})');await wait(surface+'.dataset.nativePreviewState==="visible"');assert.equal(JSON.parse(await evaluate(surface+'.dataset.modelCAccepted')).contentGeneration,'2');
 });
 await check('Fresh page starts from native projection and fences old page facts without replaying domain writes',async()=>{
  const next=crypto.randomUUID();await load(next);assert.equal(JSON.parse(await evaluate(surface+'.dataset.modelCAccepted')).page,next);
  const before=await evaluate(surface+'.dataset.modelCAcceptedThrough');await evaluate('window.modelCQa.emit("PreviewPresentationState",{acceptedThrough:"999",accepted:null,lifecycleValid:false,reason:"stale-page",apertureOwner:"host-composition-clip"},{generation:'+JSON.stringify(generation)+'})');await rest();assert.equal(await evaluate(surface+'.dataset.modelCAcceptedThrough'),before);assert.equal((await evaluate('window.modelCQa.stats()')).snapshots,1);
  assert(await evaluate('window.modelCQa.sent.every(c=>["GetMainWindowSnapshot","GetRuntimePlaybackSample","SetPreviewRect"].includes(c.type))'));
 });
 const responsiveFacts=()=>evaluate(`(()=>{const w=document.querySelector('.session-workspace'),p=document.querySelector('.preview-workspace'),l=document.querySelector('.session-library'),i=document.querySelector('.session-inspector'),rect=e=>{const r=e.getBoundingClientRect();return{x:r.x,width:r.width};};return{workspace:rect(w),viewportWidth:innerWidth,mode:w.dataset.layoutMode,tracks:getComputedStyle(w).gridTemplateColumns.split(' ').filter(Boolean),gap:getComputedStyle(w).columnGap,library:getComputedStyle(l).display,inspector:getComputedStyle(i).display,preview:rect(p),left:rect(l),right:rect(i),accepted:JSON.parse(${surface}.dataset.modelCAccepted),transparent:getComputedStyle(${surface}).backgroundColor};})()`);
 async function holdAt(width){await send('Emulation.setDeviceMetricsOverride',{width,height:820,deviceScaleFactor:1,mobile:false});await load();await evaluate('window.qaHoldResponsive=true;window.modelCQa.outcome="transient-layout-pending"');return responsiveFacts();}
 async function resizeHeld(width){await send('Emulation.setDeviceMetricsOverride',{width,height:820,deviceScaleFactor:1,mobile:false});await rest();return responsiveFacts();}
 await check('1100 shrink keeps two CSS tracks when old React mode still has three columns',async()=>{const before=await holdAt(1304),after=await resizeHeld(1100);assert.equal(after.mode,'three-column');assert.equal(after.library,'none');assert.equal(after.tracks.length,2);assert.deepEqual(after.preview,{x:0,width:836});assert.deepEqual(after.right,{x:836,width:264});assert.deepEqual(after.accepted,before.accepted);assert.equal(after.transparent,'rgba(0, 0, 0, 0)');});
 await check('1100 grow restores three CSS tracks and splitter gap before React mode catches up',async()=>{await holdAt(1100);const after=await resizeHeld(1304);assert.equal(after.mode,'preview-inspector');assert.notEqual(after.library,'none');assert.equal(after.tracks.length,3);assert.equal(after.gap,'5px');assert.deepEqual(after.left,{x:0,width:230});assert.equal(after.preview.x,235);});
 await check('760 shrink hides both panels with one CSS track even while React is still wide',async()=>{const before=await holdAt(1304),after=await resizeHeld(760);assert.equal(after.mode,'three-column');assert.equal(after.library,'none');assert.equal(after.inspector,'none');assert.equal(after.tracks.length,1);assert.deepEqual(after.preview,{x:0,width:760});assert.deepEqual(after.accepted,before.accepted);});
 await check('760 grow restores matching panel participation and all three tracks from stale narrow React mode',async()=>{await holdAt(760);const after=await resizeHeld(1304);assert.equal(after.mode,'preview-only');assert.notEqual(after.library,'none');assert.notEqual(after.inspector,'none');assert.equal(after.tracks.length,3);assert.equal(after.gap,'5px');assert.deepEqual(after.left,{x:0,width:230});});
 async function assertGapCoverage(){const gaps=await evaluate(`(()=>{const w=document.querySelector('.session-workspace'),r=w.getBoundingClientRect(),s=getComputedStyle(w),columns=s.gridTemplateColumns.split(' ').map(parseFloat),gap=parseFloat(s.columnGap);return ['left','right'].map(side=>{const preview=document.querySelector('.preview-workspace').getBoundingClientRect(),x=side==='left'?preview.x-gap:preview.right,y=r.y+Math.min(120,r.height/2),e=document.elementFromPoint(x+gap/2,y),b=e?.getBoundingClientRect(),paint=e&&getComputedStyle(e);return{side,x,gap,owner:e?.getAttribute('data-layout-splitter'),bounds:b&&{x:b.x,y:b.y,width:b.width,height:b.height},workspace:{y:r.y,height:r.height},background:paint?.backgroundColor,opacity:paint?.opacity};});})()`);for(const g of gaps){assert.equal(g.owner,g.side,'Each desktop gap has its actual opaque splitter owner');assert.equal(g.gap,5);assert.deepEqual(g.bounds,{x:g.x,y:g.workspace.y,width:5,height:g.workspace.height});assert.equal(g.background,'rgb(23, 28, 35)');assert.equal(g.opacity,'1');}return gaps;}
 await check('splitter coverage after 1100 grow exists before the React layout mode catches up',async()=>{await holdAt(1100);await resizeHeld(1304);await assertGapCoverage();});
 await check('splitter coverage after 760 grow exists before the React layout mode catches up',async()=>{await holdAt(760);await resizeHeld(1304);await assertGapCoverage();});
 await check('preferred-width splitter coverage follows actual grid lines while React mode is stale',async()=>{
  await send('Emulation.setDeviceMetricsOverride',{width:1920,height:820,deviceScaleFactor:1,mobile:false});await load();
  async function drag(side,target){const point=await evaluate('(()=>{const r=document.querySelector("[data-layout-splitter='+side+']").getBoundingClientRect();return{x:r.x+2.5,y:r.y+120};})()');await send('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',buttons:1,clickCount:1,...point});await send('Input.dispatchMouseEvent',{type:'mouseMoved',button:'left',buttons:1,x:target,y:point.y});await send('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',buttons:0,clickCount:1,x:target,y:point.y});await rest();}
  await drag('left',702.5);await drag('right',1217.5);const wide=await responsiveFacts();assert.equal(wide.left.width,700);assert.equal(wide.right.width,700);
  await send('Emulation.setDeviceMetricsOverride',{width:1100,height:820,deviceScaleFactor:1,mobile:false});await rest();await evaluate('window.qaHoldResponsive=true;window.modelCQa.outcome="transient-layout-pending"');const grown=await resizeHeld(1304);assert.equal(grown.mode,'preview-inspector');assert.equal(grown.tracks.length,3);assert.equal(grown.left.width,700);assert.equal(grown.right.width,700);const gaps=await assertGapCoverage();return{wide,grown,gaps};
 });
 const splitterState=()=>evaluate(`(()=>{const workspace=document.querySelector('.session-workspace');return {dragging:workspace.dataset.layoutDragging,left:Number(workspace.dataset.layoutLeft),right:Number(workspace.dataset.layoutRight)};})()`);
 async function splitterPointer(type,point,pressed){
  try{await send('Input.dispatchMouseEvent',{type,...point,button:type==='mouseMoved'&&!pressed?'none':'left',buttons:pressed?1:0,clickCount:type==='mouseMoved'?0:1});}
  catch(error){throw new Error(`Rejected splitter ${type} at ${JSON.stringify(point)}: ${error.message}`);}
  await rest();
 }
 async function startSplitter(side){
  const selector=JSON.stringify('[data-layout-splitter='+side+']');
  const point=await evaluate(`(()=>{const control=document.querySelector(${selector}),rect=control.getBoundingClientRect();control.addEventListener('pointerdown',event=>{window.qaSplitterPointer=event.pointerId;},{once:true});return {x:rect.left+rect.width/2,y:rect.top+120};})()`);
  await splitterPointer('mousePressed',point,true);
  assert.equal((await splitterState()).dragging,side);
  return point;
 }
 async function loadSplitters(){
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await load();
 }
 await check('Splitter blur ends capture and the next gesture can resize the other pane',async()=>{
  await loadSplitters();
  const before=await splitterState(),point=await startSplitter('left');
  const outside={x:point.x+70,y:30};
  try{
   await splitterPointer('mouseMoved',outside,true);
   assert((await splitterState()).left>before.left);
   await evaluate('window.dispatchEvent(new Event("blur"))');
   await rest();
   const blurred=await splitterState();
   assert.equal(blurred.dragging,'');
   assert.equal(await evaluate('document.querySelector("[data-layout-splitter=left]").hasPointerCapture(window.qaSplitterPointer)'),false);
   await splitterPointer('mouseMoved',{x:point.x+130,y:point.y},true);
   assert.deepEqual(await splitterState(),blurred);
  }finally{await splitterPointer('mouseReleased',outside,false);}
  const next=await startSplitter('right'),release={x:next.x-70,y:30};
  try{await splitterPointer('mouseMoved',release,true);}
  finally{await splitterPointer('mouseReleased',release,false);}
  const final=await splitterState();
  assert.equal(final.dragging,'');
  assert(final.right>before.right);
  assert(await evaluate('Array.from(document.querySelectorAll(".session-splitter")).every(control=>getComputedStyle(control).cursor==="col-resize")'));
 });
 await check('Splitter ignores another pointer capture loss and clears a missed-release reentry',async()=>{
  await loadSplitters();
  const point=await startSplitter('left');
  try{
   await evaluate('document.querySelector("[data-layout-splitter=right]").dispatchEvent(new PointerEvent("lostpointercapture",{bubbles:true,pointerId:window.qaSplitterPointer+1}))');
   assert.equal((await splitterState()).dragging,'left');
   const outside={x:point.x+50,y:30};
   await splitterPointer('mouseMoved',outside,true);
   const width=(await splitterState()).left;
   // Model the first pointer sample when a native release was lost outside the window.
   await evaluate(`document.querySelector("[data-layout-splitter=left]").dispatchEvent(new PointerEvent("pointermove",{bubbles:true,pointerId:window.qaSplitterPointer,buttons:0,clientX:${outside.x+45},clientY:${point.y}}))`);
   await rest();
   assert.equal((await splitterState()).dragging,'');
   assert.equal((await splitterState()).left,width);
  }finally{await splitterPointer('mouseReleased',point,false);}
 });
 await check('Splitter keeps its owner during rerenders and clears drag on cancel or responsive collapse',async()=>{
  await loadSplitters();
  const point=await startSplitter('right');
  const outside={x:point.x-50,y:30};
  try{
   await evaluate('void(window.qaSplitterNode=document.querySelector("[data-layout-splitter=right]"))');
   await splitterPointer('mouseMoved',outside,true);
   assert(await evaluate('window.qaSplitterNode===document.querySelector("[data-layout-splitter=right]")'));
   assert.equal((await splitterState()).dragging,'right');
   await evaluate('window.qaSplitterNode.dispatchEvent(new PointerEvent("pointercancel",{bubbles:true,pointerId:window.qaSplitterPointer}))');
   await rest();
   assert.equal((await splitterState()).dragging,'');
   assert.equal(await evaluate('window.qaSplitterNode.hasPointerCapture(window.qaSplitterPointer)'),false);
  }finally{await splitterPointer('mouseReleased',outside,false);}
  const second=await startSplitter('left');
  try{
   await send('Emulation.setDeviceMetricsOverride',{width:1100,height:900,deviceScaleFactor:1,mobile:false});
   await wait('document.querySelector(".session-workspace").dataset.layoutMode==="preview-inspector"');
   assert.equal((await splitterState()).dragging,'');
  }finally{await splitterPointer('mouseReleased',second,false);}
 });
 assert.deepEqual(errors,[]);await send('Browser.close');
}catch(error){checks.push({name:'DOM harness',pass:false,error:error.stack??String(error)});}finally{
 socket?.close();vite.kill();for(let i=0;i<20&&chrome.exitCode===null;i++)await pause(100);if(chrome.exitCode===null)chrome.kill();
 const target=resolve(profile),base=resolve(tmpdir())+sep;if(!target.startsWith(base)||!target.slice(base.length).startsWith('dse-model-c-m2-frontend-'))throw new Error('Unsafe QA profile cleanup');rmSync(target,{recursive:true,force:true,maxRetries:5,retryDelay:100});
 writeFileSync(new URL('model-c-dom.json',evidence),JSON.stringify({checks,pass:checks.every(value=>value.pass),errors,serverLog,proofScope:'actual production React/DOM + controlled wire; native/real media proved separately'},null,2)+'\n');
}
for(const value of checks)console.log((value.pass?'PASS ':'FAIL ')+value.name+(value.error?'\n'+value.error:''));console.log(checks.filter(value=>value.pass).length+'/'+checks.length);if(checks.some(value=>!value.pass))process.exitCode=1;
