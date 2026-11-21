import { chromeExecutable } from './chrome-executable.mjs';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {mkdirSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve,sep} from 'node:path';
import {fixturePreviewReply,fixtureRuntimeSample,generation,incarnation,productionSessionUrl,snapshot} from './session-readonly-fixture.mjs';

// Real production React route, controlled native wire peer. This proves page
// projection/routing/lifetime, not filesystem or actual native picker behavior.
const red=process.argv.includes('--red'),origin='http://127.0.0.1:9429',debug='http://127.0.0.1:9397';
const evidence=new URL('../node_modules/.cache/current-contract/document/',import.meta.url);mkdirSync(evidence,{recursive:true});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
for(const url of [origin,debug+'/json/list'])await assert.rejects(fetch(url),'Private QA port must be free: '+url);
let serverLog='';
const vite=spawn(process.execPath,[fileURLToPath(new URL('../node_modules/vite/bin/vite.js',import.meta.url)),'--host','127.0.0.1','--port','9429','--strictPort'],{cwd:fileURLToPath(new URL('../',import.meta.url)),windowsHide:true,stdio:['ignore','pipe','pipe']});
vite.stdout.on('data',b=>serverLog+=b);vite.stderr.on('data',b=>serverLog+=b);
const profile=mkdtempSync(join(tmpdir(),'dse-b2-frontend-'));
const chrome=spawn(chromeExecutable,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-address=127.0.0.1','--remote-debugging-port=9397','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:'ignore'});
const checks=[],errors=[],warnings=[];let socket,send,injection;
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
 async function install(value){
  if(injection)await send('Page.removeScriptToEvaluateOnNewDocument',{identifier:injection.identifier});
  injection=await send('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{
   const listeners=new Set();let audioNegotiated=false;const q=window.b2Qa={value:${JSON.stringify(value)},sent:[],holdAck:false,acks:[],holdReads:false,reads:[],
    emit(patch={}){for(const fn of listeners)fn({data:{kind:'event',version:1,type:'MainWindowSnapshotInvalidated',generation:${JSON.stringify(generation)},sessionIncarnation:${JSON.stringify(incarnation)},payload:{},...patch}});},
    deliver(c,payload){for(const fn of listeners)fn({data:{kind:'result',version:1,requestId:c.requestId,type:c.type,generation:c.generation,sessionIncarnation:c.sessionIncarnation,ok:true,payload}});},
    release(){for(const f of q.acks.splice(0))f();},
    releaseReads(){for(const f of q.reads.splice(0))f();},
    settle(outcome='completed',message=''){const p=q.value.documentOperation.pending;if(!p)throw new Error('No native operation');q.value.documentOperation={pending:null,last:{token:p.token,action:p.action,outcome,message}};q.emit();},
    stats(){return {listeners:listeners.size,commands:q.sent.map(c=>c.type)};}};
   const replyPreview=(${fixturePreviewReply.toString()})();const runtimeSample=${fixtureRuntimeSample.toString()};
   window.chrome??={};window.chrome.webview={
    postMessage(c){q.sent.push(c);if(c.type==='GetMainWindowSnapshot'){const deliver=()=>q.deliver(c,structuredClone(q.value));if(q.holdReads)q.reads.push(deliver);else queueMicrotask(deliver);return;}
     if(c.type==='SetPreviewRect'){queueMicrotask(()=>q.deliver(c,replyPreview(c)));return;}
     if(c.type==='GetRuntimePlaybackSample'){queueMicrotask(()=>q.deliver(c,runtimeSample(q.value)));return;}
     if(c.type==='NegotiateAudioEffects'){
      if(c.payload.version!==1)throw new Error('Invalid audio-effects negotiation');
      audioNegotiated=true;queueMicrotask(()=>q.deliver(c,structuredClone(q.value.audioEffects)));return;
     }
     if(!audioNegotiated||c.audioEffects?.version!==1||c.audioEffects.documentIncarnation!==q.value.audioEffects.documentIncarnation)throw new Error('Document command lacks the native document stamp');
     const actions={NewDocument:'new',OpenDocument:'open',SaveDocument:'save',SaveDocumentAs:'save-as',RequestClose:'close'};
     const active=q.value.documentOperation?.pending;
     let ack;if(c.type==='ContinueDocumentDecision'){
      if(!active||active.stage!=='decision'||c.payload.token!==active.token)ack={status:'stale-operation',token:null};
      else{ack={status:'accepted',token:active.token};if(c.payload.choice==='cancel')q.value.documentOperation={pending:null,last:{token:active.token,action:active.action,outcome:'cancelled',message:''}};else active.stage=c.payload.choice==='save'?'save-picker':'executing';}
     }else if(actions[c.type]){
      if(active)ack={status:'busy',token:active.token};
      else{const token=crypto.randomUUID();q.value.documentOperation.pending={token,action:actions[c.type],stage:['new','close'].includes(actions[c.type])&&q.value.document.dirty?'decision':c.type==='OpenDocument'?'open-picker':c.type==='SaveDocumentAs'?'save-picker':'executing'};ack={status:'accepted',token};}
     }else throw new Error('Unexpected production command '+c.type);
     const deliver=()=>{q.deliver(c,ack);q.emit();};if(q.holdAck)q.acks.push(deliver);else queueMicrotask(deliver);
    },
    addEventListener(type,fn){if(type!=='message'||listeners.has(fn))throw new Error('Duplicate handler');listeners.add(fn);},
    removeEventListener(type,fn){if(type!=='message'||!listeners.delete(fn))throw new Error('Unbalanced handler');}};
  })();`});
 }
 const ready='document.querySelector(".production-session")?.dataset.bootstrapState==="ready"';
 async function load(value){await install(value);const marker=crypto.randomUUID();await evaluate('window.b2Marker='+JSON.stringify(marker));await send('Page.navigate',{url:productionSessionUrl(origin)});await wait('window.b2Marker!=='+JSON.stringify(marker)+'&&document.querySelector(".production-session")&&document.querySelector(".production-session").dataset.bootstrapState!=="loading"');}
 async function assertReadOnlyStartup(){
  const sent=await evaluate('window.b2Qa.sent.map(c=>c.type)');
  assert.equal(sent.filter(type=>type==='GetMainWindowSnapshot').length,1);
  assert(sent.every(type=>['GetMainWindowSnapshot','GetRuntimePlaybackSample','SetPreviewRect'].includes(type)));
 }
 const native=snapshot();native.documentOperation={pending:null,last:null};
 await load(native);
 await check('The current production Session exposes Project commands and sends native New intent',async()=>{
  assert(await evaluate(ready),'Expected B2 bootstrap ready; actual '+JSON.stringify(await evaluate('({state:document.querySelector(".production-session")?.dataset.bootstrapState,code:document.querySelector(".production-session")?.dataset.failureCode,body:document.body.innerText})')));assert.equal(await evaluate('document.querySelector(".session-project-trigger").disabled'),false);
  await evaluate('document.querySelector(".session-project-trigger").click()');await wait('document.querySelector("[data-command=new]")');
  await evaluate('document.querySelector("[data-command=new]").click()');await wait('window.b2Qa.sent.some(c=>c.type==="NewDocument")');
  assert.deepEqual(await evaluate('window.b2Qa.sent.find(c=>c.type==="NewDocument").payload'),{});
 });
 if(!red){
 await check('Native pending decision preserves native identity and dirty truth; all non-document writes remain disabled',async()=>{
  await wait('document.querySelector("[data-decision-save]")');assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'true');
  assert.equal(await evaluate('document.querySelector(".session-project-name").textContent'),'native.dse');
  assert(await evaluate('[...document.querySelectorAll(".session-library button,.session-inspector input,.session-inspector button,.playback-rail button,.window-control:not(.window-control--close)")].every(e=>e.matches(":disabled"))'));
  assert.equal(await evaluate('document.querySelector(".session-project-trigger").disabled'),true);assert.equal(await evaluate('document.querySelector(".window-control--close")'),null);
  assert.equal(await evaluate('document.querySelector(".session-dev-trigger")'),null);await wait('document.querySelector(".preview-native-surface").dataset.nativePreviewState==="visible"');
 });
 await check('Three actual reloads reconstruct the same native pending token without replay or implicit decision',async()=>{
  const token=await evaluate('window.b2Qa.value.documentOperation.pending.token');
  for(let n=0;n<3;n++){
   const value=await evaluate('window.b2Qa.value');await install(value);const marker=crypto.randomUUID();await evaluate('window.b2Marker='+JSON.stringify(marker));await send('Page.reload',{ignoreCache:true});await wait('window.b2Marker!=='+JSON.stringify(marker)+'&&'+ready+'&&document.querySelector("[data-decision-save]")');
   assert.equal(await evaluate('window.b2Qa.value.documentOperation.pending.token'),token);await assertReadOnlyStartup();
  }
 });
 await check('Cancel continues exactly the reconstructed native operation once and leaves document unchanged',async()=>{
  const token=await evaluate('window.b2Qa.value.documentOperation.pending.token');await evaluate('document.querySelector("[data-decision-cancel]").click()');await wait('!document.querySelector("[data-decision-save]")&&!document.querySelector(".session-project-trigger").disabled');
  assert.deepEqual(await evaluate('window.b2Qa.sent.filter(c=>c.type==="ContinueDocumentDecision").map(c=>c.payload)'),[{token,choice:'cancel'}]);
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'true');assert.equal(await evaluate('document.querySelector(".session-project-name").textContent'),'native.dse');
 });
 async function command(name){await evaluate('document.querySelector(".session-project-trigger").click()');await wait('document.querySelector("[data-command='+name+']")');await evaluate('document.querySelector("[data-command='+name+']").click()');await wait('window.b2Qa.value.documentOperation.pending!==null');}
 await check('Save continuation requests native choice and follows native picker stage without clearing dirty',async()=>{
  await command('new');await wait('document.querySelector("[data-decision-save]")');const token=await evaluate('window.b2Qa.value.documentOperation.pending.token');
  await evaluate('document.querySelector("[data-decision-save]").click()');await wait('!document.querySelector("[data-decision-save]")');
  assert.deepEqual(await evaluate('window.b2Qa.sent.filter(c=>c.type==="ContinueDocumentDecision").at(-1).payload'),{token,choice:'save'});assert.equal(await evaluate('window.b2Qa.value.documentOperation.pending.stage'),'save-picker');
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'true');await evaluate('window.b2Qa.settle("save-cancelled")');await wait('!document.querySelector(".session-project-trigger").disabled');
 });
 await check('Discard routes exact token; command acknowledgement never fabricates replacement',async()=>{
  await command('new');await wait('document.querySelector("[data-decision-discard]")');const token=await evaluate('window.b2Qa.value.documentOperation.pending.token');await evaluate('document.querySelector("[data-decision-discard]").click()');
  await wait('window.b2Qa.value.documentOperation.pending.stage==="executing"');assert.deepEqual(await evaluate('window.b2Qa.sent.filter(c=>c.type==="ContinueDocumentDecision").at(-1).payload'),{token,choice:'discard'});
  assert.equal(await evaluate('document.querySelector(".session-project-name").textContent'),'native.dse');await evaluate('window.b2Qa.settle()');await wait('!document.querySelector(".session-project-trigger").disabled');
 });
 await check('Open, Save, Save As and Close use only frozen document command types',async()=>{
  for(const [name,type]of [['open','OpenDocument'],['save','SaveDocument'],['save-as','SaveDocumentAs'],['close','RequestClose']]){
   await command(name);assert.equal(await evaluate('window.b2Qa.sent.filter(c=>["OpenDocument","SaveDocument","SaveDocumentAs","RequestClose"].includes(c.type)).at(-1).type'),type);await evaluate('window.b2Qa.settle("cancelled")');await wait('!document.querySelector(".session-project-trigger").disabled');
  }
  assert.equal(await evaluate('document.querySelector(".window-control--close")'),null);
 });
 await check('Duplicate intent is suppressed during ack delivery; standard shortcuts route once and respect pending modal',async()=>{
  await evaluate('window.b2Qa.holdAck=true;for(let i=0;i<2;i++)window.dispatchEvent(new KeyboardEvent("keydown",{key:"s",ctrlKey:true,bubbles:true,cancelable:true}))');await wait('window.b2Qa.acks.length===1');assert.equal(await evaluate('window.b2Qa.acks.length'),1);
  await evaluate('window.b2Qa.holdAck=false;window.b2Qa.release()');await wait('document.querySelector(".session-project-trigger").disabled');await evaluate('window.b2Qa.settle("saved")');await wait('!document.querySelector(".session-project-trigger").disabled');
  await evaluate('window.dispatchEvent(new KeyboardEvent("keydown",{key:"n",ctrlKey:true,bubbles:true,cancelable:true}))');await wait('document.querySelector("[data-decision-save]")');const opens='window.b2Qa.sent.filter(c=>c.type==="OpenDocument").length',count=await evaluate(opens);await evaluate('window.dispatchEvent(new KeyboardEvent("keydown",{key:"o",ctrlKey:true,bubbles:true,cancelable:true}))');await pause(60);assert.equal(await evaluate(opens),count);await evaluate('document.querySelector("[data-decision-cancel]").click()');await wait('!document.querySelector(".session-project-trigger").disabled');
 });
 await check('Acknowledgement cannot release delivery gate before a delayed authoritative snapshot settles',async()=>{
  const before=await evaluate('window.b2Qa.sent.filter(c=>c.type==="SaveDocument").length');await evaluate('window.b2Qa.holdReads=true;window.dispatchEvent(new KeyboardEvent("keydown",{key:"s",ctrlKey:true,bubbles:true,cancelable:true}))');await wait('window.b2Qa.reads.length>0');
  await pause(60);assert.equal(await evaluate('document.querySelector(".session-project-trigger").disabled'),true);await evaluate('window.dispatchEvent(new KeyboardEvent("keydown",{key:"s",ctrlKey:true,bubbles:true,cancelable:true}))');await pause(60);assert.equal(await evaluate('window.b2Qa.sent.filter(c=>c.type==="SaveDocument").length'),before+1);
  await evaluate('window.b2Qa.holdReads=false;window.b2Qa.releaseReads()');await wait('document.querySelector(".production-session").dataset.documentOperationStage==="executing"');await evaluate('window.b2Qa.settle("saved")');await wait('!document.querySelector(".session-project-trigger").disabled');
 });
 await check('Authoritative save outcome updates native filename/revisions; failure feedback remains truthful across reload',async()=>{
  await command('save-as');await evaluate('window.b2Qa.value.document.path="C:\\\\B2\\\\saved.json";window.b2Qa.value.document.displayName="saved.json";window.b2Qa.value.document.cleanRevision=window.b2Qa.value.document.authoredRevision;window.b2Qa.value.document.dirty=false;window.b2Qa.settle("saved")');await wait('document.querySelector(".session-project-name").textContent==="saved.json"&&document.querySelector(".production-session").dataset.dirty==="false"');assert.equal(await evaluate('document.title'),'DesktopSceneEngine — saved.json');
  await command('open');await evaluate('window.b2Qa.settle("candidate-invalid","Native candidate rejected before replacement")');await wait('document.querySelector("[data-document-feedback]")');assert.match(await evaluate('document.querySelector("[data-document-feedback]").textContent'),/Native candidate rejected/);
  const value=await evaluate('window.b2Qa.value');await install(value);const marker=crypto.randomUUID();await evaluate('window.b2Marker='+JSON.stringify(marker));await send('Page.reload',{ignoreCache:true});await wait('window.b2Marker!=='+JSON.stringify(marker)+'&&'+ready+'&&document.querySelector("[data-document-feedback]")');assert.equal(await evaluate('document.querySelector(".session-project-name").textContent'),'saved.json');await assertReadOnlyStartup();await evaluate('document.querySelector("[data-message-ok]").click()');
 });
 await check('Tray failure uses the shared TS notice without a duplicate; other Save errors retain their TS feedback',async()=>{
  const value=snapshot();value.documentOperation={pending:null,last:{token:crypto.randomUUID(),action:'close',outcome:'save-failed',message:''}};
  value.sessionNotice={id:crypto.randomUUID(),level:'error',title:'Save Project Failed',message:'Tray Close save failed'};await load(value);await wait(ready);
  await wait('document.querySelector("[data-session-notice]")');
  assert.match(await evaluate('document.querySelector("[data-session-notice]").textContent'),/Tray Close save failed/);
  assert.equal(await evaluate('document.querySelector("[data-document-feedback]")===null'),true);
  assert.equal(await evaluate('document.querySelectorAll("dialog[open]").length'),1);
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'true');assert.equal(await evaluate('document.querySelector(".session-project-name").textContent'),'native.dse');await assertReadOnlyStartup();
  await evaluate('delete window.b2Qa.value.sessionNotice;window.b2Qa.value.documentOperation.last={token:crypto.randomUUID(),action:"close",outcome:"save-failed",message:"Native Close Save failed: disk persistence rejected"};window.b2Qa.emit()');await wait('document.querySelector("[data-document-feedback]")');assert.match(await evaluate('document.querySelector("[data-document-feedback]").textContent'),/Native Close Save failed: disk persistence rejected/);assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'true');
  await evaluate('document.querySelector("[data-message-ok]").click()');
  await evaluate('window.b2Qa.value.documentOperation.last={token:crypto.randomUUID(),action:"save",outcome:"save-failed",message:""};window.b2Qa.emit()');await wait('document.querySelector("[data-document-feedback]")');assert.match(await evaluate('document.querySelector("[data-document-feedback]").textContent'),/Save Project Failed/);assert.equal(await evaluate('document.querySelector(".production-session").dataset.dirty'),'true');
  await evaluate('document.querySelector("[data-message-ok]").click()');
 });
 await check('A partial document snapshot fails visibly without mounting an editor or replaying commands',async()=>{
  const partial=snapshot();delete partial.documentOperation;await load(partial);
  assert.equal(await evaluate('document.querySelector(".production-session").dataset.failureCode'),'SnapshotValidationFailure');
  assert.equal(await evaluate('document.querySelector(".session-project-trigger")'),null);
  assert.deepEqual(await evaluate('window.b2Qa.sent.map(c=>c.type)'),['GetMainWindowSnapshot']);
 });
 }
 assert.deepEqual(errors,[]);assert.deepEqual(warnings,[]);await send('Browser.close');
}catch(error){checks.push({name:'DOM harness',pass:false,error:error.stack??String(error)});}finally{
 socket?.close();vite.kill();for(let i=0;i<20&&chrome.exitCode===null;i++)await pause(100);if(chrome.exitCode===null)chrome.kill();
 const target=resolve(profile),base=resolve(tmpdir())+sep;if(!target.startsWith(base)||!target.slice(base.length).startsWith('dse-b2-frontend-'))throw new Error('Unsafe profile cleanup');rmSync(target,{recursive:true,force:true,maxRetries:5,retryDelay:100});
 writeFileSync(new URL(red?'dom-red.json':'dom-checks.json',evidence),JSON.stringify({checks,pass:checks.every(c=>c.pass),errors,warnings,serverLog,temporaryProfilesRetained:0,proofScope:'production React route + controlled native wire peer; not native filesystem/picker/Session authority proof'},null,2));
}
for(const c of checks)console.log((c.pass?'PASS':'FAIL')+' '+c.name+(c.error?'\n'+c.error:''));console.log(checks.filter(c=>c.pass).length+'/'+checks.length);if(checks.some(c=>!c.pass))process.exitCode=1;
