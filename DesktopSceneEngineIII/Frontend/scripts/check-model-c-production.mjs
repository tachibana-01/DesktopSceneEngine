import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {ProductionPreviewFacade,observeNativePreviewRect} from '../src/ProductionPreviewGeometry.ts';
import {isModelCSessionPage} from '../src/sessionComposition.ts';
import {generation,incarnation,productionSessionUrl} from './session-readonly-fixture.mjs';

const checks=[];
async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
const turn=async()=>{for(let i=0;i<8;i++)await Promise.resolve();};
const intent=()=>({rect:{x:100.25,y:120.5,width:800,height:450},viewport:{width:1440,height:900},rasterizationScale:1.5,visible:true});
function bridge(){const listeners=new Set();return {sent:[],listeners,postMessage(value){this.sent.push(value);},addEventListener(_,fn){listeners.add(fn);},removeEventListener(_,fn){listeners.delete(fn);},receive(value){for(const fn of listeners)fn({data:value});}};}
const accepted=(revision='1',patch={})=>({revision,page:generation,binding:'1',requestId:crypto.randomUUID(),rect:{x:150,y:180,width:1200,height:675},contentGeneration:'1',deviceGeneration:'1',...patch});
const current=(value,patch={})=>({acceptedThrough:value?.revision??'0',accepted:value,lifecycleValid:Boolean(value),reason:value?'accepted':'initializing',apertureOwner:'host-composition-clip',...patch});
const event=(value,patch={})=>({kind:'event',version:1,type:'PreviewPresentationState',generation,sessionIncarnation:incarnation,payload:value,...patch});
const projection=(command,value,outcome='accepted')=>({status:value?'visible':'suspended',desiredAccepted:outcome==='accepted',desiredRequestId:command.requestId,requestOutcome:outcome,reason:outcome,accepted:value,lifecycleValid:Boolean(value),apertureOwner:'host-composition-clip'});
const response=(command,payload,patch={})=>({kind:'result',version:1,requestId:command.requestId,type:'SetPreviewRect',generation,sessionIncarnation:incarnation,ok:true,payload,...patch});
const facade=wire=>new ProductionPreviewFacade(wire,generation,incarnation);

await check('One production Session route admits Model C and old phase combinations fail closed',()=>{
 const query=new URL(productionSessionUrl('http://127.0.0.1')).search;
 assert.equal(isModelCSessionPage(query),true);
 for(const search of [query.replace('mode=production','mode=prototype'),query.replace('view=session','view=launcher'),
  query.replace('model-c-production','legacy-windowed'),query.replace('model-c-production','composition-b4'),
  query.replace('&presentation=model-c-production',''),query+'&preview=native-b5a',query+'&documents=native-b2',
  query+'&authored=native-b3',query+'&runtime=native-b4',query+'&session='+incarnation])assert.equal(isModelCSessionPage(search),false);
});
await check('Current native presentation arrives without a geometry request and copies wire facts',()=>{
 const wire=bridge(),f=facade(wire),seen=[];
 try{f.subscribePresentation(value=>seen.push(value));const value=accepted('18446744073709551614');wire.receive(event(current(value)));assert.equal(seen.at(-1).accepted.revision,value.revision);value.rect.width=1;assert.equal(seen.at(-1).accepted.rect.width,1200);assert.equal(wire.sent.length,0);}finally{f.dispose();}
});
await check('Pending and rejected desired geometry retain the actual accepted presentation',async()=>{
 const wire=bridge(),f=facade(wire),seen=[];
 try{f.subscribePresentation(value=>seen.push(value));const value=accepted();wire.receive(event(current(value)));for(const outcome of ['transient-layout-pending','stale-request','invalid-request']){const p=f.setRect(intent());assert.equal(seen.at(-1).accepted.revision,'1');wire.receive(response(wire.sent.at(-1),projection(wire.sent.at(-1),value,outcome)));assert.equal((await p).requestOutcome,outcome);assert.equal(seen.at(-1).accepted.revision,'1');}}finally{f.dispose();}
});
await check('No-op accepted desired request does not fabricate a new native revision or request identity',async()=>{
 const wire=bridge(),f=facade(wire),seen=[];
 try{f.subscribePresentation(value=>seen.push(value));const value=accepted('4');wire.receive(event(current(value)));const p=f.setRect(intent()),command=wire.sent.at(-1);assert.notEqual(value.requestId,command.requestId);wire.receive(response(command,projection(command,value)));assert.equal((await p).desiredAccepted,true);assert.equal(seen.at(-1).accepted.revision,'4');assert.equal(seen.at(-1).accepted.requestId,value.requestId);}finally{f.dispose();}
});
await check('Explicit desired command rejection retains current native accepted truth until authoritative retirement',async()=>{
 const wire=bridge(),f=facade(wire),seen=[];
 try{f.subscribePresentation(value=>seen.push(value));const value=accepted('4');wire.receive(event(current(value)));const p=f.setRect(intent()),command=wire.sent.at(-1),failure=assert.rejects(p,/desired-request-rejected/);const {payload,...envelope}=response(command,null);wire.receive({...envelope,ok:false,error:'desired-request-rejected'});await failure;assert.deepEqual(seen.at(-1),current(value));wire.receive(event(current(null,{acceptedThrough:'4',reason:'page-retired'})));assert.equal(seen.at(-1).lifecycleValid,false);}finally{f.dispose();}
});
await check('Content replacement and true invalidation project independently, old results cannot resurrect retired facts',async()=>{
 const wire=bridge(),f=facade(wire),seen=[];
 try{f.subscribePresentation(value=>seen.push(value));const first=accepted('1'),next=accepted('2',{contentGeneration:'2'});wire.receive(event(current(first)));const p=f.setRect(intent()),command=wire.sent.at(-1);wire.receive(event(current(next)));assert.equal(seen.at(-1).accepted.contentGeneration,'2');wire.receive(event(current(null,{acceptedThrough:'2',reason:'device-invalid'})));assert.equal(seen.at(-1).lifecycleValid,false);wire.receive(response(command,projection(command,first,'stale-request')));await p;assert.equal(seen.at(-1).lifecycleValid,false);wire.receive(event(current(next)));assert.equal(seen.at(-1).lifecycleValid,false);wire.receive(event(current(accepted('3',{contentGeneration:'3',deviceGeneration:'2'}))));assert.equal(seen.at(-1).accepted.revision,'3');}finally{f.dispose();}
});
await check('Wrong page/incarnation and same-revision identity mutation cannot rewrite current truth',()=>{
 const wire=bridge(),f=facade(wire),seen=[];
 try{f.subscribePresentation(value=>seen.push(value));const value=accepted('3');wire.receive(event(current(value)));const count=seen.length;for(const message of [event(current(accepted('4')),{generation:crypto.randomUUID()}),event(current(accepted('4')),{sessionIncarnation:crypto.randomUUID()}),event(current(accepted('4',{page:crypto.randomUUID()}))),event(current({...value,rect:{...value.rect,width:1}})),event(current({...value,contentGeneration:'2'})),event(current(accepted('2'))),event(current(accepted('4',{contentGeneration:'0'})))])wire.receive(message);assert.equal(seen.length,count);}finally{f.dispose();}
});
await check('Unavailable transport clears local availability without fabricating native invalidation revision',async()=>{
 const wire=bridge(),f=facade(wire),seen=[];
 try{f.subscribePresentation(value=>seen.push(value));const value=accepted('5');wire.receive(event(current(value)));wire.postMessage=()=>{throw new Error('closed');};await assert.rejects(f.setRect(intent()),/transport-failure/);assert.equal(seen.at(-1),undefined);wire.receive(event(current(value)));assert.equal(seen.at(-1).accepted.revision,'5');}finally{f.dispose();}
});

async function browser(fn){
 const names=['window','document','Element','getComputedStyle','ResizeObserver','MutationObserver','requestAnimationFrame','cancelAnimationFrame'],originals=Object.fromEntries(names.map(name=>[name,globalThis[name]]));
 try{
  class Element{constructor(kind){this.dataset=kind?{nativePreviewOverlay:kind}:{};this.isConnected=true;this.hidden=false;this.tagName='DIV';this.rect={x:100,y:120,width:800,height:450};this.style={display:'block',visibility:'visible',opacity:'1'};}getBoundingClientRect(){return this.rect;}hasAttribute(){return false;}matches(){return Boolean(this.dataset.nativePreviewOverlay);}closest(){return this.matches()?this:null;}}
  const frames=new Map();let serial=0;const env={frames,overlays:[],surface:new Element(),viewport:{width:800,height:600}};
  env.surface.dataset={previewFitWidth:'800',previewFitHeight:'600'};env.surface.closest=()=>({getBoundingClientRect:()=>env.viewport});
  globalThis.window={innerWidth:1440,innerHeight:900,devicePixelRatio:1.5,addEventListener(){},removeEventListener(){}};globalThis.document={body:{},hidden:false,querySelectorAll:()=>env.overlays,addEventListener(){},removeEventListener(){}};
  globalThis.Element=Element;globalThis.getComputedStyle=e=>e.style;globalThis.ResizeObserver=class{observe(){}unobserve(){}disconnect(){}};globalThis.MutationObserver=class{observe(){}disconnect(){}};
  globalThis.requestAnimationFrame=callback=>{frames.set(++serial,callback);return serial;};globalThis.cancelAnimationFrame=id=>frames.delete(id);env.flush=()=>{const calls=[...frames.values()];frames.clear();for(const callback of calls)callback();};env.Element=Element;await fn(env);
 }finally{for(const [name,value]of Object.entries(originals)){if(value===undefined)delete globalThis[name];else globalThis[name]=value;}}
}
await check('Model C mismatch plus modal transports overlay facts without requesting Preview suspension',()=>browser(async env=>{
 const wire=bridge(),f=facade(wire),states=[],observation=observeNativePreviewRect(env.surface,f,value=>states.push(value));
 try{wire.receive(event(current(accepted())));env.viewport={width:640,height:500};env.overlays=[new env.Element('modal')];env.flush();assert.equal(wire.sent.at(-1).payload.visible,true);assert.equal(wire.sent.at(-1).payload.occlusion.modalActive,true);assert.equal(states.at(-1).status,'visible');wire.receive(response(wire.sent.at(-1),projection(wire.sent.at(-1),accepted(),'transient-layout-pending')));await turn();assert.equal(states.at(-1).status,'visible');}finally{observation.dispose();f.dispose();await turn();}
}));
await check('Model C zero desired extent remains a geometry request rather than page retirement',()=>browser(async env=>{
 const wire=bridge(),f=facade(wire),observation=observeNativePreviewRect(env.surface,f,()=>{});
 try{env.surface.rect.width=0;env.flush();assert.equal(wire.sent.at(-1).payload.rect.width,0);assert.equal(wire.sent.at(-1).payload.visible,true);}finally{observation.dispose();f.dispose();await turn();}
}));
await check('Native lifecycle closes observer while delayed desired results remain separate facts',()=>browser(async env=>{
 const wire=bridge(),f=facade(wire),states=[],observation=observeNativePreviewRect(env.surface,f,value=>states.push(value));
 try{env.flush();const command=wire.sent.at(-1),value=accepted();wire.receive(event(current(value)));assert.equal(states.at(-1).status,'visible');wire.receive(event(current(null,{acceptedThrough:'1',reason:'page-retired'})));assert.equal(states.at(-1).status,'suspended');assert.equal(states.at(-1).currentPresentation.reason,'page-retired');wire.receive(response(command,projection(command,value)));await turn();assert.equal(states.at(-1).status,'suspended');assert.equal(states.at(-1).desired.outcome,'accepted');}finally{observation.dispose();f.dispose();await turn();}
}));
const stage=process.env.DSE_CHECK_STAGE??'green',result={suite:'Model C production frontend',pass:checks.every(value=>value.pass),checks};
const output=new URL(process.env.DSE_MODEL_C_EVIDENCE??'../node_modules/.cache/current-contract/',import.meta.url);mkdirSync(output,{recursive:true});writeFileSync(new URL('model-c-'+stage+'.json',output),JSON.stringify(result,null,2)+'\n');
for(const value of checks)console.log((value.pass?'PASS ':'FAIL ')+value.name+(value.error?'\n'+value.error:''));console.log(checks.filter(value=>value.pass).length+'/'+checks.length);if(!result.pass)process.exitCode=1;
