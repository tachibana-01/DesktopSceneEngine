import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {ProductionPreviewFacade,observeNativePreviewRect} from '../src/ProductionPreviewGeometry.ts';
import {fixturePreviewReply,generation,incarnation} from './session-readonly-fixture.mjs';

const checks=[];async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
const turn=async()=>{await Promise.resolve();await Promise.resolve();await Promise.resolve();};
const intent=(width=800)=>({rect:{x:100.25,y:120.5,width,height:450},viewport:{width:1440,height:900},rasterizationScale:1.5,visible:true});
function bridge(){const listeners=new Set(),retired=[];return {listeners,retired,sent:[],
 postMessage(c){this.sent.push(c);},addEventListener(_,fn){assert(!listeners.has(fn));listeners.add(fn);},removeEventListener(_,fn){assert(listeners.delete(fn));retired.push(fn);},
 receive(v){for(const fn of listeners)fn({data:v});},late(v){for(const fn of retired)fn({data:v});}};}
const reply=fixturePreviewReply();
const response=(command,patch={})=>({kind:'result',version:1,requestId:command.requestId,type:'SetPreviewRect',generation,sessionIncarnation:incarnation,ok:true,...(patch.ok===false?{}:{payload:reply(command)}),...patch});
const event=(patch={})=>({kind:'event',version:1,type:'PreviewGeometryInvalidated',generation,sessionIncarnation:incarnation,payload:{},...patch});

await check('Presentation wire preserves fractional CSS rect, viewport and scale observation exactly',async()=>{
 const b=bridge(),f=new ProductionPreviewFacade(b,generation,incarnation),p=f.setRect(intent());const {requestId,...wire}=b.sent[0];assert.equal(typeof requestId,'string');assert.deepEqual(wire,{kind:'command',version:1,type:'SetPreviewRect',generation,sessionIncarnation:incarnation,payload:intent()});b.receive(response(b.sent[0]));assert.equal((await p).status,'visible');f.dispose();
});
await check('Only exact current-context native geometry invalidations reach subscribers',()=>{
 const b=bridge(),f=new ProductionPreviewFacade(b,generation,incarnation);let n=0;const off=f.subscribeInvalidation(()=>n++);
 for(const patch of [{generation:crypto.randomUUID()},{sessionIncarnation:crypto.randomUUID()},{version:2},{extra:true},{payload:{extra:true}},{type:'MainWindowSnapshotInvalidated'}])b.receive(event(patch));assert.equal(n,0);b.receive(event());assert.equal(n,1);off();b.receive(event());assert.equal(n,1);f.dispose();
});
await check('Stale native replies cannot resolve the current page desired geometry request',async()=>{
 const b=bridge(),f=new ProductionPreviewFacade(b,generation,incarnation);let completed=false;const p=f.setRect({...intent(),visible:false}).then(s=>{completed=true;return s;});
 b.receive(response(b.sent[0],{generation:crypto.randomUUID()}));b.receive(response(b.sent[0],{sessionIncarnation:crypto.randomUUID()}));await turn();assert.equal(completed,false);b.receive(response(b.sent[0]));assert.equal((await p).status,'suspended');f.dispose();
});
await check('Native attach failure, malformed envelope and unknown status never become success',async()=>{
 for(const patch of [{ok:false,error:'engine-attach-failure'},{version:2},{payload:{status:'ready'}},{payload:{status:'visible'}},{payload:{status:'visible',mock:true}}]){
  const b=bridge(),f=new ProductionPreviewFacade(b,generation,incarnation),p=f.setRect(intent());const failure=assert.rejects(p,patch.ok===false?/engine-attach-failure/:/invalid-preview/);b.receive(response(b.sent[0],patch));await failure;f.dispose();
 }
});
await check('Transport throws become explicit geometry failures and release pending timers',async()=>{
 const b=bridge();b.postMessage=()=>{throw new Error('closed');};const f=new ProductionPreviewFacade(b,generation,incarnation);await assert.rejects(f.setRect(intent()),/preview-geometry-transport-failure/);f.dispose();assert.equal(b.listeners.size,0);
});
await check('Fifty presentation disposal cycles unsubscribe and reject pending and late geometry',async()=>{
 const b=bridge();for(let i=0;i<50;i++){const f=new ProductionPreviewFacade(b,generation,incarnation);let events=0;f.subscribeInvalidation(()=>events++);const p=f.setRect(intent()),failure=assert.rejects(p,/preview-page-disposed/);f.dispose();await failure;b.late(response(b.sent.at(-1)));b.late(event());assert.equal(events,0);assert.equal(b.listeners.size,0);await assert.rejects(f.setRect(intent()),/preview-page-disposed/);}
});

// Deterministic layout scheduling boundary: actual facade and observer code,
// controllable browser primitives. Actual DOM measurement is tested separately.
await check('Resize bursts coalesce; latest queued layout wins, and teardown cancels all work',async()=>{
 const originals=Object.fromEntries(['window','document','ResizeObserver','MutationObserver','requestAnimationFrame','cancelAnimationFrame'].map(key=>[key,globalThis[key]]));
 try{
  const frames=new Map(),windowListeners=new Map(),documentListeners=new Map();let frameId=0,resize,disconnected=false,rect=intent().rect;
  const surface={isConnected:true,dataset:{previewFitWidth:'800',previewFitHeight:'600'},closest:()=>({getBoundingClientRect:()=>({width:800,height:600})}),getBoundingClientRect:()=>rect};
  globalThis.window={innerWidth:1440,innerHeight:900,devicePixelRatio:1.5,addEventListener:(k,fn)=>windowListeners.set(k,fn),removeEventListener:k=>windowListeners.delete(k)};
  globalThis.document={body:{},hidden:false,querySelectorAll:()=>[],addEventListener:(k,fn)=>documentListeners.set(k,fn),removeEventListener:k=>documentListeners.delete(k)};
  globalThis.ResizeObserver=class {constructor(callback){resize=callback;}observe(){}disconnect(){disconnected=true;}};
  globalThis.MutationObserver=class{observe(){}disconnect(){}};
  globalThis.requestAnimationFrame=callback=>{frames.set(++frameId,callback);return frameId;};globalThis.cancelAnimationFrame=id=>frames.delete(id);
  const flush=()=>{const pending=[...frames.values()];frames.clear();for(const callback of pending)callback();};
  const b=bridge(),f=new ProductionPreviewFacade(b,generation,incarnation),states=[];const observation=observeNativePreviewRect(surface,f,state=>states.push(state));
  for(let i=0;i<20;i++)resize();assert.equal(frames.size,1);flush();assert.equal(b.sent.length,1);
  rect={...rect,width:600};resize();flush();rect={...rect,width:800};resize();flush();assert.equal(b.sent.length,1);
  b.receive(response(b.sent[0]));await turn();assert.equal(b.sent.length,2);assert.equal(b.sent[1].payload.rect.width,800);
  b.receive(response(b.sent[1]));await turn();assert.equal(states.at(-1).status,'visible');assert.equal(states.at(-1).desired.pending,false);
  document.hidden=true;documentListeners.get('visibilitychange')();assert.equal(b.sent.at(-1).payload.visible,false);b.receive(response(b.sent.at(-1)));await turn();
  const acceptedThrough=states.at(-1).currentPresentation.acceptedThrough;
  b.receive(event({type:'PreviewPresentationState',payload:{acceptedThrough,accepted:null,lifecycleValid:false,reason:'page-retired',apertureOwner:'host-composition-clip'}}));assert.equal(states.at(-1).status,'suspended');
  resize();assert.equal(frames.size,1);observation.dispose();f.dispose();assert.equal(frames.size,0);assert.equal(windowListeners.size,0);assert.equal(documentListeners.size,0);assert(disconnected);resize();b.late(event());flush();assert.equal(b.sent.length,3);
 }finally{for(const [key,value]of Object.entries(originals)){if(value===undefined)delete globalThis[key];else globalThis[key]=value;}}
});
await check('A raw resized DOM sample waits for the current React fitted-layout input before native admission',async()=>{
 const originals=Object.fromEntries(['window','document','ResizeObserver','MutationObserver','requestAnimationFrame','cancelAnimationFrame'].map(key=>[key,globalThis[key]]));
 try{
  const frames=new Map();let frameId=0;const rect=intent().rect;
  const viewport={getBoundingClientRect:()=>({width:640.9,height:500.2})};
  const surface={isConnected:true,dataset:{previewFitWidth:'800',previewFitHeight:'600'},closest:()=>viewport,getBoundingClientRect:()=>rect};
  globalThis.window={innerWidth:1100,innerHeight:700,devicePixelRatio:1.5,addEventListener(){},removeEventListener(){}};
  globalThis.document={body:{},hidden:false,querySelectorAll:()=>[],addEventListener(){},removeEventListener(){}};
  globalThis.ResizeObserver=class{observe(){}disconnect(){}};
  globalThis.MutationObserver=class{observe(){}disconnect(){}};
  globalThis.requestAnimationFrame=callback=>{frames.set(++frameId,callback);return frameId;};globalThis.cancelAnimationFrame=id=>frames.delete(id);
  const flush=()=>{const callbacks=[...frames.values()];frames.clear();for(const callback of callbacks)callback();};
  const b=bridge(),f=new ProductionPreviewFacade(b,generation,incarnation),observation=observeNativePreviewRect(surface,f,()=>{});
  flush();assert.equal(b.sent.length,0,'unfitted layout must not be admitted');
  surface.dataset.previewFitWidth='640';surface.dataset.previewFitHeight='500';observation.remeasure();flush();
  assert.equal(b.sent.length,1);assert.deepEqual(b.sent[0].payload.rect,rect);
  b.receive(response(b.sent[0]));await turn();observation.dispose();f.dispose();assert.equal(frames.size,0);
 }finally{for(const [key,value]of Object.entries(originals)){if(value===undefined)delete globalThis[key];else globalThis[key]=value;}}
});
const evidence=new URL('../node_modules/.cache/current-contract/',import.meta.url);mkdirSync(evidence,{recursive:true});writeFileSync(new URL('model-c-geometry.json',evidence),JSON.stringify({checks,pass:checks.every(c=>c.pass),presentationDisposalCycles:50},null,2));for(const c of checks)console.log((c.pass?'PASS':'FAIL')+' '+c.name+(c.error?'\n'+c.error:''));console.log(checks.filter(c=>c.pass).length+'/'+checks.length);if(checks.some(c=>!c.pass))process.exitCode=1;
