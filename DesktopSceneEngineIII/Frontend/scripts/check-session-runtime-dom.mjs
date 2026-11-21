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
const origin='http://127.0.0.1:9434',debug='http://127.0.0.1:9399',red=process.argv.includes('--red');
const evidence=new URL('../node_modules/.cache/current-contract/check-session-runtime-dom/',import.meta.url);mkdirSync(evidence,{recursive:true});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
for(const url of [origin,debug+'/json/list'])await assert.rejects(fetch(url),'Owned B4 QA port must be free: '+url);
const server=spawn(process.execPath,[fileURLToPath(new URL('../node_modules/vite/bin/vite.js',import.meta.url)),'--host','127.0.0.1','--port','9434','--strictPort'],{cwd:fileURLToPath(new URL('../',import.meta.url)),windowsHide:true,stdio:'ignore'});
const profile=mkdtempSync(join(tmpdir(),'dse-b4-frontend-'));
const browser=spawn(chromeExecutable,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-address=127.0.0.1','--remote-debugging-port=9399','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:'ignore'});
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
 runtime(patch){Object.assign(q.sample,patch);q.emit('RuntimePlaybackSample',structuredClone(q.sample));},release(){for(const fn of q.held.splice(0))fn();q.hold=false;},
 completeSeek(patch,status='applied'){const command=q.held.shift();if(!command)throw new Error('No held runtime command');command({status,message:'',sample:{...q.sample,...patch}});}};
 const replyPreview=(${fixturePreviewReply.toString()})();window.chrome??={};window.chrome.webview={postMessage(command){q.sent.push(command);if(command.type==='GetMainWindowSnapshot'){queueMicrotask(()=>q.deliver(command,q.failRead?{}:structuredClone(q.value)));return;}if(command.type==='SetPreviewRect'){queueMicrotask(()=>q.deliver(command,replyPreview(command)));return;}
 if(command.type==='GetRuntimePlaybackSample'){queueMicrotask(()=>q.deliver(command,structuredClone(q.sample)));return;}
 if(command.type==='PresentTimelineHover'){queueMicrotask(()=>q.deliver(command,{status:q.presentationFailure?'unavailable':'shown',serial:command.payload.serial}));return;}
 if(command.type==='RequestTimelineHover'||command.type==='CancelTimelineHover'){queueMicrotask(()=>q.deliver(command,{status:command.type==='RequestTimelineHover'?'pending':'cancelled',serial:command.payload.serial}));return;}
 if(command.type==='NegotiateAudioEffects'||command.type==='GetAudioEffects'){if(command.type==='NegotiateAudioEffects'){if(JSON.stringify(command.payload)!==JSON.stringify({version:1}))throw new Error('Invalid audioEffects negotiation');q.audioEffectsNegotiated=true;}else if(!q.audioEffectsNegotiated||Object.keys(command.payload).length)throw new Error('Invalid audioEffects read');queueMicrotask(()=>q.deliver(command,structuredClone(q.value.audioEffects)));return;}
 if(!q.audioEffectsNegotiated||!command.audioEffects||Object.keys(command.audioEffects).length!==2||command.audioEffects.version!==1||command.audioEffects.documentIncarnation!==q.value.audioEffects.documentIncarnation)throw new Error('Affected mutation lacks negotiated native document stamp');
 const deliver=(reply={status:'applied',message:'',sample:structuredClone(q.sample)})=>q.deliver(command,reply);if(q.hold)q.held.push(deliver);else queueMicrotask(deliver);
 },addEventListener(_,fn){if(listeners.has(fn))throw new Error('Duplicate handler');listeners.add(fn);},removeEventListener(_,fn){if(!listeners.delete(fn))throw new Error('Unbalanced handler');}};
 document.addEventListener('pointerdown',event=>{if(event.isTrusted)q.pointerId=event.pointerId;},true);})();`});
 const url=productionSessionUrl(origin);
 const q=selector=>'document.querySelector('+JSON.stringify(selector)+')';
 const commands=()=>evaluate('window.b4Qa.sent.filter(c=>!["GetMainWindowSnapshot","SetPreviewRect","GetRuntimePlaybackSample","RequestTimelineHover","CancelTimelineHover","PresentTimelineHover","NegotiateAudioEffects","GetAudioEffects"].includes(c.type))');
 const rest=()=>evaluate('new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))');
 const click=async selector=>{await evaluate(q(selector)+'.click()');await rest();};
 const runtime=async patch=>{await evaluate('window.b4Qa.runtime('+JSON.stringify(patch)+')');await rest();};
 const point=(x=.5)=>evaluate('(()=>{const r=document.querySelector(".timeline-groove").getBoundingClientRect();return{x:r.left+(r.width-1)*'+x+',y:r.top+1};})()');
 async function pointer(type,x=.5,down=false){await send('Input.dispatchMouseEvent',{type,...await point(x),button:type==='mouseMoved'?down?'left':'none':'left',buttons:down?1:0,clickCount:type==='mouseMoved'?0:1});await rest();}
 async function load(){await send('Page.navigate',{url});await wait('document.querySelector(".production-session")?.dataset.bootstrapState==="ready"');await rest();}
 await check('B4 bootstraps one native runtime subscription and projects native active/rate/clock without retargeting authored selection',async()=>{
  await load();await wait('window.b4Qa.sent.some(c=>c.type==="GetRuntimePlaybackSample")');assert.equal(await evaluate('window.b4Qa.sent.filter(c=>c.type==="GetRuntimePlaybackSample").length'),1);
  assert.equal(await evaluate(q('.production-session')+'.dataset.activeItemId'),ids.video);assert.equal(await evaluate(q('.production-session')+'.dataset.selectedItemId'),ids.video);assert.equal(await evaluate(q('.session-inspector')+'.dataset.authoredRate'),'1.25');assert.equal(await evaluate(q('[aria-label="Actual Rate"]')+'.textContent'),'1.10×');assert.equal(await evaluate(q('.session-timeline')+'.dataset.runtimeTime'),'3');
 });
 await check('Release keeps the chosen position enabled; a second drag is immediately responsive while native confirmation is held',async()=>{
  await load();await evaluate('window.b4Qa.hold=true');
  const slider=q('.timeline-control');
  await pointer('mousePressed',.25,true);await pointer('mouseMoved',.6,true);
  const requested=await evaluate(slider+'.getAttribute("aria-valuenow")');
  await pointer('mouseReleased',.6);
  assert.deepEqual(await evaluate('['+slider+'.getAttribute("aria-valuenow"),'+slider+'.getAttribute("aria-disabled")]'),[requested,'false'],
    'Held native result cannot flash back to the old sample or disable seek');
  await pointer('mousePressed',.2,true);await pointer('mouseMoved',.35,true);
  const second=await evaluate(slider+'.getAttribute("aria-valuenow")');
  assert.notEqual(second,requested);
  await pointer('mouseReleased',.35);
  assert.equal(await evaluate(slider+'.getAttribute("aria-valuenow")'),second);
  assert.equal((await commands()).length,1,'Only one native seek in flight');
 });
 if(!red)await load();
 if(!red){
 await check('An exact-end Next remains a structural barrier for its binding while releases on the successor remain usable',async()=>{
  const slider=q('.timeline-control'),timeline=q('.session-timeline');
  const value=()=>evaluate(slider+'.getAttribute("aria-valuenow")');
  async function gesture(fraction){
   await pointer('mousePressed',fraction,true);
   const preview=await value();
   await pointer('mouseReleased',fraction);
   assert.equal(await evaluate(slider+'.getAttribute("aria-disabled")'),'false');
   return preview;
  }
  await evaluate('window.b4Qa.hold=true');
  const first=await gesture(.55);
  await gesture(1);
  assert.equal(await value(),'10000','The exact endpoint retains its navigation target');
  assert.notEqual(await gesture(.2),'10000','Ordinary drag can still preview while Next waits');
  assert.equal(await value(),'10000','A later release on the old binding cannot replace queued Next');
  assert.equal((await commands()).length,1);
  await evaluate('window.b4Qa.completeSeek({position:'+Number(first)/500+',asOfTimeNs:"9007199254741100"})');
  await wait('window.b4Qa.held.length===1&&window.b4Qa.sent.filter(c=>c.type==="Seek").length===2');
  assert.deepEqual((await commands()).map(c=>c.payload.value),[Number(first),10000]);
  await gesture(.25);
  await gesture(1);
  assert.equal(await value(),'10000','Neither a regular nor repeated endpoint release can replace pending Next on the old binding');
  assert.equal((await commands()).length,2);
  const successor={bindingToken:'binding-successor',active:{playlistId:ids.a,itemId:ids.image},duration:25,position:1,asOfTimeNs:'9007199254741300'};
  await evaluate('window.b4Qa.completeSeek('+JSON.stringify(successor)+')');
  await wait(timeline+'.dataset.bindingToken==="binding-successor"');
  assert.equal(await value(),'400');
  assert.equal((await commands()).length,2,'The old binding emits one Next');
  const successorTarget=await gesture(.42);
  assert.deepEqual((await commands()).at(-1).payload,{bindingToken:'binding-successor',itemId:ids.image,duration:25,value:Number(successorTarget)});
  await load();
 });
 await check('Confirmed Seek ignores pre-seek events; rapid releases retain only the newest unsent target before truthful reconciliation',async()=>{
  const slider=q('.timeline-control'),timeline=q('.session-timeline');
  const value=()=>evaluate(slider+'.getAttribute("aria-valuenow")');
  const readTime=()=>evaluate(timeline+'.dataset.previewTime');
  const previewAndRelease=async fraction=>{
   await pointer('mousePressed',fraction,true);const held=await value();await pointer('mouseReleased',fraction);
   assert.equal(await value(),held);assert.equal(await evaluate(slider+'.getAttribute("aria-disabled")'),'false');
   return held;
  };
  await runtime({state:'paused'});
  await evaluate('window.b4Qa.hold=true');
  const first=await previewAndRelease(.55);
  for(let step=0;step<8;step++){
   const chosen=await previewAndRelease(.48+step*.04);
   await runtime({position:2.25,asOfTimeNs:String(9007199254740996n+BigInt(step))});
   assert.equal(await value(),chosen,'Stale playback observation cannot overwrite the newest released target');
  }
  const final=await value(),chosenTime=Number(await readTime());
  assert.equal((await commands()).length,1,'Burst must keep one in-flight command');
  await evaluate('window.b4Qa.completeSeek({position:'+Number(first)/500+',asOfTimeNs:"9007199254741100"})');
  await wait('window.b4Qa.held.length===1&&window.b4Qa.sent.filter(c=>c.type==="Seek").length===2');
  assert.equal(await value(),final,'Older command confirmation cannot flash the first target or stale position');
  assert.deepEqual((await commands()).map(c=>c.payload.value),[Number(first),Number(final)],'Only the latest pending target reaches native');
  await evaluate('window.b4Qa.completeSeek({position:'+chosenTime+',asOfTimeNs:"9007199254741200"})');
  await wait(timeline+'.dataset.runtimeTime==='+JSON.stringify(String(chosenTime)));
  await runtime({position:2,asOfTimeNs:'9007199254741010'});
  assert.equal(await readTime(),String(chosenTime),'Delayed pre-seek sample is discarded even after command settlement');
  assert.equal(await value(),final);
  const next=chosenTime-0.2;
  await runtime({position:next,asOfTimeNs:'9007199254741201'});
  assert.equal(await readTime(),String(next),'New native observations reconcile without a JS clock or position clamp');
  await evaluate('window.b4Qa.hold=false');
  await load();
 });
 await check('A new binding retires an old held Seek and its unsent endpoint; F5 does not replay interaction',async()=>{
  const slider=q('.timeline-control'),timeline=q('.session-timeline');
  await evaluate('window.b4Qa.hold=true');
  await pointer('mousePressed',.6,true);await pointer('mouseReleased',.6);
  await pointer('mousePressed',1,true);await pointer('mouseReleased',1);
  assert.equal((await commands()).length,1);
  const retired=await evaluate('structuredClone(window.b4Qa.sample)');
  await runtime({bindingToken:'binding-2',position:4,asOfTimeNs:'9007199254741400'});
  assert.equal(await evaluate(slider+'.getAttribute("aria-valuenow")'),'2000');
  await evaluate('window.b4Qa.completeSeek('+JSON.stringify(retired)+')');
  await rest();
  assert.equal((await commands()).length,1,'Queued endpoint belongs to the retired playback binding');
  assert.equal(await evaluate(timeline+'.dataset.bindingToken'),'binding-2');
  await pointer('mousePressed',.6,true);await pointer('mouseReleased',.6);
  assert.equal((await commands()).length,2,'Fresh binding can accept a new release');
  await send('Page.reload',{ignoreCache:true});await wait(q('.production-session')+'?.dataset.bootstrapState==="ready"');await rest();
  assert.deepEqual(await commands(),[],'No replay on a newly loaded production page');
  assert.equal(await evaluate(slider+'.getAttribute("aria-valuenow")'),'1500','Reload renders only fresh native state');
 });
 await check('100 same-binding samples update real DOM without editor snapshot polling, and do not advance without another native sample',async()=>{
  const reads=await evaluate('window.b4Qa.sent.filter(c=>c.type==="GetMainWindowSnapshot").length');for(let n=0;n<100;n++)await runtime({position:4+n/10});assert.equal(await evaluate(q('.session-timeline')+'.dataset.runtimeTime'),'13.9');assert.equal(await evaluate('window.b4Qa.sent.filter(c=>c.type==="GetMainWindowSnapshot").length'),reads);await pause(180);assert.equal(await evaluate(q('.session-timeline')+'.dataset.runtimeTime'),'13.9');
 });
 await check('Native per-operation eligibility drives transport; output mute waits for native truth and keeps Master and revisions unchanged',async()=>{
  await runtime({canPrevious:false,canNext:true,canPlayPause:false,canMute:true});assert.equal(await evaluate(q('[aria-label="Previous"]')+'.disabled'),true);assert.equal(await evaluate(q('.playback-button--primary')+'.disabled'),true);assert.equal(await evaluate(q('[aria-label="Next"]')+'.disabled'),false);
  const before=await evaluate('JSON.stringify({master:window.b4Qa.value.project.playlists.map(p=>p.masterVolume),revision:window.b4Qa.value.document.authoredRevision})');await evaluate('window.b4Qa.hold=true');await click('.rail-mute');assert.equal((await commands()).at(-1).type,'ToggleOutputMute');assert.equal(await evaluate(q('.rail-mute')+'.getAttribute("aria-pressed")'),'false');await evaluate('window.b4Qa.sample.outputMuted=true;window.b4Qa.release()');await wait(q('.rail-mute')+'.getAttribute("aria-pressed")==="true"');assert.equal(await evaluate('JSON.stringify({master:window.b4Qa.value.project.playlists.map(p=>p.masterVolume),revision:window.b4Qa.value.document.authoredRevision})'),before);await runtime({canPlayPause:true,canPrevious:true});
 });
 await check('No media or image runtime leaves selected video configured rate intact and Actual Rate unavailable; mute eligibility stays native',async()=>{
  await runtime({hasMedia:false,state:'unavailable',active:{playlistId:null,itemId:null},duration:0,position:0,canPlayPause:false,canPrevious:false,canNext:false,canSeek:false,canMute:true,sceneIndex:-1,sceneCount:0,mediaKind:null});assert.equal(await evaluate(q('.timeline-control')+'.getAttribute("aria-disabled")'),'true');assert.equal(await evaluate(q('.playback-button--primary')+'.disabled'),true);assert.equal(await evaluate(q('.rail-mute')+'.disabled'),false);assert.equal(await evaluate(q('[aria-label="Actual Rate"]')+'.textContent'),'—');assert.equal(await evaluate(q('.session-inspector')+'.dataset.authoredRate'),'1.25');assert.equal(await evaluate(q('.production-session')+'.dataset.selectedItemId'),ids.video);
  await runtime(sample({mediaKind:'image',active:{playlistId:ids.a,itemId:ids.image}}));assert.equal(await evaluate(q('[aria-label="Actual Rate"]')+'.textContent'),'—');assert.equal(await evaluate(q('.production-session')+'.dataset.selectedItemId'),ids.video);assert.equal(await evaluate(q('.playback-rail')+'.hasAttribute("data-embedded-gain")'),false);assert.equal(await evaluate(q('.playback-rail')+'.hasAttribute("data-effective-output")'),false);await runtime(sample());
 });
 await check('Pointer movement is local, incoming samples preserve thumb, and Release sends exactly one captured integer seek',async()=>{
  const before=(await commands()).length;await pointer('mousePressed',.3,true);await pointer('mouseMoved',.6,true);const value=await evaluate(q('[role="slider"][aria-label="Playback position"]')+'.getAttribute("aria-valuenow")');assert.equal((await commands()).length,before);await runtime({position:2});assert.equal(await evaluate(q('[role="slider"][aria-label="Playback position"]')+'.getAttribute("aria-valuenow")'),value);await pointer('mouseReleased',.6);const sent=await commands();assert.equal(sent.length,before+1);assert.equal(sent.at(-1).type,'Seek');assert.deepEqual(sent.at(-1).payload,{bindingToken:'binding-1',itemId:ids.video,duration:20,value:Number(value)});assert.equal(await evaluate(q('.session-timeline')+'.dataset.runtimeTime'),'2');
 });
 await check('Exact maximum and near maximum remain distinct integer commits',async()=>{for(const [x,exact] of [[.999,false],[1,true]]){await pointer('mousePressed',x,true);await pointer('mouseReleased',x);const value=(await commands()).at(-1).payload.value;assert.equal(value===10000,exact);assert.equal((await commands()).at(-1).type,'Seek');}});
 await check('Progress control, groove, fill, thumb, and their hit targets use the normal arrow',async()=>{
  const roles=await evaluate(`['.timeline-control','.timeline-groove','.timeline-progress','.timeline-thumb'].map(selector=>{
    const node=document.querySelector(selector),rect=node.getBoundingClientRect();
    const x=selector==='.timeline-control'?rect.right-3:rect.x+rect.width/2,y=selector==='.timeline-control'?rect.top+3:rect.top+rect.height/2;
    const hit=document.elementFromPoint(x,y);
    return {cursor:getComputedStyle(node).cursor,hitCursor:getComputedStyle(hit).cursor,inside:!!hit.closest('.timeline-control')};
  })`);
  assert.equal(roles.length,4);
  for(const role of roles)assert.deepEqual(role,{cursor:'default',hitCursor:'default',inside:true});
 });
 await check('Outside-track overdrag, re-entry, and release clamp the preview and admit the next drag',async()=>{
  const before=(await commands()).length,control=q('.timeline-control'),timeline=q('.session-timeline');
  for(const [start,other,end,expected]of[[.25,-.15,1.15,'10000'],[.75,1.15,-.15,'0']]){
    await pointer('mousePressed',start,true);await pointer('mouseMoved',other,true);await pointer('mouseMoved',.5,true);await pointer('mouseMoved',end,true);
    assert.equal(await evaluate(control+'.getAttribute("aria-valuenow")'),expected);
    assert.equal(await evaluate(timeline+'.dataset.dragging'),'true');
    await pointer('mouseReleased',end);
    assert.equal(await evaluate(timeline+'.dataset.dragging'),'false');
    assert.equal(await evaluate(control+'.hasPointerCapture(window.b4Qa.pointerId)'),false);
  }
  const sent=(await commands()).slice(before);
  assert.deepEqual(sent.map(command=>[command.type,command.payload.value]),[['Seek',10000],['Seek',0]]);
 });
 await check('Another pointer cannot move, cancel, or release the live Timeline pointer',async()=>{
  const before=(await commands()).length,control=q('.timeline-control');
  await pointer('mousePressed',.35,true);
  const held=await evaluate(control+'.getAttribute("aria-valuenow")');
  for(const type of ['pointerdown','pointermove','pointercancel','pointerup']){
    await evaluate(control+'.dispatchEvent(new PointerEvent('+JSON.stringify(type)+',{bubbles:true,pointerId:window.b4Qa.pointerId+9,pointerType:"touch",button:0,buttons:1,clientX:0}))');
  }
  await rest();
  assert.equal(await evaluate(control+'.getAttribute("aria-valuenow")'),held);
  assert.equal((await commands()).length,before);
  await pointer('mouseReleased',.35);
  assert.deepEqual((await commands()).slice(before).map(command=>[command.type,command.payload.value]),[['Seek',Number(held)]]);
 });
 await check('Same Item restart, Next binding change, Escape, and pointer cancellation cannot release an old drag',async()=>{
  for(const mode of ['restart','next','escape','cancel','blur','element-blur','capture-loss','released-move']){
    const before=(await commands()).length,control=q('.timeline-control');
    await pointer('mousePressed',.4,true);
    assert.equal(await evaluate(control+'.hasPointerCapture(window.b4Qa.pointerId)'),true,mode+' acquired capture');
    if(mode==='restart'||mode==='next')await runtime({bindingToken:'binding-'+crypto.randomUUID(),active:{playlistId:mode==='next'?ids.a:ids.b,itemId:mode==='next'?ids.image:ids.video}});
    else if(mode==='escape')await evaluate(control+'.dispatchEvent(new KeyboardEvent("keydown",{key:"Escape",bubbles:true,cancelable:true}))');
    else if(mode==='cancel')await evaluate(control+'.dispatchEvent(new PointerEvent("pointercancel",{bubbles:true,pointerId:window.b4Qa.pointerId}))');
    else if(mode==='blur')await evaluate('window.dispatchEvent(new Event("blur"))');
    else if(mode==='element-blur')await evaluate(q('.rail-mute')+'.focus()');
    else if(mode==='released-move')await evaluate(control+'.dispatchEvent(new PointerEvent("pointermove",{bubbles:true,pointerId:window.b4Qa.pointerId,pointerType:"mouse",buttons:0}))');
    else {
      await evaluate(control+'.releasePointerCapture(window.b4Qa.pointerId)');
      await pointer('mouseMoved',.55,true);
    }
    await rest();
    assert.equal(await evaluate(control+'.hasPointerCapture(window.b4Qa.pointerId)'),false,mode+' releases capture');
    assert.equal(await evaluate(q('.session-timeline')+'.dataset.dragging'),'false',mode+' retires local draft');
    await pointer('mouseReleased',.6);
    assert.equal((await commands()).length,before,mode);
  }
  await runtime(sample());
 });
 await check('Remount while held retires that Timeline and the next real drag settles independently',async()=>{
  const before=(await commands()).length;
  await pointer('mousePressed',.4,true);
  await evaluate('window.b4Qa.oldControl='+q('.timeline-control')+';window.b4Qa.failRead=true;window.b4Qa.emit("MainWindowSnapshotInvalidated",{})');
  await wait('document.querySelector(".production-session")?.dataset.bootstrapState==="failure"');
  await evaluate('window.b4Qa.failRead=false;window.b4Qa.emit("MainWindowSnapshotInvalidated",{})');
  await wait('document.querySelector(".production-session")?.dataset.bootstrapState==="ready"');
  assert.equal(await evaluate('window.b4Qa.oldControl.isConnected'),false);
  await pointer('mouseReleased',.6);
  assert.equal((await commands()).length,before);
  await pointer('mousePressed',.45,true);await pointer('mouseMoved',.58,true);await pointer('mouseReleased',.58);
  assert.equal((await commands()).length,before+1);
  assert.equal((await commands()).at(-1).type,'Seek');
 });
 await check('Keyboard thumb movement never commits native seek',async()=>{const before=(await commands()).length;await evaluate(q('.timeline-control')+'.dispatchEvent(new KeyboardEvent("keydown",{key:"End",bubbles:true,cancelable:true}))');await rest();assert.equal(await evaluate(q('.timeline-control')+'.getAttribute("aria-valuenow")'),'10000');assert.equal((await commands()).length,before);});
 await check('Row double-click and menu Play send stable explicit IDs, separate from selection',async()=>{
  await evaluate(q('[data-playlist-id="'+ids.a+'"] .entity-select')+'.dispatchEvent(new MouseEvent("dblclick",{bubbles:true}))');await rest();assert.deepEqual((await commands()).at(-1).payload,{playlistId:ids.a});assert.equal((await commands()).at(-1).type,'PlayPlaylist');await click('[data-item-id="'+ids.video+'"] .entity-more');await wait('document.querySelector("[data-command=play]")');await click('[data-command=play]');assert.equal((await commands()).at(-1).type,'PlayItem');assert.deepEqual((await commands()).at(-1).payload,{playlistId:ids.b,itemId:ids.video});
 });
 await check('Native hover is metadata-only, latest/page/binding fenced, and leave cancels the sole native view',async()=>{
  await pointer('mouseMoved',.35);await wait('window.b4Qa.sent.some(c=>c.type==="RequestTimelineHover")');const request=await evaluate('window.b4Qa.sent.filter(c=>c.type==="RequestTimelineHover").at(-1)');assert.deepEqual(Object.keys(request.payload).sort(),['bindingToken','itemId','serial','time']);
  await evaluate('window.b4Qa.emit("TimelineHoverResult",{status:"ready",...'+JSON.stringify(request.payload)+'},{generation:crypto.randomUUID()})');await rest();assert.equal(await evaluate(q('.session-timeline')+'.dataset.hoverStatus'),'loading');
  await pointer('mouseMoved',.65);await pause(110);const latest=await evaluate('window.b4Qa.sent.filter(c=>c.type==="RequestTimelineHover").at(-1)');assert(latest.payload.serial>request.payload.serial);await evaluate('window.b4Qa.emit("TimelineHoverResult",{status:"ready",...'+JSON.stringify(request.payload)+'})');await rest();assert.equal(await evaluate(q('.session-timeline')+'.dataset.hoverStatus'),'loading');assert.equal(await evaluate(q('.playback-button--primary')+'.disabled'),false);
  await evaluate('window.b4Qa.emit("TimelineHoverResult",{status:"ready",...'+JSON.stringify(latest.payload)+'})');await wait(q('.session-timeline')+'.dataset.hoverStatus==="ready"');assert.equal(await evaluate('document.querySelectorAll(".timeline-hover-popup,.timeline-thumbnail,.timeline-hover-overlay").length'),0,'No obsolete production DOM presentation path');assert.equal(await evaluate('window.b4Qa.sent.filter(c=>c.type==="SetPreviewRect").at(-1).payload.occlusion.rects.length'),0,'Native hover does not enter B3 DOM occlusion');
  const before=(await commands()).length;await runtime({bindingToken:'hover-new'});await wait(q('.session-timeline')+'.dataset.hoverActive==="false"');await evaluate('window.b4Qa.emit("TimelineHoverResult",{status:"ready",...'+JSON.stringify(request.payload)+'})');await rest();assert.equal(await evaluate(q('.session-timeline')+'.dataset.hoverActive'),'false');assert.equal((await commands()).length,before);
  await pointer('mouseMoved',.6);await pause(100);await send('Input.dispatchMouseEvent',{type:'mouseMoved',x:10,y:10,button:'none',buttons:0});await wait(q('.session-timeline')+'.dataset.hoverActive==="false"');assert(await evaluate('window.b4Qa.sent.some(c=>c.type==="CancelTimelineHover")'));
 });
 await check('Canonical native presentation follows20–80 percent in current central lane at every tested layout/DPI without layout mutation',async()=>{
  const observations=[];
  const read=()=>evaluate('(()=>{const rect=s=>{const r=document.querySelector(s).getBoundingClientRect();return{x:r.x,y:r.y,width:r.width,height:r.height}};return{intent:window.b4Qa.sent.filter(c=>c.type==="PresentTimelineHover").at(-1)?.payload,lane:rect(".session-timeline"),groove:rect(".timeline-groove"),nativeDomCount:document.querySelectorAll(".timeline-hover-overlay,.timeline-hover-popup,.timeline-thumbnail").length,layout:[...document.querySelectorAll(".preview-workspace,.session-library,.session-inspector,.session-timeline,.playback-rail,.preview-native-surface")].map(e=>{const r=e.getBoundingClientRect();return{x:r.x,y:r.y,width:r.width,height:r.height}})}})()');
  for(const[width,height,dpr]of[[1183,711,1],[1000,650,1.5],[1440,900,2],[1920,600,1]]){
   await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:dpr,mobile:false});await load();const before=(await read()).layout;let previous=null;
   for(const fraction of [.2,.3,.4,.5,.6,.7,.8]){
    const pointerPoint=await point(fraction);await pointer('mouseMoved',fraction);await wait('window.b4Qa.sent.filter(c=>c.type==="PresentTimelineHover").at(-1)?.payload.serial===Number(document.querySelector(".session-timeline").dataset.serial)&&document.querySelector(".session-timeline").dataset.hoverPresentation==="shown"');
    const value=await read(),intent=value.intent;assert.deepEqual(value.layout,before,'Hover must not reflow accepted workspace/Preview/rail');assert.equal(value.nativeDomCount,0);assert.deepEqual(intent.imageSize,{width:200,height:112.5});assert.equal(intent.rect.width,216);assert.equal(intent.rect.height,152.5);assert.deepEqual(intent.lane,value.lane);assert.deepEqual(intent.viewport,{width,height});assert.equal(intent.rasterizationScale,dpr);assert.match(intent.timeLabel,/^(?:[0-9]{2,}:)?[0-5][0-9]:[0-5][0-9]$/);
    const expectedLeft=Math.max(value.lane.x+8,Math.min(pointerPoint.x-108,value.lane.x+value.lane.width-8-216));assert(Math.abs(intent.rect.x-expectedLeft)<1,'Only lane edges clamp X');assert(Math.abs(intent.rect.y+152.5-value.groove.y+8)<1);assert(intent.rect.x>=value.lane.x+8&&intent.rect.x+216<=value.lane.x+value.lane.width-8);
    const interior=expectedLeft>value.lane.x+8+.1&&expectedLeft<value.lane.x+value.lane.width-8-216-.1;if(previous&&interior&&previous.interior)assert(intent.rect.x>previous.x,'Unclamped points move continuously');previous={x:intent.rect.x,interior};observations.push({fraction,pointerPoint,...value});
   }
   for(const fraction of [0,.5,1,.5,0]){const pointerPoint=await point(fraction);await pointer('mouseMoved',fraction);const value=await read(),expectedLeft=Math.max(value.lane.x+8,Math.min(pointerPoint.x-108,value.lane.x+value.lane.width-224));assert.equal(value.intent.rect.width,216);assert.equal(value.intent.rect.height,152.5);assert(Math.abs(value.intent.rect.x-expectedLeft)<1);assert.equal(value.nativeDomCount,0);}
   assert.deepEqual(await commands(),[],'Hover geometry never emits playback/authored intents');
  }
  writeFileSync(new URL('native-hover-geometry.json',evidence),JSON.stringify({observations,proof:'CSS intent/layout only; actual native HWND visibility/paint is separate native proof'},null,2));
 });
 await check('Resize updates geometry independently of decode and fresh-page hover never replays old presentation',async()=>{
  await send('Emulation.setDeviceMetricsOverride',{width:1183,height:711,deviceScaleFactor:1,mobile:false});await load();await pointer('mouseMoved',.5);await wait('window.b4Qa.sent.some(c=>c.type==="RequestTimelineHover")');
  const before=await evaluate('({presentation:window.b4Qa.sent.filter(c=>c.type==="PresentTimelineHover").length,request:window.b4Qa.sent.filter(c=>c.type==="RequestTimelineHover").length,serial:document.querySelector(".session-timeline").dataset.serial})');
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:711,deviceScaleFactor:1,mobile:false});await wait('window.b4Qa.sent.filter(c=>c.type==="PresentTimelineHover").length>'+before.presentation);const resized=await evaluate('window.b4Qa.sent.filter(c=>c.type==="PresentTimelineHover").at(-1).payload');assert.equal(String(resized.serial),before.serial);assert.equal(resized.viewport.width,1440);assert.equal(resized.viewport.height,711);assert.equal(resized.rasterizationScale,1);assert.equal(resized.rect.width,216);assert.equal(resized.rect.height,152.5);assert.equal(await evaluate('window.b4Qa.sent.filter(c=>c.type==="RequestTimelineHover").length'),before.request,'Layout observation is not another decode request');
  await load();assert.equal(await evaluate('window.b4Qa.sent.filter(c=>c.type==="PresentTimelineHover").length'),0);assert.equal(await evaluate(q('.session-timeline')+'.dataset.hoverActive'),'false');await pointer('mouseMoved',.5);await wait('window.b4Qa.sent.some(c=>c.type==="PresentTimelineHover")');const current=await evaluate('window.b4Qa.sent.filter(c=>c.type==="PresentTimelineHover").at(-1).payload');assert.equal(current.rect.width,216);assert.equal(current.rect.height,152.5);assert.equal(await evaluate('document.querySelectorAll(".timeline-hover-popup,.timeline-thumbnail img").length'),0);assert.deepEqual(await commands(),[]);
 });
 await check('Native presentation failure is explicit and cannot silently fall back to DOM thumbnail',async()=>{
  await evaluate('window.b4Qa.presentationFailure=true');await pointer('mouseMoved',.4);await wait(q('.session-timeline')+'.dataset.hoverPresentation==="unavailable"');assert.equal(await evaluate('document.querySelectorAll(".timeline-hover-popup,.timeline-thumbnail img").length'),0);assert(await evaluate('document.querySelector(".session-timeline [role=status]").textContent.includes("Preview unavailable")'));assert.equal(await evaluate(q('.playback-button--primary')+'.disabled'),false);await evaluate('window.b4Qa.presentationFailure=false');await pointer('mouseMoved',.6);await wait(q('.session-timeline')+'.dataset.hoverPresentation==="shown"');
 });
 await check('Transient editor failure remount preserves page-scoped hover serial and uses the existing runtime subscription',async()=>{
  await pointer('mouseMoved',.2);await pause(100);const before=await evaluate('window.b4Qa.sent.filter(c=>c.type==="RequestTimelineHover").at(-1).payload.serial');await evaluate('window.b4Qa.failRead=true;window.b4Qa.emit("MainWindowSnapshotInvalidated",{})');await wait('document.querySelector(".production-session")?.dataset.bootstrapState==="failure"');await evaluate('window.b4Qa.failRead=false;window.b4Qa.emit("MainWindowSnapshotInvalidated",{})');await wait('document.querySelector(".production-session")?.dataset.bootstrapState==="ready"');await pointer('mouseMoved',.8);await pause(110);assert(await evaluate('window.b4Qa.sent.filter(c=>c.type==="RequestTimelineHover").at(-1).payload.serial')>before);assert.equal(await evaluate('window.b4Qa.sent.filter(c=>c.type==="GetRuntimePlaybackSample").length'),1);
 });
 await check('Five reloads start one subscription each and never replay runtime mutation intents',async()=>{for(let n=0;n<5;n++){await load();await wait('window.b4Qa.sent.some(c=>c.type==="GetRuntimePlaybackSample")');assert.equal(await evaluate('window.b4Qa.sent.filter(c=>c.type==="GetRuntimePlaybackSample").length'),1);assert.deepEqual(await commands(),[]);}});
 await check('No React warning or uncaught browser error',()=>assert.deepEqual(errors,[]));
 }
}catch(error){checks.push({name:'Harness startup/completion',pass:false,error:error.stack??String(error)});}
finally{
 socket?.close();browser.kill();server.kill();await Promise.all([new Promise(yes=>browser.exitCode!==null?yes():browser.once('exit',yes)),new Promise(yes=>server.exitCode!==null?yes():server.once('exit',yes))]);
 const absolute=resolve(profile);assert(absolute.startsWith(resolve(tmpdir())+sep)&&absolute.split(sep).at(-1).startsWith('dse-b4-frontend-'),'Owned profile verified before deletion');for(let n=0;n<20;n++){try{rmSync(absolute,{recursive:true,force:true});break;}catch{await pause(100);}}
 const report={pass:checks.length>0&&checks.every(c=>c.pass),checks,errors,proofScope:'Actual production React DOM against controlled native wire, not Engine/native playback proof',temporaryProfileRetained:existsSync(absolute)};writeFileSync(new URL(red?'dom-red.json':'dom-checks.json',evidence),JSON.stringify(report,null,2));for(const c of checks)console.log((c.pass?'PASS':'FAIL')+' '+c.name+(c.error?'\n'+c.error:''));if(!report.pass||existsSync(absolute))process.exitCode=1;
}
