import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {mkdirSync,writeFileSync,mkdtempSync,rmSync,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve,sep} from 'node:path';
import {fixturePreviewReply,generation,incarnation,ids,productionSessionUrl,snapshot} from './session-readonly-fixture.mjs';

// The actual production React tree, with a controlled native wire peer. Native
// Engine/sequence/decoder correctness is established separately by native proof.
const origin='http://127.0.0.1:9667',debug='http://127.0.0.1:9668',red=process.argv.includes('--red');
const evidence=new URL('../node_modules/.cache/current-contract/check-hover-request-gate-dom/',import.meta.url);mkdirSync(evidence,{recursive:true});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
for(const url of [origin,debug+'/json/list'])await assert.rejects(fetch(url),'Owned B4 QA port must be free: '+url);
const server=spawn(process.execPath,[fileURLToPath(new URL('../node_modules/vite/bin/vite.js',import.meta.url)),'--host','127.0.0.1','--port','9667','--strictPort'],{cwd:fileURLToPath(new URL('../',import.meta.url)),windowsHide:true,stdio:'ignore'});
const profile=mkdtempSync(join(tmpdir(),'dse-f3-gate-dom-'));
const browser=spawn(chromeExecutable,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-address=127.0.0.1','--remote-debugging-port=9668','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:'ignore'});
const checks=[],errors=[];let socket;
const sample=(patch={})=>({bindingToken:'binding-1',asOfEffectSequence:'9007199254740993',asOfTimeNs:'9007199254740995',active:{playlistId:ids.b,itemId:ids.video},hasMedia:true,state:'playing',position:3,duration:20,actualRate:1.1,outputMuted:false,canPlayPause:true,canPrevious:true,canNext:true,canSeek:true,canMute:true,sceneIndex:0,sceneCount:1,mediaKind:'video',...patch});
function base(){const value=snapshot();value.documentOperation={pending:null,last:null};value.mediaImport={pending:null,last:null};value.audioEffects={version:1,documentIncarnation:'30000000-0000-0000-0000-000000000001',execution:'1',capacityRevision:'1',closing:false,stopped:false,desiredMuted:false,appliedMuted:false,muteKnown:true,deviceHealth:'healthy',effects:[]};return value;}
try{
 let pages;for(let n=0;n<100;n++){try{await fetch(origin);pages=await(await fetch(debug+'/json/list')).json();if(pages.some(page=>page.type==='page'))break;}catch{}await pause(100);}
 const page=pages?.find(page=>page.type==='page');assert(page,'Owned B4 browser/server started');socket=new WebSocket(page.webSocketDebuggerUrl);
 await new Promise((yes,no)=>{socket.addEventListener('open',yes,{once:true});socket.addEventListener('error',no,{once:true});});
 let serial=0;const pending=new Map();socket.addEventListener('message',async event=>{const value=JSON.parse(typeof event.data==='string'?event.data:await event.data.text());if(value.method==='Runtime.exceptionThrown')errors.push(value.params.exceptionDetails);if(value.method==='Runtime.consoleAPICalled'&&['error','warning'].includes(value.params.type))errors.push(value.params);if(value.method==='Log.entryAdded'&&['error','warning'].includes(value.params.entry.level))errors.push(value.params.entry);
  const request=pending.get(value.id);if(request){pending.delete(value.id);value.error?request.no(new Error(JSON.stringify(value.error))):request.yes(value.result);}});
 const send=(method,params={})=>new Promise((yes,no)=>{const id=++serial;pending.set(id,{yes,no});socket.send(JSON.stringify({id,method,params}));});
 const evaluate=async expression=>{const value=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});assert(!value.exceptionDetails,JSON.stringify(value.exceptionDetails));return value.result.value;};
 async function wait(expression){for(let n=0;n<100;n++){if(await evaluate('Boolean('+expression+')'))return;await pause(40);}assert.fail('DOM wait: '+expression+' BODY='+await evaluate('document.body.innerText'));}
 async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
 await Promise.all([send('Page.enable'),send('Runtime.enable'),send('Log.enable'),send('Network.enable')]);await send('Network.setCacheDisabled',{cacheDisabled:true});await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});await send('Emulation.setFocusEmulationEnabled',{enabled:true});
 await send('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{const listeners=new Set(),q=window.b4Qa={value:${JSON.stringify(base())},sample:${JSON.stringify(sample())},sent:[],hold:false,held:[],audioEffectsNegotiated:false,
 deliver(command,payload,patch={}){for(const fn of listeners)fn({data:{kind:'result',version:1,requestId:command.requestId,type:command.type,generation:command.generation,sessionIncarnation:command.sessionIncarnation,ok:true,payload,...patch}});},
 emit(type,payload,patch={}){for(const fn of listeners)fn({data:{kind:'event',version:1,type,generation:${JSON.stringify(generation)},sessionIncarnation:${JSON.stringify(incarnation)},payload,...patch}});},
 runtime(patch){Object.assign(q.sample,patch);q.emit('RuntimePlaybackSample',structuredClone(q.sample));},release(){for(const fn of q.held.splice(0))fn();q.hold=false;}};
 const replyPreview=(${fixturePreviewReply.toString()})();window.chrome??={};window.chrome.webview={postMessage(command){q.sent.push(command);if(command.type==='GetMainWindowSnapshot'){queueMicrotask(()=>q.deliver(command,q.failRead?{}:structuredClone(q.value)));return;}if(command.type==='SetPreviewRect'){queueMicrotask(()=>q.deliver(command,replyPreview(command)));return;}
 if(command.type==='GetRuntimePlaybackSample'){queueMicrotask(()=>q.deliver(command,structuredClone(q.sample)));return;}
 if(command.type==='PresentTimelineHover'){queueMicrotask(()=>q.deliver(command,{status:q.presentationFailure?'unavailable':'shown',serial:command.payload.serial}));return;}
 if(command.type==='RequestTimelineHover'||command.type==='CancelTimelineHover'){queueMicrotask(()=>q.deliver(command,{status:command.type==='RequestTimelineHover'?'pending':'cancelled',serial:command.payload.serial}));return;}
 if(command.type==='NegotiateAudioEffects'||command.type==='GetAudioEffects'){if(command.type==='NegotiateAudioEffects'){if(JSON.stringify(command.payload)!==JSON.stringify({version:1}))throw new Error('Invalid audioEffects negotiation');q.audioEffectsNegotiated=true;}else if(!q.audioEffectsNegotiated||Object.keys(command.payload).length)throw new Error('Invalid audioEffects read');queueMicrotask(()=>q.deliver(command,structuredClone(q.value.audioEffects)));return;}
 if(!q.audioEffectsNegotiated||!command.audioEffects||Object.keys(command.audioEffects).length!==2||command.audioEffects.version!==1||command.audioEffects.documentIncarnation!==q.value.audioEffects.documentIncarnation)throw new Error('Affected mutation lacks negotiated native document stamp');
 const deliver=()=>q.deliver(command,{status:'applied',message:'',sample:structuredClone(q.sample)});if(q.hold)q.held.push(deliver);else queueMicrotask(deliver);
 },addEventListener(_,fn){if(listeners.has(fn))throw new Error('Duplicate handler');listeners.add(fn);},removeEventListener(_,fn){if(!listeners.delete(fn))throw new Error('Unbalanced handler');}};})();`});
 const url=productionSessionUrl(origin);
 const q=selector=>'document.querySelector('+JSON.stringify(selector)+')';
 const commands=()=>evaluate('window.b4Qa.sent.filter(c=>!["GetMainWindowSnapshot","SetPreviewRect","GetRuntimePlaybackSample","RequestTimelineHover","CancelTimelineHover","PresentTimelineHover","NegotiateAudioEffects","GetAudioEffects"].includes(c.type))');
 const rest=()=>evaluate('new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))');
 const click=async selector=>{await evaluate(q(selector)+'.click()');await rest();};
 const runtime=async patch=>{await evaluate('window.b4Qa.runtime('+JSON.stringify(patch)+')');await rest();};
 const point=(x=.5)=>evaluate('(()=>{const r=document.querySelector(".timeline-groove").getBoundingClientRect();return{x:r.left+(r.width-1)*'+x+',y:r.top+1};})()');
 async function pointer(type,x=.5,down=false){await send('Input.dispatchMouseEvent',{type,...await point(x),button:type==='mouseMoved'?down?'left':'none':'left',buttons:down?1:0,clickCount:type==='mouseMoved'?0:1});await rest();}
 async function load(){await send('Page.navigate',{url});await wait('document.querySelector(".production-session")?.dataset.bootstrapState==="ready"');await rest();}
 async function setup(){
  const marker=crypto.randomUUID();await evaluate('window.gateMarker='+JSON.stringify(marker));await send('Page.navigate',{url});await wait('window.gateMarker!=='+JSON.stringify(marker)+'&&document.querySelector(".production-session")?.dataset.bootstrapState==="ready"');await rest();
  await evaluate(`(()=>{const realSet=setTimeout.bind(window),realClear=clearTimeout.bind(window);let id=1000000;const c=window.gateClock={now:0,frames:new Map(),timers:new Map(),rawMoves:0};
   Object.defineProperty(performance,'now',{value:()=>c.now,configurable:true});
   window.requestAnimationFrame=fn=>{const n=++id;c.frames.set(n,fn);return n;};window.cancelAnimationFrame=n=>c.frames.delete(n);
   window.setTimeout=(fn,ms,...args)=>{if(ms>0&&ms<=75){const n=++id;c.timers.set(n,{due:c.now+ms,fn:()=>fn(...args)});return n;}return realSet(fn,ms,...args);};window.clearTimeout=n=>{if(!c.timers.delete(n))realClear(n);};
   c.frame=time=>{if(time<c.now)throw Error('Clock cannot reverse');c.now=time;for(const[n,v]of [...c.timers])if(v.due<=time){c.timers.delete(n);v.fn();}const batch=[...c.frames.values()];c.frames.clear();for(const fn of batch)fn(time);};
   c.move=f=>{const e=document.querySelector('.timeline-control'),r=document.querySelector('.timeline-groove').getBoundingClientRect();++c.rawMoves;e.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,pointerId:1,pointerType:'mouse',isPrimary:true,clientX:r.x+(r.width-1)*f,clientY:r.y+1,buttons:0}));};
   c.leave=()=>document.querySelector('.timeline-control').dispatchEvent(new PointerEvent('pointerout',{bubbles:true,pointerId:1,pointerType:'mouse',relatedTarget:document.body}));
   c.complete=(request,status='ready')=>window.b4Qa.emit('TimelineHoverResult',{...request.payload,status});
  })()`);
 }
 const hoverRequests=()=>evaluate('window.b4Qa.sent.filter(c=>c.type==="RequestTimelineHover")');
 const state=()=>evaluate('({...document.querySelector(".session-timeline").dataset})');
 const move=f=>evaluate('window.gateClock.move('+f+')');
 const pump=async time=>{for(let i=0;i<5;i++)await evaluate('window.gateClock.frame('+time+')');};
 await check('Idle first target dispatches on a frame without the original75ms dwell',async()=>{
  await setup();await move(.2);await pump(16);const requests=await hoverRequests();
  assert.equal(requests.length,1,'Meaningful RED: original fixed timer emits zero requests after frame16, not the required one');
  assert.equal(requests[0].payload.serial,Number((await state()).serial));
 });
 if(!red){
 await check('Raw bursts coalesce; remaining budget sends final latest target without restart/starvation',async()=>{
  await setup();for(let i=0;i<120;i++)await move(.1+i*.005);await pump(16);let r=await hoverRequests();assert.equal(r.length,1);const a=r[0];
  await evaluate('window.gateClock.complete('+JSON.stringify(a)+')');await pump(16);assert.equal((await state()).hoverStatus,'ready');
  await evaluate('window.gateClock.now=20');await move(.3);await move(.8);await pump(20);assert.equal((await hoverRequests()).length,1);
  const latest=Number((await state()).serial);await evaluate('window.gateClock.complete('+JSON.stringify(a)+')');assert.equal((await state()).hoverStatus,'loading','Late A cannot relabel current target ready');
  await pump(90);assert.equal((await hoverRequests()).length,1);await pump(91);r=await hoverRequests();assert.equal(r.length,2);assert.equal(r[1].payload.serial,latest);
  const shown=await evaluate('window.b4Qa.sent.filter(c=>c.type==="PresentTimelineHover").at(-1).payload');assert.equal(r[1].payload.time,shown.time);
  await evaluate('window.gateClock.complete('+JSON.stringify(r[1])+')');assert.equal((await state()).hoverStatus,'ready');
  assert((await evaluate('window.gateClock.rawMoves'))>r.length);
 });
 await check('Cooldown expiry before a new layout Present does not starve the stopped latest pointer',async()=>{
  await setup();await move(.2);await pump(16);await evaluate('window.gateClock.now=20');await move(.4);await pump(20);
  await evaluate('window.gateClock.now=90;window.gateClock.move(.9);window.gateClock.frame(91)');
  assert.equal((await hoverRequests()).length,1,'Expiry in the same event turn must not send a target whose current Present has not committed');
  await pump(92);
  const r=await hoverRequests();assert.equal(r.length,2);assert.equal(r.at(-1).payload.serial,Number((await state()).serial));
 });
 await check('Pointer leave cancels timer/frame; reentry retains rate budget and late results cannot resurrect',async()=>{
  await setup();await move(.2);await pump(16);const a=(await hoverRequests())[0];
  for(const time of[20,30,40,60]){await evaluate('window.gateClock.now='+time+';window.gateClock.leave()');assert.equal((await state()).hoverActive,'false');await move(.7);await pump(time);assert.equal((await hoverRequests()).length,1);}
  await evaluate('window.gateClock.leave()');await pump(200);await evaluate('window.gateClock.complete('+JSON.stringify(a)+')');assert.equal((await state()).hoverActive,'false');assert.equal((await hoverRequests()).length,1);
  await move(.9);await pump(216);assert.equal((await hoverRequests()).length,2);
  await setup();await move(.3);await evaluate('window.gateClock.leave()');await pump(100);assert.equal((await hoverRequests()).length,0,'Leave before the first frame cancels it');
 });
 await check('Source invalidation and pagehide cancel scheduled work; new source keeps native identity',async()=>{
  await setup();await move(.2);await evaluate('window.b4Qa.runtime({bindingToken:"binding-2",active:{playlistId:'+JSON.stringify(ids.a)+',itemId:'+JSON.stringify(ids.image)+'},mediaKind:"image"})');await pump(16);assert.equal((await hoverRequests()).length,0);assert.equal((await state()).hoverActive,'false');
  await move(.6);await pump(32);const r=await hoverRequests();assert.equal(r.length,1);assert.equal(r[0].payload.bindingToken,'binding-2');assert.equal(r[0].payload.itemId,ids.image);
  await move(.8);await evaluate('window.dispatchEvent(new Event("pagehide"))');await pump(200);assert.equal((await hoverRequests()).length,1);assert.equal((await state()).hoverActive,'false');
 });
 await check('No browser error/warning or obsolete DOM thumbnail',async()=>{assert.deepEqual(errors,[]);assert.equal(await evaluate('document.querySelectorAll(".timeline-hover-overlay,.timeline-hover-popup,.timeline-thumbnail img").length'),0);});
 }
}catch(error){checks.push({name:'Harness',pass:false,error:error.stack??String(error)});}
finally{
 try{socket?.close();}catch{}browser.kill();server.kill();
 await Promise.all([new Promise(r=>browser.exitCode!==null?r():browser.once('exit',r)),new Promise(r=>server.exitCode!==null?r():server.once('exit',r))]);
 const absolute=resolve(profile);assert(absolute.startsWith(resolve(tmpdir())+sep)&&absolute.split(sep).at(-1).startsWith('dse-f3-gate-dom-'));for(let n=0;n<20;n++){try{rmSync(absolute,{recursive:true,force:true});break;}catch{await pause(100);}}
 const report={pass:checks.length>0&&checks.every(c=>c.pass),checks,errors,clock:'Controlled RAF/monotonic clock/short timeout scheduling, not wall-clock performance evidence',scope:'Actual production React + existing facade against controlled native wire; native image retention is separately verified',profileRetained:existsSync(absolute)};
 writeFileSync(new URL(red?'gate-red.json':'gate-green.json',evidence),JSON.stringify(report,null,2));for(const c of checks)console.log((c.pass?'PASS':'FAIL')+' '+c.name+(c.error?'\n'+c.error:''));console.log(checks.filter(c=>c.pass).length+'/'+checks.length);if(!report.pass||report.profileRetained)process.exitCode=1;
}
