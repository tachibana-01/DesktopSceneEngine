import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {mkdirSync,writeFileSync,mkdtempSync,rmSync,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve,sep} from 'node:path';
import {fixturePreviewReply,fixtureRuntimeSample,generation,incarnation,ids,productionSessionUrl,snapshot} from './session-readonly-fixture.mjs';

// Actual production React route against a controlled native wire peer. No
// assertion here is evidence of native persistence, pickers, Engine or domain
// workflow correctness: those have independent native/runtime coverage.
const origin='http://127.0.0.1:9433',debug='http://127.0.0.1:9398';
const humanBlockers=process.argv.includes('--human-blockers'),red=process.argv.includes('--red');
const numericFormatting=process.argv.includes('--numeric-formatting');
const inspectorBaseline=process.argv.includes('--inspector-baseline');
const evidence=new URL(process.env.DSE_AUTHORED_DOM_EVIDENCE??'../node_modules/.cache/current-contract/authored/',import.meta.url);mkdirSync(evidence,{recursive:true});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
for(const url of [origin,debug+'/json/list'])await assert.rejects(fetch(url),'Owned B3 QA port must be free: '+url);
let serverLog='';
const server=spawn(process.execPath,[fileURLToPath(new URL('../node_modules/vite/bin/vite.js',import.meta.url)),'--host','127.0.0.1','--port','9433','--strictPort'],{cwd:fileURLToPath(new URL('../',import.meta.url)),windowsHide:true,stdio:['ignore','pipe','pipe']});
server.stdout.on('data',data=>serverLog+=data);server.stderr.on('data',data=>serverLog+=data);
const profile=mkdtempSync(join(tmpdir(),'dse-b3-frontend-'));
let browserLog='';
const browser=spawn(chromeExecutable,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-address=127.0.0.1','--remote-debugging-port=9398','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:['ignore','pipe','pipe']});
browser.stderr.on('data',data=>browserLog+=data);
const checks=[],errors=[],warnings=[];let socket,send,injection;
const duplicate='20000000-0000-0000-0000-000000000003',image='20000000-0000-0000-0000-000000000004',token='707c10bd-ad78-4371-ab44-d47dd7c79cb1';
function base(){const value=snapshot();value.document.authoredRevision='11';value.document.cleanRevision='10';value.document.dirty=true;value.documentOperation={pending:null,last:null};value.mediaImport={pending:null,last:null};
 const a=value.project.playlists[0],b=value.project.playlists[1];a.alias=b.alias='Duplicate playlist';b.items[0].alias='Duplicate item';
 b.items[0].presentation.crop={x:.1,y:.15,width:.8,height:.7};b.items.push({...structuredClone(b.items[0]),id:duplicate});
 b.items.push({...structuredClone(a.items[0]),id:image,duration:12.345,alias:'Test image'});return value;
}
const result=(patch={})=>({status:'applied',playlistId:ids.b,itemId:null,token:null,authored:'committed',revision:'12',dirty:true,runtime:'not-required',positionRestore:'not-required',warnings:[],sources:[],...patch});
try{
 let pages;for(let n=0;n<100;n++){try{await fetch(origin);pages=await(await fetch(debug+'/json/list')).json();if(pages.some(page=>page.type==='page'))break;}catch{}await pause(100);}
 const page=pages?.find(page=>page.type==='page');assert(page,'Owned B3 browser/server started: '+serverLog+'; browser exit='+browser.exitCode+' '+browserLog);
 socket=new WebSocket(page.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
 let serial=0;const pending=new Map();socket.addEventListener('message',async event=>{const value=JSON.parse(typeof event.data==='string'?event.data:await event.data.text());
  if(value.method==='Runtime.exceptionThrown')errors.push(value.params.exceptionDetails);
  if(value.method==='Runtime.consoleAPICalled'&&['error','warning'].includes(value.params.type))(value.params.type==='error'?errors:warnings).push(value.params);
  if(value.method==='Log.entryAdded'&&['error','warning'].includes(value.params.entry.level))(value.params.entry.level==='error'?errors:warnings).push(value.params.entry);
  const request=pending.get(value.id);if(request){pending.delete(value.id);value.error?request.reject(new Error(JSON.stringify(value.error))):request.resolve(value.result);}
 });
 send=(method,params={})=>new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});
 async function evaluate(expression){const value=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});assert(!value.exceptionDetails,JSON.stringify(value.exceptionDetails));return value.result.value;}
 async function wait(expression){for(let n=0;n<100;n++){if(await evaluate('Boolean('+expression+')'))return;await pause(40);}assert.fail('DOM wait: '+expression+' BODY='+await evaluate('document.body.innerText'));}
 async function check(name,fn){if(process.env.DSE_AUTHORED_DOM_FILTER&&!new RegExp(process.env.DSE_AUTHORED_DOM_FILTER).test(name))return;try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??JSON.stringify(error)});}}
 await Promise.all([send('Page.enable'),send('Runtime.enable'),send('Log.enable'),send('Network.enable')]);await send('Network.setCacheDisabled',{cacheDisabled:true});await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
 await send('Page.bringToFront');await send('Emulation.setFocusEmulationEnabled',{enabled:true});
 async function install(value){
  if(injection)await send('Page.removeScriptToEvaluateOnNewDocument',{identifier:injection.identifier});
  injection=await send('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{
   const listeners=new Set();let audioNegotiated=false;const q=window.b3Qa={value:${JSON.stringify(value)},sent:[],hold:false,held:[],liveGain:false,blockGain:false,trackSave:false,saves:[],holdFailedSeek:false,heldSeek:null,nextValue:null,nextResult:null,
    emit(patch={}){for(const fn of listeners)fn({data:{kind:'event',version:1,type:'MainWindowSnapshotInvalidated',generation:${JSON.stringify(generation)},sessionIncarnation:${JSON.stringify(incarnation)},payload:{},...patch}});},
    deliver(command,payload,patch={}){for(const fn of listeners)fn({data:{kind:'result',version:1,requestId:command.requestId,type:command.type,generation:command.generation,sessionIncarnation:command.sessionIncarnation,ok:true,payload,...patch}});},
    release(){q.hold=false;for(const fn of q.held.splice(0))fn();}};
   const replyPreview=(${fixturePreviewReply.toString()})();const runtimeSample=${fixtureRuntimeSample.toString()};
   window.chrome??={};window.chrome.webview={postMessage(command){q.sent.push(command);
    if(command.type==='GetMainWindowSnapshot'){queueMicrotask(()=>q.deliver(command,structuredClone(q.value)));return;}
    if(command.type==='SetPreviewRect'){queueMicrotask(()=>q.deliver(command,replyPreview(command)));return;}
    if(command.type==='GetRuntimePlaybackSample'){queueMicrotask(()=>q.deliver(command,runtimeSample(q.value)));return;}
    if(command.type==='NegotiateAudioEffects'){
     if(command.payload.version!==1)throw new Error('Invalid audio-effects negotiation');
     audioNegotiated=true;queueMicrotask(()=>q.deliver(command,structuredClone(q.value.audioEffects)));return;
    }
    if(!audioNegotiated||command.audioEffects?.version!==1||command.audioEffects.documentIncarnation!==q.value.audioEffects.documentIncarnation)throw new Error('Authored command lacks the native document stamp');
    if(command.type==='PlayItem'){queueMicrotask(()=>q.deliver(command,{status:'applied',message:'',sample:runtimeSample(q.value)}));return;}
    if(command.type==='SaveDocument'&&q.trackSave){
     q.saves.push(structuredClone(q.value));
     q.value.document.cleanRevision=q.value.document.authoredRevision;q.value.document.dirty=false;
     queueMicrotask(()=>{q.deliver(command,{status:'accepted',token:${JSON.stringify(token)}});q.emit();});
     return;
    }
    if(command.type==='Seek'&&q.holdFailedSeek){
     q.heldSeek=()=>{q.heldSeek=null;q.deliver(command,{status:'bind-failed',message:'Seek bind failed.',sample:runtimeSample(q.value)});};
     return;
    }
    if(command.type==='SetItemGain'&&q.liveGain){
     const deliverGain=()=>{
      const {playlistId,itemId,value}=command.payload;
      if(q.heldSeek||q.blockGain){
       q.deliver(command,{...${JSON.stringify(result())},playlistId,itemId,revision:q.value.document.authoredRevision,dirty:q.value.document.dirty,authored:'rejected',status:'busy'});
       return;
      }
      const item=q.value.project.playlists.find(playlist=>playlist.id===playlistId)?.items.find(item=>item.id===itemId);
      if(!item||command.audioEffects.documentIncarnation!==q.value.audioEffects.documentIncarnation)throw new Error('Stale live gain target in test fixture');
      const changed=item.mediaGain!==value;
      if(changed){
       item.mediaGain=value;
       q.value.document.authoredRevision=String(BigInt(q.value.document.authoredRevision)+1n);
       q.value.document.dirty=q.value.document.cleanRevision!==q.value.document.authoredRevision;
      }
      q.deliver(command,{...${JSON.stringify(result())},playlistId,itemId,revision:q.value.document.authoredRevision,dirty:q.value.document.dirty,authored:changed?'committed':'unchanged',status:changed?'applied':'no-change'});
      q.emit();
     };
     if(q.hold)q.held.push(deliverGain);else queueMicrotask(deliverGain);
     return;
    }
    const value=q.nextValue,response=q.nextResult;q.nextValue=q.nextResult=null;
    const deliver=()=>{if(value)q.value=value;q.deliver(command,response??${JSON.stringify(result({authored:'unchanged',status:'no-change'}))});q.emit();};
    if(q.hold)q.held.push(deliver);else queueMicrotask(deliver);
   },addEventListener(type,fn){if(type!=='message'||listeners.has(fn))throw new Error('Duplicate handler');listeners.add(fn);},removeEventListener(type,fn){if(type!=='message'||!listeners.delete(fn))throw new Error('Unbalanced handler');}};
  })();`});
 }
 const url=productionSessionUrl(origin);
 const ready='document.querySelector(".production-session")?.dataset.bootstrapState==="ready"';
 async function load(value=base()){await install(value);const marker=crypto.randomUUID();await evaluate('window.b3Marker='+JSON.stringify(marker));await send('Page.navigate',{url});await wait('window.b3Marker!=='+JSON.stringify(marker)+'&&document.querySelector(".production-session")?.dataset.bootstrapState!=="loading"&&document.querySelector(".production-session")');}
 const q=selector=>'document.querySelector('+JSON.stringify(selector)+')';
 const field=label=>'input[aria-label='+JSON.stringify(label)+']';
 const row=(entity,id)=>'[data-'+entity+'-id='+JSON.stringify(id)+']';
 async function rest(){await evaluate('new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))');}
 async function pointer(type,point,down=false){await send('Input.dispatchMouseEvent',{type,...point,button:type==='mouseMoved'?down?'left':'none':'left',buttons:down?1:0,clickCount:type==='mouseMoved'?0:1});await rest();}
 async function point(selector,x=.5,y=.5){return evaluate('(()=>{const r='+q(selector)+'.getBoundingClientRect();return {x:r.x+r.width*'+x+',y:r.y+r.height*'+y+'}})()');}
 async function key(name){const virtual=name==='ArrowUp'?38:name==='Escape'?27:40;await send('Input.dispatchKeyEvent',{type:'keyDown',key:name,code:name,windowsVirtualKeyCode:virtual});await send('Input.dispatchKeyEvent',{type:'keyUp',key:name,code:name,windowsVirtualKeyCode:virtual});await rest();}
 async function input(selector,value){await evaluate('(()=>{const e='+q(selector)+';if(!e)throw new Error("Missing input");e.focus();Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,"value").set.call(e,'+JSON.stringify(String(value))+');e.dispatchEvent(new Event("input",{bubbles:true}));})()');await rest();}
 async function click(selector){assert(await evaluate('!!'+q(selector)),selector);await evaluate(q(selector)+'.click()');await rest();}
 async function blur(selector){await evaluate(q(selector)+'.blur()');await rest();}
 async function idle(){await wait('document.querySelector(".production-session").dataset.authoredBusy==="false"&&!document.querySelector(".session-project-trigger").disabled');await rest();}
 async function reload(value){await install(value);const marker=crypto.randomUUID();await evaluate('window.b3Marker='+JSON.stringify(marker));await send('Page.reload',{ignoreCache:true});await wait('window.b3Marker!=='+JSON.stringify(marker)+'&&'+ready);await rest();}
 async function prepare(value,ack=result(),hold=false){await evaluate('Object.assign(window.b3Qa,{nextValue:'+JSON.stringify(value)+',nextResult:'+JSON.stringify(ack)+',hold:'+hold+'})');}
 async function commands(){return evaluate('window.b3Qa.sent.filter(c=>!["GetMainWindowSnapshot","GetRuntimePlaybackSample","SetPreviewRect","NegotiateAudioEffects"].includes(c.type))');}
 async function last(){return (await commands()).at(-1);}
 async function selectPlaylist(id){const value=await evaluate('structuredClone(window.b3Qa.value)');value.selection={playlistId:id,itemId:null};await prepare(value,result({playlistId:id,authored:'not-applicable'}));await click(row('playlist',id)+' .entity-select');await wait('document.querySelector(".production-session").dataset.selectedPlaylistId==='+JSON.stringify(id)+'&&document.querySelector(".production-session").dataset.selectedItemId===""');await idle();}
 async function selectItem(id){const value=await evaluate('structuredClone(window.b3Qa.value)');value.selection={playlistId:ids.b,itemId:id};await prepare(value,result({playlistId:ids.b,itemId:id,authored:'not-applicable'}));await click(row('item',id)+' .entity-select');await wait('document.querySelector(".production-session").dataset.selectedItemId==='+JSON.stringify(id));await idle();}
 async function entityMenu(entity,id){await click(row(entity,id)+' .entity-more');await wait('document.querySelector("[data-command=rename]")');await idle();}
 async function dismiss(){if(await evaluate('!!document.querySelector("[data-authored-feedback]")'))await click('[data-message-ok]');}

 const projectionSelectors=[
  '.session-project-trigger','.session-library .panel-icon-button',
  '[data-playback-mode-trigger]','.entity-select','.entity-more','.playlist-volume-trigger',
  '.entity-row','.entity-active','.playback-rail','.playback-button','.playback-state','.rail-mute','.timeline-control',
 ];
 function projectionFacts(selectors,remember=false,localControl=null){
  const root=document.querySelector('.production-session');
  const nodes=[...document.querySelectorAll(selectors.join(','))].filter(node=>!localControl||!node.matches(localControl));
  if(remember)window.b3ProjectionNodes=nodes;
  return {
   identity: nodes.length===window.b3ProjectionNodes.length&&nodes.every((node,index)=>node===window.b3ProjectionNodes[index]&&node.isConnected),
   selectedPlaylist:root.dataset.selectedPlaylistId, selectedItem:root.dataset.selectedItemId,
   activePlaylist:root.dataset.activePlaylistId, activeItem:root.dataset.activeItemId,
   binding:root.dataset.runtimeBindingToken, muted:root.dataset.outputMuted,
   controls:nodes.map(node=>{
    const style=getComputedStyle(node);
    return {control:node.className,disabled:node.matches(':disabled'),available:node.getAttribute('aria-disabled'),
     color:style.color,opacity:style.opacity,visibility:style.visibility,display:style.display,
     policyScope:node.dataset.playbackModeTrigger??null,policy:node.dataset.playbackModeTrigger?node.value:null,
     label:node.matches('.entity-active,.playback-state')?node.textContent:null};
   }),
  };
 }
 function watchUnchangedProjection(){
  const nodes=new Set(window.b3ProjectionNodes),changes=[];
  const observer=new MutationObserver(records=>{
   for(const record of records){
    if(record.type==='attributes'&&nodes.has(record.target))changes.push([record.target.className,record.attributeName,record.oldValue]);
    if(record.type==='childList')for(const removed of record.removedNodes)
     for(const node of nodes)if(removed===node||removed.contains(node))changes.push([node.className,'removed']);
   }
  });
  observer.observe(document.body,{subtree:true,childList:true,attributes:true,attributeOldValue:true,
   attributeFilter:['disabled','aria-disabled','data-active','data-selected','data-runtime-configured','value']});
  window.b3ProjectionChanges=changes;
 }
 async function readProjection(remember=false,localControl=null){
  return evaluate('('+String(projectionFacts)+')('+JSON.stringify(projectionSelectors)+','+remember+','+JSON.stringify(localControl)+')');
 }
 async function startProjectionProbe(localControl=null){
  await wait('document.querySelector(".session-timeline")?.dataset.enabled==="true"');
  await rest();
  return readProjection(true,localControl);
 }
 async function assertProjectionStable(before,message,localControl=null){
  const after=await readProjection(false,localControl);
  const differences=after.controls.flatMap((value,index)=>JSON.stringify(value)===JSON.stringify(before.controls[index])?[]:[{index,previous:before.controls[index],observed:value}]);
  assert.deepEqual(differences,[],message+' – rendered control state');
  assert.deepEqual(after,before,message+' – identity and native factual state');
 }

 await check('Inspector cleanup: Item identity, editable Name and readonly Type placement',async()=>{
  const value=base();await load(value);
  const facts=await evaluate(`(()=>{const i=document.querySelector('.session-inspector'),type=i.querySelector('[data-item-type]'),name=i.querySelector('[data-inspector-name]');return{context:i.dataset.inspectorContext,owner:i.dataset.ownerId,header:i.querySelector('.inspector-selection p')?.textContent,name:name.value,nameReadOnly:name.readOnly,nameDisabled:name.disabled,nameCount:i.querySelectorAll('[data-inspector-name]').length,type:type.textContent,typeInHeader:!!type.closest('.inspector-selection'),itemHeadings:[...i.querySelectorAll('h3')].filter(e=>e.textContent==='ITEM').length,sections:[...i.querySelectorAll('h3')].map(e=>e.textContent),text:i.innerText};})()`);
  writeFileSync(new URL('inspector-structure.json',evidence),JSON.stringify({mode:inspectorBaseline?'BASELINE':'AFTER',facts},null,2));
  assert.equal(facts.context,'item');assert.equal(facts.owner,ids.video);assert.equal(facts.header,value.project.playlists[1].items[0].alias);
  assert.equal(facts.name,facts.header);assert(!facts.nameReadOnly&&!facts.nameDisabled);assert.equal(facts.nameCount,1);assert.equal(facts.type,'Video');
  assert(!await evaluate('!!'+q(field('Playlist Master Volume'))),'Item must not expose Playlist-only Master');
  assert.equal(facts.typeInHeader,!inspectorBaseline);assert.equal(facts.itemHeadings,inspectorBaseline?1:0);assert.deepEqual(await commands(),[]);
 });
 await check('Inspector cleanup: active versus selected and A to B to Playlist to none stay correctly scoped',async()=>{
  const active=base();active.selection={playlistId:ids.a,itemId:ids.image};await load(active);
  assert.equal(await evaluate('document.querySelector(".inspector-selection span").textContent'),'ACTIVE');
  assert.equal(await evaluate('document.querySelector("[data-item-type]").textContent'),'Image');
  await selectPlaylist(ids.b);await selectItem(ids.video);
  assert.equal(await evaluate('document.querySelector(".inspector-selection span").textContent'),'SELECTED');
  assert.equal(await evaluate('document.querySelector("[data-item-type]").textContent'),'Video');
  assert(!await evaluate('!!'+q(field('Image Duration'))));
  await selectItem(image);assert.equal(await evaluate('document.querySelector("[data-item-type]").textContent'),'Image');
  assert(!await evaluate('!!'+q(field('Configured Rate'))));
  await selectPlaylist(ids.a);assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'),'playlist');
  assert.equal(await evaluate('document.querySelector("[data-item-count]").textContent'),'1');
  assert(await evaluate('!!'+q(field('Playlist Master Volume'))));
  assert(!await evaluate('!!document.querySelector("[data-item-type],.inspector-source")'));
  const none=base();none.selection={playlistId:null,itemId:null};await load(none);
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'),'none');
  assert.equal(await evaluate('document.querySelector(".inspector-empty").textContent'),'No selection');
  assert.equal(await evaluate('document.querySelector(".session-inspector").querySelectorAll("input,output,[data-item-type]").length'),0);
 });
 await check('Inspector cleanup: long alias and responsive pane remain bounded without changing track coverage',async()=>{
  const value=base(),alias='Long selected video — '+('readable full alias / '.repeat(30));value.project.playlists[1].items[0].alias=alias;await load(value);
  const layouts=[];
  for(const width of [1440,1183,1000,800,740]){
   await send('Emulation.setDeviceMetricsOverride',{width,height:900,deviceScaleFactor:1,mobile:false});await rest();
   const f=await evaluate(`(()=>{const i=document.querySelector('.session-inspector'),p=i.querySelector('.inspector-selection p'),n=i.querySelector('[data-inspector-name]'),r=i.getBoundingClientRect(),nr=n.getBoundingClientRect(),w=document.querySelector('.session-workspace');return{width:innerWidth,display:getComputedStyle(i).display,pane:{left:r.left,right:r.right,width:r.width},field:{left:nr.left,right:nr.right},alias:p.textContent,title:p.title,input:n.value,ellipsis:getComputedStyle(p).textOverflow,whiteSpace:getComputedStyle(p).whiteSpace,overflow:getComputedStyle(p).overflow,tracks:getComputedStyle(w).gridTemplateColumns};})()`);layouts.push(f);
   assert.equal(f.alias,alias);assert.equal(f.title,alias);assert.equal(f.input,alias);assert.equal(f.ellipsis,'ellipsis');assert.equal(f.whiteSpace,'nowrap');assert.equal(f.overflow,'hidden');
   if(width>760){assert.notEqual(f.display,'none');assert(f.field.left>=f.pane.left&&f.field.right<=f.pane.right+1);assert(f.pane.right<=width+1);}
   else assert.equal(f.display,'none');
  }
  writeFileSync(new URL('inspector-layout.json',evidence),JSON.stringify(layouts,null,2));
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
 });
 await check('Inspector cleanup: existing inline Item rename still drafts then commits the captured native ID',async()=>{
  await load();const before=base(),renamed='Inline renamed selected Item',value=base();value.project.playlists[1].items[0].alias=renamed;
  await prepare(value,result({itemId:ids.video}));await input(field('Name'),renamed);assert.deepEqual(await commands(),[],'Typing alone must not author');
  await send('Input.dispatchKeyEvent',{type:'keyDown',key:'Enter',code:'Enter',windowsVirtualKeyCode:13});await send('Input.dispatchKeyEvent',{type:'keyUp',key:'Enter',code:'Enter',windowsVirtualKeyCode:13});await idle();
  assert.equal((await last()).type,'RenameItem');assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,alias:renamed,expectedAlias:before.project.playlists[1].items[0].alias});
  assert.equal(await evaluate('document.querySelector(".inspector-selection p").textContent'),renamed);
  assert.equal(await evaluate('document.querySelector("[data-inspector-name]").value'),renamed);
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.activeItemId'),ids.image);
 });

 await check('The current Session refuses a partial authored snapshot without mounting editor controls',async()=>{
  const value=base();delete value.mediaImport;await load(value);
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.failureCode'),'SnapshotValidationFailure');
  assert.equal(await evaluate('document.querySelector(".entity-select")'),null);assert.deepEqual(await commands(),[]);
 });
 await check('Loaded configured 1.25 and selected versus active facts render without a write while current image rate is unavailable',async()=>{
  await load();await wait('document.querySelector(".production-session").dataset.runtimeBindingToken==="fixture-binding"');assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.25');assert.equal(await evaluate(q('[aria-label="Actual Rate"]')+'.textContent'),'—');
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.ownerId'),ids.video);assert.equal(await evaluate('document.querySelector(".production-session").dataset.activeItemId'),ids.image);assert.equal(await evaluate('document.querySelector(".production-session").dataset.outputMuted'),'true');assert.deepEqual(await commands(),[]);
  assert.equal(await evaluate('document.querySelector(".caption-controls")'),null);
  assert.equal(await evaluate('!!document.querySelector(".rail-master")'),false);
 });
 for(const[label,property,native,display]of[
  ['Uniform scale','scaleX',1.2000000476837158,'1.20'],
  ['Position X normalized','positionX',0.10000000149011612,'0.10'],
  ['Position Y normalized','positionY',0.05000000074505806,'0.05']]){
  await check(label+' displays '+display+' while preserving the raw native value through focus/Enter/blur/reload',async()=>{
   const value=base();value.document.cleanRevision='11';value.document.dirty=false;value.project.playlists[1].items[0].presentation.layout[property]=native;
   await load(value);assert.equal(await evaluate(q(field(label))+'.value'),display);
   await evaluate('(()=>{const e='+q(field(label))+';e.focus();e.dispatchEvent(new KeyboardEvent("keydown",{key:"Enter",bubbles:true,cancelable:true}));e.blur();})()');await rest();
   assert.deepEqual(await commands(),[],'Display formatting must never become an authored intent');
   assert.equal(await evaluate('window.b3Qa.value.project.playlists[1].items[0].presentation.layout.'+property),native);
   assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'false');
   await reload(value);assert.equal(await evaluate(q(field(label))+'.value'),display);assert.deepEqual(await commands(),[]);
   assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.25');
  });
 }
 await check('Visual numeric drafts stay unformatted while focused; only explicit field commit changes native truth',async()=>{
  const value=base();value.document.cleanRevision='11';value.document.dirty=false;
  value.project.playlists[1].items[0].presentation.layout={scaleX:1.2345670461654663,scaleY:1.2345670461654663,positionX:0.12345600128173828,positionY:0.05000000074505806};
  await load(value);assert.equal(await evaluate(q(field('Uniform scale'))+'.value'),'1.23');
  await evaluate('void(window.b3NumericEditor='+q(field('Position X normalized'))+')');await input(field('Position X normalized'),'-0.2');assert.deepEqual(await commands(),[]);
  const unrelated=structuredClone(value);unrelated.document.authoredRevision='12';unrelated.document.dirty=true;unrelated.project.playlists[0].masterVolume=.61;
  await evaluate('window.b3Qa.value='+JSON.stringify(unrelated)+';window.b3Qa.emit()');await wait('document.querySelector(".production-session").dataset.authoredRevision==="12"');
  assert.equal(await evaluate(q(field('Position X normalized'))+'.value'),'-0.2');assert(await evaluate('document.activeElement===window.b3NumericEditor&&window.b3NumericEditor.isConnected'));
  assert.deepEqual(await commands(),[]);assert.deepEqual(await evaluate('window.b3Qa.value.project.playlists[1].items[0].presentation.layout'),value.project.playlists[1].items[0].presentation.layout);
  const committed=structuredClone(unrelated);committed.document.authoredRevision='13';committed.project.playlists[1].items[0].presentation.layout.positionX=Math.fround(-.2);
  await prepare(committed,result({itemId:ids.video,revision:'13'}));await blur(field('Position X normalized'));await idle();
  assert.equal((await commands()).length,1);assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,patch:{positionX:-.2}});
  assert.equal(await evaluate(q(field('Position X normalized'))+'.value'),'-0.20');assert.equal(await evaluate(q(field('Uniform scale'))+'.value'),'1.23');
  assert.equal(await evaluate('window.b3Qa.value.project.playlists[1].items[0].presentation.layout.positionY'),value.project.playlists[1].items[0].presentation.layout.positionY);
  await reload(committed);assert.deepEqual(await commands(),[]);assert.equal(await evaluate(q(field('Position X normalized'))+'.value'),'-0.20');assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.25');
 });
 await check('Loaded non-step Rate keeps 1.25 until actual arrows select accepted 0.1 grid values',async()=>{
  const clean=base();clean.document.cleanRevision='11';clean.document.dirty=false;
  await load(clean);await evaluate(q(field('Configured Rate'))+'.focus()');assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.25');await key('ArrowUp');assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.3');await key('ArrowUp');assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.4');await key('ArrowDown');assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.3');assert.deepEqual(await commands(),[]);
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'false');assert.equal(await evaluate('window.b3Qa.value.project.playlists[1].items[0].rate'),1.25);
  const value=structuredClone(clean);value.document.authoredRevision='12';value.document.dirty=true;value.project.playlists[1].items[0].rate=1.3;await prepare(value,result({itemId:ids.video}));await click('[aria-label="Apply Rate"]');await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,value:1.3});assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.authoredRate'),'1.3');assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'true');
  await load(clean);await evaluate(q(field('Configured Rate'))+'.focus()');await key('ArrowDown');assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.2');assert.deepEqual(await commands(),[]);assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'false');
  const down=structuredClone(value);down.project.playlists[1].items[0].rate=1.2;await prepare(down,result({itemId:ids.video}));await click('[aria-label="Apply Rate"]');await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,value:1.2});assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.authoredRate'),'1.2');
 });
 await check('Actual captured row Master submits during drag while delayed native acknowledgement keeps authored facts native',async()=>{
  await load();await evaluate('window.b3Qa.hold=true');const trigger=row('playlist',ids.b)+' .playlist-volume-trigger';await pointer('mouseMoved',await point(trigger));await wait('document.querySelector("[data-playlist-master-slider]")');const slider='[data-playlist-master-slider]';await evaluate('void(window.b3DragSlider='+q(slider)+')');
  await pointer('mouseMoved',await point(slider,.5,.6));const before=await startProjectionProbe(trigger);
  try{
   await pointer('mousePressed',await point(slider,.5,.6),true);await pointer('mouseMoved',await point(slider,.5,.35),true);
   const intermediate=(await commands()).filter(c=>c.type==='SetPlaylistMaster').length;assert(intermediate>0,'Pointer movement sends native intent before release');assert(await evaluate('window.b3DragSlider.isConnected&&!window.b3DragSlider.disabled'));
   assert.equal(await evaluate(q(trigger)+'.disabled'),true,'Only the owning speaker keeps its mute guard while Master is pending');
   assert.equal(await evaluate(q(row('playlist',ids.a)+' .playlist-volume-trigger')+'.disabled'),false);
   await assertProjectionStable(before,'Pending Playlist Master must leave unrelated controls and facts available',trigger);
   await pointer('mouseMoved',await point(slider,.5,.16),true);const percent=Number(await evaluate(q(slider)+'.value'));assert(percent>65);assert.equal((await commands()).filter(c=>c.type==='SetPlaylistMaster').length,intermediate,'Only one scalar is admitted while native owns the previous request');assert.equal(await evaluate('window.b3Qa.value.project.playlists[1].masterVolume'),.4);assert.equal(await evaluate(q(trigger)+'.dataset.masterPercent'),'40');
   await pointer('mouseReleased',await point(slider,.5,.16));
   const value=base();value.project.playlists[1].masterVolume=percent/100;await prepare(value,result({playlistMasterVolume:percent/100}),true);await evaluate('window.b3Qa.release()');
   await wait(q(trigger)+'.dataset.masterPercent==='+JSON.stringify(String(percent)));assert.deepEqual((await last()).payload,{playlistId:ids.b,value:percent/100});
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 await check('Actual Inspector Percent drag remains enabled through release and keeps authored facts native until delayed result',async()=>{
  await load();await selectPlaylist(ids.a);await selectPlaylist(ids.b);await evaluate('window.b3Qa.hold=true');const slider=field('Playlist Master Volume');await evaluate('void(window.b3DragSlider='+q(slider)+')');
  try{
   await pointer('mouseMoved',await point(slider,.4));await pointer('mousePressed',await point(slider,.4),true);await pointer('mouseMoved',await point(slider,.65),true);assert.equal(await evaluate('window.b3DragSlider.matches(":disabled")'),false,'Inspector range must stay enabled while the pointer owns the drag');assert.equal((await commands()).length,2,'Only the two native selection commands may precede release');await pointer('mouseMoved',await point(slider,.86),true);const percent=Number(await evaluate(q(slider)+'.value'));assert(percent>70);assert.equal(await evaluate('window.b3Qa.value.project.playlists[1].masterVolume'),.4);
   const value=await evaluate('structuredClone(window.b3Qa.value)');value.project.playlists[1].masterVolume=percent/100;await prepare(value,result(),true);await pointer('mouseReleased',await point(slider,.86));await wait('window.b3Qa.held.length===1');assert.equal((await commands()).length,3);assert.deepEqual((await last()).payload,{playlistId:ids.b,value:percent/100});assert.equal(await evaluate('window.b3Qa.value.project.playlists[1].masterVolume'),.4);assert.equal(await evaluate(q(slider)+'.value'),String(percent),'Inspector Master thumb retains the released draft until native settles');await evaluate('window.b3Qa.release()');await idle();assert.equal(await evaluate(q(slider)+'.value'),String(percent));
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 await check('Video Inspector shows Volume in English and 音量 in Simplified Chinese',async()=>{
  for(const [language,label] of [['en','Volume'],['zh_CN','音量']]){
   const value=base();value.language.committed=value.language.effective=language;await load(value);
   const facts=await evaluate(`[...document.querySelectorAll('.session-inspector input[type=range]')].map(e=>({label:e.getAttribute('aria-label'),row:e.closest('.inspector-property').querySelector('.inspector-property-label').textContent}))`);
   assert.deepEqual(facts,[{label,row:label}]);
   assert.deepEqual(await commands(),[]);
  }
 });
 await check('Volume sends its first native move before release and keeps the thumb independent of the native acknowledgement',async()=>{
  const initial=base();initial.document.cleanRevision='11';initial.document.dirty=false;
  await load(initial);const slider=field('Volume');
  const intended=structuredClone(initial);intended.project.playlists[1].items[0].mediaGain=.81;
  intended.document.authoredRevision='12';intended.document.dirty=true;
  await prepare(intended,result({itemId:ids.video}),true);
  try{
   await pointer('mousePressed',await point(slider),true);
   await pointer('mouseMoved',await point(slider,.81),true);
   const first=(await commands()).find(command=>command.type==='SetItemGain');
   assert(first,'A captured pointer move must send SetItemGain before pointerup');
   assert(first.payload.value>.7);assert.equal(await evaluate('window.b3Qa.held.length'),1);
   assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'false');
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 await check('Volume full snapshot refresh and held result keep unrelated UI projection stable',async()=>{
  const initial=base();initial.active={playlistId:ids.b,itemId:duplicate};
  initial.document.cleanRevision='11';initial.document.dirty=false;
  await load(initial);
  const slider=field('Volume'),before=await startProjectionProbe();
  await evaluate('('+String(watchUnchangedProjection)+')()');
  await evaluate('Object.assign(window.b3Qa,{liveGain:true,hold:true})');
  const first=await point(slider,.69),second=await point(slider,.26);
  try{
   await pointer('mousePressed',await point(slider),true);
   await pointer('mouseMoved',first,true);await wait('window.b3Qa.held.length===1');
   await assertProjectionStable(before,'A pending gain must preserve painted sidebar, title, transport, and selection');
   await pointer('mouseMoved',second,true);
   const target=Number(await evaluate(q(slider)+'.value'))/100;
   assert.equal((await commands()).length,1,'At most one in-flight gain');
   await assertProjectionStable(before,'Reversing with a pending gain must preserve unrelated projection');
   await evaluate('window.b3Qa.release()');
   await wait('window.b3Qa.value.project.playlists[1].items[0].mediaGain==='+target);
   await wait('document.querySelector(".production-session").dataset.authoredRevision==="13"');
   await assertProjectionStable(before,'Authoritative gain readback must preserve unchanged identities and facts');
   await pointer('mouseReleased',second);
   assert.equal(await evaluate(q(slider)+'.value'),String(Math.round(target*100)));
   assert.equal((await commands()).length,2);
   assert.deepEqual(await evaluate('window.b3ProjectionChanges'),[],'Pending and full readback do not transiently change or detach unrelated controls');
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 await check('Save orders after the latest released gain already submitted before opening the document menu',async()=>{
  const initial=base();initial.document.cleanRevision='11';initial.document.dirty=false;
  await load(initial);
  await evaluate('Object.assign(window.b3Qa,{liveGain:true,hold:true,trackSave:true})');
  const slider=field('Volume');
  try{
   await pointer('mousePressed',await point(slider),true);
   await pointer('mouseMoved',await point(slider,.7),true);await wait('window.b3Qa.held.length===1');
   const finalPoint=await point(slider,.26);
   await pointer('mouseMoved',finalPoint,true);
   const target=Number(await evaluate(q(slider)+'.value'))/100;
   await pointer('mouseReleased',finalPoint);
   await click('.session-project-trigger');
   await wait('document.querySelector("[data-command=save]")');
   await click('[data-command=save]');
   assert.deepEqual((await commands()).map(command=>command.type),['SetItemGain'],'Save cannot overtake the already released latest draft');
   await evaluate('window.b3Qa.release()');await wait('window.b3Qa.saves.length===1');
   const saved=await evaluate('window.b3Qa.saves[0]');
   assert.equal(saved.project.playlists[1].items[0].mediaGain,target);
   assert.equal(saved.document.authoredRevision,'13');assert.equal(saved.document.dirty,true);
   await wait('document.querySelector(".production-session").dataset.dirty==="false"');
   assert.deepEqual((await commands()).map(command=>command.type),['SetItemGain','SetItemGain','SaveDocument']);
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 await check('Save releases its UI when pending gain returns Busy without a capacity edge, then succeeds on explicit retry',async()=>{
  const initial=base();initial.document.cleanRevision='11';initial.document.dirty=false;
  await load(initial);
  await evaluate('Object.assign(window.b3Qa,{liveGain:true,blockGain:true,trackSave:true})');
  const slider=field('Volume'),finalPoint=await point(slider,.72);
  const capacity=await evaluate('window.b3Qa.value.audioEffects.capacityRevision');
  const openSave=async()=>{
   await click('.session-project-trigger');
   await wait('document.querySelector("[data-command=save]")');await click('[data-command=save]');
  };
  try{
   await pointer('mousePressed',await point(slider),true);
   await pointer('mouseMoved',finalPoint,true);
   const target=Number(await evaluate(q(slider)+'.value'))/100;
   await pointer('mouseReleased',finalPoint);
   const initialAttempts=(await commands()).length;
   assert(initialAttempts>=1&&initialAttempts<=2,'Unchanged snapshot cannot keep retrying a blocked gain');
   await openSave();
   await wait('document.querySelector("[data-document-feedback]")&&!document.querySelector(".session-project-trigger").disabled');
   assert.equal(await evaluate('window.b3Qa.saves.length'),0,'No successful or provisional Save before confirming gain');
   assert.equal(await evaluate('window.b3Qa.value.document.cleanRevision'),'11');
   assert.match(await evaluate('document.querySelector("[data-document-feedback]").textContent'),/volume change could not be confirmed/i);
   assert.deepEqual((await commands()).slice(initialAttempts).map(command=>command.type),['SetItemGain'],'One blocked draft retry per explicit action');
   await click('[data-message-ok]');
   await evaluate('window.b3Qa.blockGain=false'); // Decode failure released admission without an audio capacity publication.
   await openSave();
   await wait('window.b3Qa.saves.length===1');
   const saved=await evaluate('window.b3Qa.saves[0]');
   assert.equal(saved.project.playlists[1].items[0].mediaGain,target);
   assert.equal(saved.document.authoredRevision,'12');assert.equal(saved.document.dirty,true);
   assert.equal(saved.audioEffects.capacityRevision,capacity);
   assert.deepEqual((await commands()).slice(initialAttempts).map(command=>command.type),['SetItemGain','SetItemGain','SaveDocument']);
   await wait('document.querySelector(".production-session").dataset.dirty==="false"');
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 await check('Delayed mode, alias, and selection edits preserve unrelated projection while native results own each changed fact',async()=>{
  for(const edit of ['mode','alias','selection']){
   const initial=base();initial.active={playlistId:ids.b,itemId:ids.video};
   await load(initial);
   const next=structuredClone(initial),expected=await startProjectionProbe();
   if(edit==='mode'){
    next.project.playlistMode='random';next.document.authoredRevision='12';
    await prepare(next,result({playlistId:null}),true);
    await click('[data-playback-mode-trigger=project]');await wait('document.querySelector("[data-playback-mode-popup]")');
    await click('[data-choice-value=random]');
    assert.equal((await last()).type,'SetProjectPolicy');
   }else if(edit==='alias'){
    next.project.playlists[1].items[0].alias='Updated from native';next.document.authoredRevision='12';
    await prepare(next,result({itemId:ids.video}),true);
    await input(field('Name'),'Updated from native');await blur(field('Name'));
    assert.equal((await last()).type,'RenameItem');
   }else{
    next.selection.itemId=duplicate;
    await prepare(next,result({itemId:duplicate,authored:'not-applicable',revision:'11'}),true);
    await click(row('item',duplicate)+' .entity-select');
    assert.equal((await last()).type,'SelectItem');
   }
   await wait('window.b3Qa.held.length===1');
   await assertProjectionStable(expected,edit+' pending native observation');
   await evaluate('window.b3Qa.release()');
   if(edit==='selection'){
    expected.selectedItem=duplicate;
    await wait('document.querySelector(".production-session").dataset.selectedItemId==='+JSON.stringify(duplicate));
   }else{
    await wait('document.querySelector(".production-session").dataset.authoredRevision==="12"');
    if(edit==='mode')expected.controls.find(control=>control.policyScope==='project').policy='random';
    else assert.equal(await evaluate(q(row('item',ids.video)+' .entity-alias')+'.textContent'),'Updated from native');
   }
   await assertProjectionStable(expected,edit+' authoritative readback');
  }
 });
 await check('Rapid selection A to B to A sends the final native selection while the first reply is held',async()=>{
  const first=base(),middle=structuredClone(first);middle.selection.itemId=duplicate;
  await load(first);const before=await startProjectionProbe();
  await prepare(middle,result({itemId:duplicate,authored:'not-applicable',revision:'11'}),true);
  await click(row('item',duplicate)+' .entity-select');
  await wait('window.b3Qa.held.length===1');
  await prepare(first,result({itemId:ids.video,authored:'not-applicable',revision:'11'}),true);
  await click(row('item',ids.video)+' .entity-select');
  await wait('window.b3Qa.held.length===2');
  assert.deepEqual((await commands()).map(command=>[command.type,command.payload.itemId]),[['SelectItem',duplicate],['SelectItem',ids.video]]);
  await assertProjectionStable(before,'Unconfirmed B and A must preserve authoritative A with no remount');
  await evaluate('window.b3Qa.held.shift()()');
  await wait('document.querySelector(".production-session").dataset.selectedItemId==='+JSON.stringify(duplicate));
  await assertProjectionStable({...before,selectedItem:duplicate},'Native B may project while A is held');
  await evaluate('window.b3Qa.release()');
  await wait('document.querySelector(".production-session").dataset.selectedItemId==='+JSON.stringify(ids.video));
  await assertProjectionStable(before,'Final native A restores selection without perturbing active/transport');
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.authoredRevision'),'11');
 });
 await check('Rapid Project and Playlist mode reversals send only the user choice for each owning policy before readback',async()=>{
  for(const scope of ['project','playlist']){
   const first=base(),middle=structuredClone(first),lastValue=structuredClone(first);
   const mode=scope==='project'?(value=>value.project.playlistMode):(value=>value.project.playlists[1].itemMode);
   const command=scope==='project'?'SetProjectPolicy':'SetPlaylistPolicy';
   const choice=scope==='project'?'list-loop':'repeat-current';
   if(scope==='project')middle.project.playlistMode='random';else middle.project.playlists[1].itemMode='random';
   middle.document.authoredRevision='12';lastValue.document.authoredRevision='13';
   await load(first);
   const before=await startProjectionProbe(),trigger='[data-playback-mode-trigger='+scope+']';
   async function choose(value){
    await click(trigger);await wait('document.querySelector("[data-playback-mode-popup]")');
    await click('[data-choice-value='+value+']');
   }
   await choose(choice);
   assert.equal((await commands()).length,0,'Reselecting current native mode remains a no-op when no edit is pending');
   await prepare(middle,result({playlistId:scope==='project'?null:ids.b,revision:'12'}),true);
   await choose('random');await wait('window.b3Qa.held.length===1');
   await prepare(lastValue,result({playlistId:scope==='project'?null:ids.b,revision:'13'}),true);
   await choose(choice);await wait('window.b3Qa.held.length===2');
   assert.deepEqual((await commands()).map(edit=>[edit.type,edit.payload]),['random',choice].map(mode=>[command,scope==='project'?{mode}:{playlistId:ids.b,mode}]));
   await assertProjectionStable(before,scope+' policy before native readback');
   await evaluate('window.b3Qa.held.shift()()');
   await wait(q(trigger)+'.value==='+JSON.stringify(mode(middle)));
   const accepted=structuredClone(before);accepted.controls.find(control=>control.policyScope===scope).policy=mode(middle);
   await assertProjectionStable(accepted,scope+' policy intermediate native value');
   await evaluate('window.b3Qa.release()');
   await wait('document.querySelector(".production-session").dataset.authoredRevision==="13"');
   await assertProjectionStable(before,scope+' policy final native readback');
  }
 });
 await check('Volume native Busy coalesces the latest move without interrupting the held pointer',async()=>{
  const initial=base();initial.document.cleanRevision='11';initial.document.dirty=false;
  initial.active={...initial.selection};
  initial.audioEffects.effects=[{
   operation:token,documentIncarnation:initial.audioEffects.documentIncarnation,bindingToken:fixtureRuntimeSample(initial).bindingToken,
   execution:'1',sequence:'1',scope:'embedded-gain',phase:'pending',error:'',effectTimeNs:'0',playlistId:ids.b,itemId:ids.video,
  }];
  await load(initial);const slider=field('Volume'),range=q(slider);
  await prepare(initial,result({status:'busy',itemId:ids.video,revision:'11',dirty:false,authored:'rejected'}),true);
  try{
   await pointer('mousePressed',await point(slider),true);
   await pointer('mouseMoved',await point(slider,.8),true);
   await wait('window.b3Qa.held.length===1');
   await pointer('mouseMoved',await point(slider,.26),true);
   const latest=Number(await evaluate(range+'.value'));
   await evaluate('window.b3Qa.liveGain=true;window.b3Qa.release()');
   await idle();
   assert(!await evaluate('document.querySelector("[data-authored-feedback]")'),'native capacity is not a user-facing interruption');
   assert.equal((await commands()).length,1);
   assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'false');
   await evaluate('window.b3Qa.value.audioEffects.capacityRevision="2";window.b3Qa.value.audioEffects.effects[0].phase="applied";window.b3Qa.emit()');
   await wait('window.b3Qa.value.project.playlists[1].items[0].mediaGain==='+String(latest/100));
   assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,value:latest/100});
   assert.equal(await evaluate(range+'.value'),String(latest));
   assert.equal((await commands()).length,2,'only the latest value is admitted after capacity reopens');
   await pointer('mouseReleased',await point(slider,.26));
   await idle();assert.equal((await commands()).length,2);
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 await check('Volume retries one latest unsent draft when Seek preparation fails before changing audio capacity',async()=>{
  const initial=base();initial.active={...initial.selection};
  await load(initial);const slider=field('Volume'),timeline='.timeline-groove';
  await wait('document.querySelector(".session-timeline").dataset.enabled==="true"');
  await evaluate('window.b3Qa.liveGain=true;window.b3Qa.holdFailedSeek=true');
  await input(slider,'45');await idle();
  const capacity=await evaluate('window.b3Qa.value.audioEffects.capacityRevision');
  const gains=async()=>(await commands()).filter(command=>command.type==='SetItemGain');
  try{
   await pointer('mousePressed',await point(timeline,.55),true);
   await pointer('mouseReleased',await point(timeline,.55));
   await wait('window.b3Qa.heldSeek');
   await pointer('mousePressed',await point(slider,.24),true);
   await idle();
   assert.equal((await gains()).length,2);
   assert.equal(await evaluate('window.b3Qa.value.project.playlists[1].items[0].mediaGain'),.45);
   await pointer('mouseMoved',await point(slider,.71),true);
   const latest=Number(await evaluate(q(slider)+'.value'))/100;
   assert.equal((await gains()).length,2,'Seek preparation owns the only pending native operation');
   assert(!await evaluate('document.querySelector("[data-authored-feedback]")'));
   await evaluate('window.b3Qa.heldSeek()');
   await wait('window.b3Qa.value.project.playlists[1].items[0].mediaGain==='+String(latest));
   assert.equal(await evaluate('window.b3Qa.value.audioEffects.capacityRevision'),capacity);
   assert.equal((await gains()).length,3);
   assert.deepEqual((await gains()).at(-1).payload,{playlistId:ids.b,itemId:ids.video,value:latest});
   await pointer('mouseReleased',await point(slider,.71));await idle();
   assert.equal((await gains()).length,3);
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 await check('Volume admits live values before release and coalesces rapid reverse, overdrag and the next gesture',async()=>{
  const initial=base();initial.document.cleanRevision='11';initial.document.dirty=false;
  await load(initial);const slider=field('Volume');const range=q(slider);
  const thumb=async()=>Number(await evaluate(range+'.value'));
  const gainCommands=async()=>(await commands()).filter(c=>c.type==='SetItemGain');
  await evaluate('window.b3Qa.liveGain=true;void(window.b3DragSlider='+range+')');
  try{
   await pointer('mousePressed',await point(slider),true);
   const live=[];
   for(const position of [.69,.32,.58]){
    await pointer('mouseMoved',await point(slider,position),true);
    live.push(await thumb());
    await idle();
    assert.equal(await evaluate('window.b3Qa.value.project.playlists[1].items[0].mediaGain'),live.at(-1)/100);
   }
   assert(live[0]>live[1]&&live[1]<live[2],'native gain follows both directions while the button stays down');
   assert.deepEqual((await gainCommands()).map(c=>c.payload.value),live.map(percent=>percent/100));
   assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'true');
   await evaluate('window.b3Qa.hold=true');
   await pointer('mouseMoved',{x:1438,y:100},true);
   assert.equal(await thumb(),100,'right overdrag clamps to 100%');
   await wait('window.b3Qa.held.length===1');
   const projected=await evaluate('structuredClone(window.b3Qa.value)');projected.document.authoredRevision=String(BigInt(projected.document.authoredRevision)+1n);projected.project.playlists[0].masterVolume=.63;
   await evaluate('window.b3Qa.value='+JSON.stringify(projected)+';window.b3Qa.emit()');
   await wait('document.querySelector(".production-session").dataset.authoredRevision==='+JSON.stringify(projected.document.authoredRevision));
   assert(await evaluate('window.b3DragSlider==='+range));assert.equal(await thumb(),100);
   await pointer('mouseMoved',{x:1,y:100},true);assert.equal(await thumb(),0,'left overdrag clamps to 0%');
   const outside=await point(slider,.76,7);await pointer('mouseMoved',outside,true);
   const first=await thumb();assert(first>60&&first<90);
   await pointer('mouseReleased',outside);
   assert.equal((await gainCommands()).length,live.length+1,'held ACK bounds all additional movement to the latest local draft');
   assert.equal(await thumb(),first,'Volume thumb retains the released draft while waiting');
   await pointer('mousePressed',await point(slider,.45),true);await pointer('mouseMoved',await point(slider,.19),true);
   const second=await thumb();assert(second<30);
   await pointer('mouseReleased',await point(slider,.19));assert.equal((await gainCommands()).length,live.length+1);
   assert.equal(await thumb(),second);
   await evaluate('window.b3Qa.release()');
   await wait('window.b3Qa.value.project.playlists[1].items[0].mediaGain==='+second/100);await idle();
   assert.deepEqual((await gainCommands()).map(c=>c.payload),[...live,100,second].map(percent=>({playlistId:ids.b,itemId:ids.video,value:percent/100})));
   assert.equal(await evaluate('window.b3Qa.value.project.playlists[1].items[0].mediaGain'),second/100);
   assert.equal(await thumb(),second);assert.equal(await evaluate('window.b3Qa.value.project.playlists[0].masterVolume'),.63);
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 await check('An unchanged Volume press cannot write stale authored gain after a native update',async()=>{
  await load();const slider=field('Volume');const atThumb=await point(slider);
  await evaluate(q(slider)+'.addEventListener("input",()=>window.gainInputs=(window.gainInputs??0)+1)');
  try{
   await pointer('mousePressed',atThumb,true);
   const projected=base();projected.project.playlists[1].items[0].mediaGain=.73;projected.document.authoredRevision='12';
   await evaluate('window.b3Qa.value='+JSON.stringify(projected)+';window.b3Qa.emit()');
   await wait('document.querySelector(".production-session").dataset.authoredRevision==="12"');
   await pointer('mouseReleased',atThumb);
   assert.equal(await evaluate('window.gainInputs??0'),0,'no range input was produced by this press');
   assert.deepEqual(await commands(),[],'a press without input must not send a stale authored command');
   assert.equal(await evaluate(q(slider)+'.value'),'73');
  }finally{await pointer('mouseReleased',{x:700,y:400});}
 });
 for(const interruption of ['pointercancel','lostpointercapture','element blur','window blur','document hidden signal','pagehide','buttonless re-entry','invalidation','remount','same-path document replacement']){
  await check('Volume '+interruption+' retires the gesture without extra edits and permits the next drag',async()=>{
   await load();const slider=field('Volume'),range=q(slider);
   await evaluate('window.b3Qa.liveGain=true;void(window.b3DragSlider='+range+');window.gainTrace=[];for(const type of ["pointerdown","pointermove","gotpointercapture","lostpointercapture","input","pointerup"])window.b3DragSlider.addEventListener(type,e=>{if(type==="pointerdown")window.gainPointer=e.pointerId;window.gainTrace.push({type,id:e.pointerId,buttons:e.buttons,value:e.target.value});})');
   try{
    await pointer('mousePressed',await point(slider,.3),true);await pointer('mouseMoved',await point(slider,.65),true);
    assert(Number(await evaluate(range+'.value'))>60);await idle();
    const admitted=await commands();
    assert.equal(admitted.length,2);
    if(interruption==='pointercancel')await evaluate('window.b3DragSlider.dispatchEvent(new PointerEvent("pointercancel",{pointerId:window.gainPointer,bubbles:true}))');
    else if(interruption==='lostpointercapture')await evaluate('document.body.setPointerCapture(window.gainPointer)');
    else if(interruption==='element blur')await blur(slider);
    else if(interruption==='window blur')await evaluate('window.dispatchEvent(new FocusEvent("blur"))');
    else if(interruption==='document hidden signal')await evaluate('Object.defineProperty(document,"hidden",{value:true,configurable:true});document.dispatchEvent(new Event("visibilitychange"));delete document.hidden');
    else if(interruption==='pagehide')await evaluate('window.dispatchEvent(new PageTransitionEvent("pagehide"))');
    else if(interruption==='buttonless re-entry')await pointer('mouseMoved',await point(slider,.7));
    else{
     const changed=await evaluate('structuredClone(window.b3Qa.value)');
     if(interruption==='remount')changed.selection.itemId=duplicate;
     else if(interruption==='invalidation')changed.host.closing=true;
     else {
      changed.audioEffects.documentIncarnation='30000000-0000-0000-0000-000000000009';
      changed.project.playlists[1].items[0].mediaGain=.5;
      changed.document.authoredRevision=String(BigInt(changed.document.authoredRevision)+1n);
     }
     await evaluate('window.b3Qa.value='+JSON.stringify(changed)+';window.b3Qa.emit()');
     if(interruption==='remount')await wait('window.b3DragSlider!=='+range);
     else if(interruption==='invalidation')await wait(range+'.matches(":disabled")');
     else await wait('window.b3DragSlider!=='+range+'&&document.querySelector(".production-session").dataset.authoredRevision==='+JSON.stringify(changed.document.authoredRevision));
    }
    await pointer('mouseMoved',await point(slider,.82),true);await pointer('mouseReleased',{x:690,y:90});
    if(interruption==='lostpointercapture'){
     const trace=await evaluate('window.gainTrace');
     assert(trace.some(event=>event.type==='lostpointercapture'&&event.buttons===1),'capture must be lost while the primary button remains held');
    }
    assert.deepEqual(await commands(),admitted,'retired drag may not write: '+JSON.stringify(await evaluate('window.gainTrace')));
    const selected=await evaluate('window.b3Qa.value.project.playlists[1].items.find(item=>item.id===window.b3Qa.value.selection.itemId).mediaGain');
    assert.equal(await evaluate(range+'.value'),String(Math.round(selected*100)));
    if(interruption==='invalidation'){await evaluate('window.b3Qa.value.host.closing=false;window.b3Qa.emit()');await wait('!'+range+'.matches(":disabled")');}
    const final=await evaluate('structuredClone(window.b3Qa.value)'),itemId=final.selection.itemId;
    await pointer('mousePressed',await point(slider,.19),true);
    const value=Number(await evaluate(range+'.value'));assert(value<31);
    await pointer('mouseReleased',await point(slider,.19));await idle();
    const gain=(await commands()).slice(admitted.length);
    assert.deepEqual(gain.map(c=>c.payload),[{playlistId:ids.b,itemId,value:value/100}]);
    assert.equal(gain[0].audioEffects.documentIncarnation,final.audioEffects.documentIncarnation);
   }finally{await pointer('mouseReleased',{x:700,y:400});}
  });
 }
 await check('Rate draft survives unrelated authoritative revision; only Apply sends the selected stable ID and runtime warning leaves authored commit visible',async()=>{
  await load();await input(field('Configured Rate'),'1.7');assert.deepEqual(await commands(),[]);
  let value=base();value.document.authoredRevision='12';value.project.playlists[0].masterVolume=.61;await evaluate('window.b3Qa.value='+JSON.stringify(value)+';window.b3Qa.emit()');await wait('document.querySelector(".production-session").dataset.authoredRevision==="12"');assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.7');
  value=structuredClone(value);value.document.authoredRevision='13';value.project.playlists[1].items[0].rate=1.7;await prepare(value,result({itemId:ids.video,revision:'13',runtime:'warning',warnings:[{code:'rate',message:'Configured rate committed; Engine rate unchanged'}]}),true);
  await click('[aria-label="Apply Rate"]');await wait('window.b3Qa.held.length===1');assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.authoredRate'),'1.25');assert.equal(await evaluate('document.querySelector(".production-session").dataset.authoredRevision'),'12');
  assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,value:1.7});assert.equal((await last()).type,'ApplyVideoRate');await evaluate('window.b3Qa.release()');await idle();assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.authoredRate'),'1.7');assert.equal(await evaluate(q('[aria-label="Actual Rate"]')+'.textContent'),'—');assert.match(await evaluate('document.querySelector("[data-authored-feedback]").textContent'),/committed; Engine rate unchanged/);await dismiss();
 });
 await check('Native selection projects stable IDs without dirty or active change; same Playlist click preserves selected Item',async()=>{
  await load();await selectPlaylist(ids.a);assert.deepEqual((await last()).payload,{playlistId:ids.a});assert.equal((await last()).type,'SelectPlaylist');assert.equal(await evaluate('document.querySelector(".production-session").dataset.activeItemId'),ids.image);assert.equal(await evaluate('document.querySelector(".production-session").dataset.authoredRevision'),'11');
  await selectPlaylist(ids.b);await selectItem(duplicate);assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:duplicate});const count=(await commands()).length;await click(row('playlist',ids.b)+' .entity-select');assert.equal((await commands()).length,count);assert.equal(await evaluate('document.querySelector(".production-session").dataset.selectedItemId'),duplicate);
  await evaluate(q(row('item',duplicate)+' .entity-select')+'.dispatchEvent(new MouseEvent("dblclick",{bubbles:true}))');await rest();assert.equal((await commands()).length,count+1);assert.equal((await last()).type,'PlayItem');assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:duplicate});
 });
 await check('Rename captures ID and expected alias across authoritative reorder; stale delete keeps captured precondition',async()=>{
  await load();await selectItem(duplicate);await entityMenu('item',duplicate);await click('[data-command=rename]');await wait('document.querySelector("[data-rename-value]")');await input('[data-rename-value]','Renamed duplicate');
  let value=base();value.selection.itemId=duplicate;value.project.playlists[1].items.reverse();await evaluate('window.b3Qa.value='+JSON.stringify(value)+';window.b3Qa.emit()');await wait('document.querySelector(".playlist-list .item-entity").dataset.itemId==='+JSON.stringify(image));
  value=structuredClone(value);value.project.playlists[1].items.find(item=>item.id===duplicate).alias='Renamed duplicate';value.document.authoredRevision='12';await prepare(value,result({itemId:duplicate}));await click('[data-dialog-confirm]');await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:duplicate,alias:'Renamed duplicate',expectedAlias:'Duplicate item'});
  await entityMenu('item',duplicate);await click('[data-command=delete]');await wait('document.querySelector("[data-dialog-confirm]")');value=structuredClone(value);value.project.playlists[1].items.find(item=>item.id===duplicate).alias='External new alias';await evaluate('window.b3Qa.value='+JSON.stringify(value)+';window.b3Qa.emit()');await prepare(value,result({status:'stale-value',itemId:duplicate,authored:'rejected'}));await click('[data-dialog-confirm]');await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:duplicate,expectedAlias:'Renamed duplicate'});assert.equal((await last()).type,'RemoveItem');assert.match(await evaluate('document.querySelector("[data-authored-feedback]").textContent'),/stale-value/);assert(await evaluate('!!document.querySelector('+JSON.stringify(row('item',duplicate))+')'));await dismiss();
 });
 await check('Adjacent move is ID-relative and list order waits for authoritative snapshot',async()=>{
  await load();await selectItem(duplicate);await entityMenu('item',duplicate);const value=base();value.selection.itemId=duplicate;[value.project.playlists[1].items[0],value.project.playlists[1].items[1]]=[value.project.playlists[1].items[1],value.project.playlists[1].items[0]];value.document.authoredRevision='12';await prepare(value,result({itemId:duplicate}),true);await click('[data-command=move-up]');await wait('window.b3Qa.held.length===1');assert.equal(await evaluate('document.querySelector(".playlist-list .item-entity").dataset.itemId'),ids.video);assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:duplicate,direction:-1});await evaluate('window.b3Qa.release()');await idle();assert.equal(await evaluate('document.querySelector(".playlist-list .item-entity").dataset.itemId'),duplicate);
 });
 await check('Row and Inspector Master target the same explicit Playlist; native output mute and active binding stay independent',async()=>{
  await load();let value=base();value.project.playlists[0].masterVolume=0;value.document.authoredRevision='12';await prepare(value,result({playlistId:ids.a,runtime:'synchronized'}));await click(row('playlist',ids.a)+' .playlist-volume-trigger');await idle();assert.deepEqual((await last()).payload,{playlistId:ids.a,value:0});assert.equal(await evaluate('document.querySelector(".production-session").dataset.selectedPlaylistId'),ids.b);assert.equal(await evaluate('document.querySelector(".production-session").dataset.activePlaylistId'),ids.a);assert.equal(await evaluate('document.querySelector(".production-session").dataset.outputMuted'),'true');
  value.project.playlists[0].masterVolume=.7;value.document.authoredRevision='13';await prepare(value,result({playlistId:ids.a}));await click(row('playlist',ids.a)+' .playlist-volume-trigger');await idle();assert.deepEqual((await last()).payload,{playlistId:ids.a,value:.7});
  await selectPlaylist(ids.a);await selectPlaylist(ids.b);value=await evaluate('structuredClone(window.b3Qa.value)');value.project.playlists[1].masterVolume=.42;value.document.authoredRevision='14';await prepare(value,result());await input(field('Playlist Master Volume'),'42');await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,value:.42});assert.equal(await evaluate(q(row('playlist',ids.b)+' .playlist-volume-trigger')+'.dataset.masterPercent'),'42');assert.equal(await evaluate('window.b3Qa.value.project.playlists[0].masterVolume'),.7);assert.equal(await evaluate('document.querySelector(".production-session").dataset.outputMuted'),'true');
  value.project.playlists[1].masterVolume=0;value.document.authoredRevision='15';await prepare(value,result());await input(field('Playlist Master Volume'),'0');await idle();value.project.playlists[1].masterVolume=.42;value.document.authoredRevision='16';await prepare(value,result());await click(row('playlist',ids.b)+' .playlist-volume-trigger');await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,value:.42});
 });
 await check('Inspector sends scalar and presentation field patches, preserves loaded crop, and uses image editing-finished drafts',async()=>{
  await load();let value=base();value.project.playlists[1].items[0].presentation.mediaPresentation.mode='custom';await prepare(value,result({itemId:ids.video}));await click('[aria-label="Visual policy"]');await wait('document.querySelector("[data-choice-value=custom]")');await click('[data-choice-value=custom]');await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,patch:{mode:'custom'}});
  for(const [label,patch,number]of [['Uniform scale','scale',1.4],['Position X normalized','positionX',.35],['Position Y normalized','positionY',-.2]]){
   value=await evaluate('structuredClone(window.b3Qa.value)');if(patch==='scale'){value.project.playlists[1].items[0].presentation.layout.scaleX=number;value.project.playlists[1].items[0].presentation.layout.scaleY=number;}else value.project.playlists[1].items[0].presentation.layout[patch]=number;
   await prepare(value,result({itemId:ids.video}));const before=(await commands()).length;await input(field(label),number);assert.equal((await commands()).length,before);assert.equal(await evaluate(q(field(label))+'.value'),String(number));assert.equal(await evaluate('document.activeElement==='+q(field(label))),true,'Edited field must own focus');await blur(field(label));await idle();assert.equal((await commands()).length,before+1,'Editing finished must submit '+label);assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,patch:{[patch]:number}});
  }
  assert.deepEqual(await evaluate('window.b3Qa.value.project.playlists[1].items[0].presentation.crop'),{x:.1,y:.15,width:.8,height:.7});assert((await commands()).filter(c=>c.type==='SetItemPresentation').every(c=>!Object.hasOwn(c.payload.patch,'crop')));
  await selectItem(image);assert.equal(await evaluate(q(field('Image Duration'))+'.value'),'12.345');value=await evaluate('structuredClone(window.b3Qa.value)');value.project.playlists[1].items.find(item=>item.id===image).duration=14.2;await prepare(value,result({itemId:image}));const before=(await commands()).length;await input(field('Image Duration'),'14.2');assert.equal((await commands()).length,before);await blur(field('Image Duration'));await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:image,value:14.2});assert.equal((await last()).type,'SetImageDuration');
 });
 await check('Inline Item and Playlist names, item gain and custom aspect use native results and captured IDs',async()=>{
  await load();let value=base();value.project.playlists[1].items[0].alias='Inline item';await prepare(value,result({itemId:ids.video}));await input(field('Name'),'  Inline item  ');await blur(field('Name'));await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,alias:'  Inline item  ',expectedAlias:'Duplicate item'});assert.equal(await evaluate(q(field('Name'))+'.value'),'Inline item');
  value=await evaluate('structuredClone(window.b3Qa.value)');value.project.playlists[1].items[0].mediaGain=.36;await prepare(value,result({itemId:ids.video}));await input(field('Volume'),'36');await idle();assert.equal((await last()).type,'SetItemGain');assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,value:.36});
  value.project.playlists[1].items[0].presentation.mediaPresentation.mode='custom';await prepare(value,result({itemId:ids.video}));await click('[aria-label="Visual policy"]');await click('[data-choice-value=custom]');await idle();value.project.playlists[1].items[0].presentation.mediaPresentation.customAspect={numerator:7,denominator:3};await prepare(value,result({itemId:ids.video}));await input(field('Aspect numerator'),'21');await blur(field('Aspect numerator'));await idle();assert.deepEqual((await last()).payload,{playlistId:ids.b,itemId:ids.video,patch:{customAspect:{numerator:7,denominator:3}}});
  await selectPlaylist(ids.a);await selectPlaylist(ids.b);assert.equal(await evaluate('document.querySelector("[data-item-count]").textContent'),'3');value=await evaluate('structuredClone(window.b3Qa.value)');value.project.playlists[1].alias='Inline playlist';await prepare(value,result());await input(field('Name'),'Inline playlist');await blur(field('Name'));await idle();assert.equal((await last()).type,'RenamePlaylist');assert.deepEqual((await last()).payload,{playlistId:ids.b,alias:'Inline playlist',expectedAlias:'Duplicate playlist'});assert.equal(await evaluate(q(field('Name'))+'.value'),'Inline playlist');
 });
 await check('Playlist mutations retain typed commands alongside the current native playback and BGM entries',async()=>{
  await load();let value=base();const added='10000000-0000-0000-0000-000000000003';value.project.playlists.push({id:added,alias:'Native added playlist',enabled:true,masterVolume:1,itemMode:'list-loop',items:[]});value.selection={playlistId:added,itemId:null};await prepare(value,result({playlistId:added}));await click('[aria-label="Add Playlist"]');await idle();assert.equal((await last()).type,'AddPlaylist');assert.deepEqual((await last()).payload,{});assert(await evaluate('!!'+q(row('playlist',added))));
  value.project.playlistMode='random';await prepare(value,result({playlistId:null}));await click('[aria-label="Project sequence policy"]');await wait('document.querySelector("[data-playback-mode-popup]")');await click('[data-choice-value=random]');await idle();assert.equal((await last()).type,'SetProjectPolicy');assert.deepEqual((await last()).payload,{mode:'random'});
  value.project.playlists[2].itemMode='repeat-current';await prepare(value,result({playlistId:added}));await click('[aria-label="Playlist Item sequence policy"]');await click('[data-choice-value=repeat-current]');await idle();assert.equal((await last()).type,'SetPlaylistPolicy');assert.deepEqual((await last()).payload,{playlistId:added,mode:'repeat-current'});
  await entityMenu('playlist',added);assert.equal(await evaluate('document.querySelector("[data-command=play]").disabled'),true);assert.equal(await evaluate('document.querySelector("[data-command=background-music]").disabled'),false);value.project.playlists[2].enabled=false;await prepare(value,result({playlistId:added}));await click('[data-command=availability]');await idle();assert.deepEqual((await last()).payload,{playlistId:added,enabled:false});assert.equal((await last()).type,'SetPlaylistEnabled');
  await entityMenu('playlist',added);[value.project.playlists[1],value.project.playlists[2]]=[value.project.playlists[2],value.project.playlists[1]];await prepare(value,result({playlistId:added}));await click('[data-command=move-up]');await idle();assert.deepEqual((await last()).payload,{playlistId:added,direction:-1});assert.equal((await last()).type,'MovePlaylist');
  await entityMenu('playlist',added);await click('[data-command=delete]');await wait('document.querySelector("[data-dialog-confirm]")');value.project.playlists=value.project.playlists.filter(playlist=>playlist.id!==added);value.selection={playlistId:ids.b,itemId:null};await prepare(value,result({playlistId:added}));await click('[data-dialog-confirm]');await idle();assert.deepEqual((await last()).payload,{playlistId:added,expectedAlias:'Native added playlist'});assert.equal((await last()).type,'RemovePlaylist');assert.equal(await evaluate('!!'+q(row('playlist',added))),false);
 });
 await check('Native Add Media ownership survives reload without replay; ordered source warnings display once',async()=>{
  await load();let value=base();value.mediaImport.pending={token,playlistId:ids.b};await prepare(value,result({status:'accepted',token,authored:'not-applicable'}));await click('[aria-label="Add Media"]');await wait('document.querySelector(".production-session").dataset.mediaImportToken==='+JSON.stringify(token));assert.deepEqual((await last()).payload,{playlistId:ids.b});assert.equal((await last()).type,'AddMedia');assert(await evaluate('[...document.querySelectorAll(".session-library button,.session-inspector input,.session-inspector button")].every(e=>e.matches(":disabled"))'));
  value=await evaluate('window.b3Qa.value');await reload(value);await wait('document.querySelector(".production-session").dataset.mediaImportToken==='+JSON.stringify(token));assert.deepEqual(await commands(),[]);
  value.mediaImport={pending:null,last:result({token,runtime:'warning',sources:[{source:'first.mp4',itemId:ids.video,authored:'committed',runtime:'warning',warnings:[{code:'runtime',message:'first warning'}]},{source:'bad.txt',itemId:null,authored:'unsupported',runtime:'not-required',warnings:[{code:'unsupported',message:'second warning'}]},{source:'last.mp4',itemId:duplicate,authored:'committed',runtime:'synchronized',warnings:[]} ]})};value.document.authoredRevision='12';await evaluate('window.b3Qa.value='+JSON.stringify(value)+';window.b3Qa.emit()');await wait('document.querySelector("[data-authored-feedback]")');assert.deepEqual(await evaluate('[...document.querySelectorAll("[data-authored-feedback] p")].map(e=>e.textContent)'),['first.mp4: first warning','bad.txt: second warning']);await dismiss();await evaluate('window.b3Qa.emit()');await idle();assert.equal(await evaluate('!!document.querySelector("[data-authored-feedback]")'),false);assert.deepEqual(await commands(),[]);
 });
 await check('Reload rereads native values/revisions/selection/active and resets only an unapplied draft',async()=>{
  await load();await input(field('Configured Rate'),'1.8');const value=await evaluate('window.b3Qa.value');await reload(value);assert.equal(await evaluate(q(field('Configured Rate'))+'.value'),'1.25');assert.equal(await evaluate('document.querySelector(".production-session").dataset.authoredRevision'),'11');assert.equal(await evaluate('document.querySelector(".production-session").dataset.selectedItemId'),ids.video);assert.equal(await evaluate('document.querySelector(".production-session").dataset.activeItemId'),ids.image);assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'true');assert.deepEqual(await commands(),[]);
 });
 await check('B3 dirty projection composes with the B2 Save command and native clean revision',async()=>{
  await load();const value=base();value.document.cleanRevision='11';value.document.dirty=false;await prepare(value,{status:'accepted',token});await click('.session-project-trigger');await wait('document.querySelector("[data-command=save]")');await click('[data-command=save]');await wait('document.querySelector(".production-session").dataset.dirty==="false"');assert.equal((await last()).type,'SaveDocument');assert.deepEqual((await last()).payload,{});assert.equal(await evaluate('document.querySelector(".production-session").dataset.cleanRevision'),'11');
 });
 if(humanBlockers){
 const geometry='window.b3Qa.sent.filter(c=>c.type==="SetPreviewRect")';
 async function bounds(selector){return evaluate('(()=>{const r='+q(selector)+'.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}})()');}
 async function contextAt(id,x,y){await evaluate(q(row('playlist',id)) + '.dispatchEvent(new MouseEvent("contextmenu",{bubbles:true,cancelable:true,clientX:'+x+',clientY:'+y+'}))');await wait('document.querySelector("[data-command=rename]")');await idle();}
 await check('Long active Playlist and Item labels yield to all trailing controls at the accepted narrow Library width',async()=>{
  const value=base(),alias='ExtremelyLongUnbrokenNativeAlias_'+('Long authored name '.repeat(20));value.project.playlists[1].alias=alias;value.project.playlists[1].items[0].alias=alias;value.active={playlistId:ids.b,itemId:ids.video};await load(value);
  for(let n=0;n<3;n++)await evaluate('document.querySelector("[data-layout-splitter=left]").dispatchEvent(new KeyboardEvent("keydown",{key:"ArrowLeft",bubbles:true,cancelable:true}))');await rest();assert.equal(await evaluate('document.querySelector(".session-workspace").dataset.layoutLeft'),'200');
  const layout=await evaluate('('+String((selectors)=>selectors.map(selector=>{const row=document.querySelector(selector),name=row.querySelector('.entity-alias'),active=row.querySelector('.entity-active'),select=row.querySelector('.entity-select'),actions=row.querySelector('.entity-more'),speaker=row.querySelector('.playlist-volume-trigger');const rect=e=>{const r=e?.getBoundingClientRect();return r?{x:r.x,right:r.right,width:r.width}:null;};return {row:rect(row),name:rect(name),active:rect(active),select:rect(select),actions:rect(actions),speaker:rect(speaker),nameClient:name.clientWidth,nameScroll:name.scrollWidth,selectClient:select.clientWidth,selectScroll:select.scrollWidth,rowClient:row.clientWidth,rowScroll:row.scrollWidth,nameText:name.textContent,title:select.title};})) + ')('+JSON.stringify([row('playlist',ids.b),row('item',ids.video)])+')');
  writeFileSync(new URL(red?'long-name-red-layout.json':'long-name-layout.json',evidence),JSON.stringify(layout,null,2));
  for(const data of layout){assert(data.name.width>0,'Name retains a visible text region');assert(data.nameScroll>data.nameClient,'Long label ellipsizes: '+JSON.stringify(data));assert(data.name.right<=data.active.x+1,'Alias must end before ACTIVE');assert(data.active.right<=data.select.right+1,'ACTIVE remains inside select lane');assert(data.select.right<=Math.min(data.speaker?.x??Infinity,data.actions.x)+1,'Select lane ends before trailing controls');assert(data.actions.width>=27&&data.actions.right<=data.row.right+1,'Actions retain width inside row');assert(data.selectScroll<=data.selectClient+1,'Select lane does not internally overflow: '+JSON.stringify(data));assert(data.rowScroll<=data.rowClient+1,'Row does not horizontally overflow');assert.equal(data.nameText,alias);assert.equal(data.title,alias);if(data.speaker)assert(data.speaker.width>=25&&data.speaker.right<=data.actions.x+1,'Speaker has its reserved trailing lane');}
  assert.equal(await evaluate(q(field('Name'))+'.value'),alias);assert.deepEqual(await commands(),[]);
 });
 await check('Current B3 entity floating rect and Rename/Delete modal facts accompany the same live Preview geometry',async()=>{
  await load();await wait(geometry+'.length>0');const preview=await bounds('.preview-native-surface');await contextAt(ids.b,preview.x+preview.width*.4,preview.y+preview.height*.4);const menu=await bounds('.session-command-menu');assert(menu.x<preview.x+preview.width&&menu.x+menu.width>preview.x&&menu.y<preview.y+preview.height&&menu.y+menu.height>preview.y,'Actual entity menu intersects Preview');await pause(80);
  const payload=await evaluate(geometry+'.at(-1).payload');assert(payload.occlusion,'Existing geometry must include current B3 overlay facts');assert.equal(payload.occlusion.modalActive,false);assert(payload.occlusion.rects.some(rect=>JSON.stringify(rect)===JSON.stringify(menu)),'Wire carries actual current menu bounds');assert.deepEqual(payload.rect,await bounds('.preview-native-surface'));
  await click('[data-command=rename]');await wait('document.querySelector("[data-rename-value]")');await wait(geometry+'.at(-1).payload.occlusion?.modalActive===true');await click('[data-dialog-cancel]');await wait(geometry+'.at(-1).payload.occlusion?.modalActive===false&&'+geometry+'.at(-1).payload.occlusion.rects.length===0');
  await contextAt(ids.b,1428,12);await pause(80);const outside=await bounds('.session-command-menu');const current=await bounds('.preview-native-surface');assert(outside.x>=current.x+current.width||outside.x+outside.width<=current.x||outside.y+outside.height<=current.y,'Actual edge menu is disjoint');assert.equal(await evaluate(geometry+'.at(-1).payload.occlusion.modalActive'),false);assert.deepEqual(await evaluate(geometry+'.at(-1).payload.occlusion.rects'),[outside]);await click('[data-command=delete]');await wait('document.querySelector("[data-dialog-confirm]")');await wait(geometry+'.at(-1).payload.occlusion?.modalActive===true');await click('[data-dialog-cancel]');await wait(geometry+'.at(-1).payload.occlusion?.modalActive===false&&'+geometry+'.at(-1).payload.occlusion.rects.length===0');
 });
 await check('B3 Inspector choices and policy popups publish current geometry through resize/F5; Settings remains outside this bounded scope',async()=>{
  await load();await wait(geometry+'.length>0');await click('[aria-label="Visual policy"]');await wait(geometry+'.at(-1).payload.occlusion.rects.length===1');assert.deepEqual(await evaluate(geometry+'.at(-1).payload.occlusion'),{modalActive:false,rects:[await bounds('.inspector-choice-menu')]});await key('Escape');await wait(geometry+'.at(-1).payload.occlusion.rects.length===0');
  await click('[aria-label="Project sequence policy"]');await wait('document.querySelector("[data-playback-mode-popup]")');await wait(geometry+'.at(-1).payload.occlusion.rects.length===1');await send('Emulation.setDeviceMetricsOverride',{width:1280,height:850,deviceScaleFactor:1.25,mobile:false});await wait(geometry+'.at(-1).payload.viewport.width===1280');await rest();await pause(50);assert.deepEqual(await evaluate(geometry+'.at(-1).payload.rect'),await bounds('.preview-native-surface'));assert.deepEqual(await evaluate(geometry+'.at(-1).payload.occlusion'),{modalActive:false,rects:[await bounds('[data-playback-mode-popup]')]});await key('Escape');await wait(geometry+'.at(-1).payload.occlusion.rects.length===0');
  await click('[data-session-settings-trigger]');await rest();assert.equal(await evaluate('document.querySelectorAll("[data-native-preview-overlay]").length'),0);await key('Escape');await reload(base());await wait(geometry+'.length>0');assert.deepEqual(await evaluate(geometry+'.at(-1).payload.occlusion'),{modalActive:false,rects:[]});assert.deepEqual(await commands(),[]);await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
 });
 }
 await check('No uncaught runtime error, React warning or browser error occurred',()=>{assert.deepEqual(errors,[]);assert.deepEqual(warnings,[]);});
}catch(error){checks.push({name:'Harness startup/completion',pass:false,error:error.stack??String(error)});process.exitCode=1;}
finally{
 try{socket?.close();}catch{}
 browser.kill();server.kill();
 await Promise.all([new Promise(resolve=>browser.exitCode!==null?resolve():browser.once('exit',resolve)),new Promise(resolve=>server.exitCode!==null?resolve():server.once('exit',resolve))]);
 const absolute=resolve(profile),parent=resolve(tmpdir())+sep;assert(absolute.startsWith(parent)&&absolute.split(sep).at(-1).startsWith('dse-b3-frontend-'),'Owned temporary profile verified before deletion');
 for(let n=0;n<20;n++){try{rmSync(absolute,{recursive:true,force:true});break;}catch{await pause(100);}}
 if(existsSync(absolute))checks.push({name:'Owned temporary profile cleanup',pass:false,error:absolute});
 const report={pass:checks.length>0&&checks.every(check=>check.pass),checks,errors,warnings,proofScope:'Actual production React DOM against a controlled native wire; not native domain/runtime proof',temporaryProfileRetained:existsSync(absolute),environmentSetup:{restrictedChromeAttempt:'failed before any product checks',exitCode:4294930433,message:'crash server failed to launch, self-terminating',retry:'Same isolated harness succeeded with process-launch escalation; this is not product RED'},serverLog};writeFileSync(new URL(red?'dom-red.json':'dom-checks.json',evidence),JSON.stringify(report,null,2));for(const check of checks)console.log((check.pass?'PASS':'FAIL')+' '+check.name+(check.error?'\n'+check.error:''));console.log(checks.filter(check=>check.pass).length+'/'+checks.length);if(!report.pass)process.exitCode=1;
}
