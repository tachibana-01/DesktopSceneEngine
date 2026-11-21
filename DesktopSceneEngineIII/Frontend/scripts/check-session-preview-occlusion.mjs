import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {ProductionPreviewFacade,observeNativePreviewRect} from '../src/ProductionPreviewGeometry.ts';
import {fixturePreviewReply,generation,incarnation} from './session-readonly-fixture.mjs';

// Actual geometry adapter/facade with deterministic browser primitives. These
// checks establish delivery/current observation, never native HWND visibility.
const checks=[];
async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
const turn=async()=>{for(let n=0;n<6;n++)await Promise.resolve();};
function bridge(){const listeners=new Set(),retired=[];return {sent:[],listeners,retired,postMessage(value){this.sent.push(value);},addEventListener(_,fn){listeners.add(fn);},removeEventListener(_,fn){listeners.delete(fn);retired.push(fn);},receive(value){for(const fn of listeners)fn({data:value});},late(value){for(const fn of retired)fn({data:value});}};}
const reply=fixturePreviewReply();
const response=command=>({kind:'result',version:1,requestId:command.requestId,type:'SetPreviewRect',generation:command.generation,sessionIncarnation:command.sessionIncarnation,ok:true,payload:reply(command)});
async function browser(fn){
 const names=['window','document','Element','getComputedStyle','ResizeObserver','MutationObserver','requestAnimationFrame','cancelAnimationFrame'],originals=Object.fromEntries(names.map(name=>[name,globalThis[name]]));
 try{
  class Element {
   constructor(kind,rect={x:40,y:50,width:180,height:90}){this.isConnected=true;this.hidden=false;this.tagName='DIV';this.dataset=kind?{nativePreviewOverlay:kind}:{};this.rect=rect;this.style={display:'block',visibility:'visible',opacity:'1'};this.attributes=new Set();}
   getBoundingClientRect(){return this.rect;}
   hasAttribute(name){return this.attributes.has(name);}
   matches(){return Boolean(this.dataset.nativePreviewOverlay);}
   closest(){return this.matches()?this:null;}
  }
  const frames=new Map(),windowListeners=new Map(),documentListeners=new Map(),mutations=[],resizes=[];let frameId=0;
  const env={Element,overlays:[],frames,mutations,resizes,windowListeners,documentListeners,surface:new Element(null,{x:100.25,y:120.5,width:800,height:450})};
  env.surface.dataset={previewFitWidth:'800',previewFitHeight:'600'};env.surface.closest=()=>({getBoundingClientRect:()=>({width:800,height:600})});
  globalThis.Element=Element;globalThis.getComputedStyle=element=>element.style;
  globalThis.window={innerWidth:1440,innerHeight:900,devicePixelRatio:1.5,addEventListener:(kind,fn)=>windowListeners.set(kind,fn),removeEventListener:(kind,fn)=>{if(windowListeners.get(kind)===fn)windowListeners.delete(kind);}};
  globalThis.document={body:{},hidden:false,querySelectorAll:()=>env.overlays,addEventListener:(kind,fn)=>documentListeners.set(kind,fn),removeEventListener:(kind,fn)=>{if(documentListeners.get(kind)===fn)documentListeners.delete(kind);}};
  globalThis.ResizeObserver=class {constructor(callback){this.callback=callback;this.observed=new Set();this.disconnected=false;resizes.push(this);}observe(element){this.observed.add(element);}unobserve(element){this.observed.delete(element);}disconnect(){this.disconnected=true;this.observed.clear();}};
  globalThis.MutationObserver=class {constructor(callback){this.callback=callback;this.disconnected=false;mutations.push(this);}observe(){}disconnect(){this.disconnected=true;}};
  globalThis.requestAnimationFrame=callback=>{frames.set(++frameId,callback);return frameId;};globalThis.cancelAnimationFrame=id=>frames.delete(id);
  env.flush=()=>{const pending=[...frames.values()];frames.clear();for(const callback of pending)callback();};
  env.mutate=(records=[{type:'childList',target:document.body}])=>{for(const mutation of mutations)mutation.callback(records);};
  await fn(env);
 }finally{for(const [name,value]of Object.entries(originals)){if(value===undefined)delete globalThis[name];else globalThis[name]=value;}}
}

await check('Model C always samples overlay facts atomically with the native extent',()=>browser(async env=>{
 const wire=bridge(),facade=new ProductionPreviewFacade(wire,generation,incarnation),observation=observeNativePreviewRect(env.surface,facade,()=>{});env.flush();
 assert.deepEqual(wire.sent[0].payload,{rect:env.surface.rect,viewport:{width:1440,height:900},rasterizationScale:1.5,visible:true,occlusion:{modalActive:false,rects:[]}});assert.equal(env.mutations.length,1);observation.dispose();facade.dispose();await turn();
}));
await check('One atomic observation contains the current Preview, modal fact and precise visible floating rects',()=>browser(async env=>{
 const floating=new env.Element('floating',{x:135.75,y:180.25,width:240.5,height:96.125}),modal=new env.Element('modal');env.overlays=[floating,modal];
 const wire=bridge(),facade=new ProductionPreviewFacade(wire,generation,incarnation),observation=observeNativePreviewRect(env.surface,facade,()=>{});env.flush();
 assert.deepEqual(wire.sent[0].payload.occlusion,{modalActive:true,rects:[floating.rect]});assert.deepEqual(wire.sent[0].payload.rect,env.surface.rect);assert.equal(wire.sent[0].payload.rasterizationScale,1.5);assert(env.resizes[0].observed.has(floating));assert(env.resizes[0].observed.has(modal));
 observation.dispose();facade.dispose();await turn();
}));
await check('Closed/hidden/disconnected/zero surfaces do not become occluders; current DOM removal produces a fresh measurement',()=>browser(async env=>{
 const closed=new env.Element('modal');closed.tagName='DIALOG';const hidden=new env.Element('floating');hidden.style.visibility='hidden';const detached=new env.Element('modal');detached.isConnected=false;const zero=new env.Element('floating',{x:0,y:0,width:0,height:10}),open=new env.Element('floating');env.overlays=[closed,hidden,detached,zero,open];
 const wire=bridge(),facade=new ProductionPreviewFacade(wire,generation,incarnation),observation=observeNativePreviewRect(env.surface,facade,()=>{});env.flush();assert.deepEqual(wire.sent[0].payload.occlusion,{modalActive:false,rects:[open.rect]});wire.receive(response(wire.sent[0]));await turn();
 env.overlays=[];env.mutate();env.flush();assert.deepEqual(wire.sent[1].payload.occlusion,{modalActive:false,rects:[]});assert(!env.resizes[0].observed.has(open));observation.dispose();facade.dispose();await turn();
}));
await check('A queued overlay close is remeasured after native reply and cannot restore over a newer modal',()=>browser(async env=>{
 const old=new env.Element('floating');env.overlays=[old];const wire=bridge(),facade=new ProductionPreviewFacade(wire,generation,incarnation),states=[],observation=observeNativePreviewRect(env.surface,facade,state=>states.push(state));env.flush();
 env.overlays=[];env.mutate();env.flush();assert.equal(wire.sent.length,1,'Close observation waits for the current native result');
 const modal=new env.Element('modal');env.overlays=[modal];env.surface.rect={x:110,y:140,width:600,height:400};env.mutate();assert.equal(env.frames.size,1);
 wire.receive(response(wire.sent[0]));await turn();assert.equal(wire.sent.length,2);assert.deepEqual(wire.sent[1].payload.occlusion,{modalActive:true,rects:[]});assert.deepEqual(wire.sent[1].payload.rect,env.surface.rect);assert.equal(env.frames.size,0,'Pending frame was replaced by a current atomic measurement');assert.equal(states.at(-1).status,'visible');
 observation.dispose();facade.dispose();await turn();
}));
await check('More than 32 floating surfaces use a bounded hover-blocking fact rather than dropping an occluder',()=>browser(async env=>{
 env.overlays=Array.from({length:33},(_,index)=>new env.Element('floating',{x:index,y:index,width:15,height:15}));const wire=bridge(),facade=new ProductionPreviewFacade(wire,generation,incarnation),observation=observeNativePreviewRect(env.surface,facade,()=>{});env.flush();
 assert.equal(wire.sent[0].payload.occlusion.rects.length,32);assert.equal(wire.sent[0].payload.occlusion.modalActive,true);observation.dispose();facade.dispose();await turn();
}));
await check('A queued overlay close cannot reuse stale valid Preview geometry when the current slot becomes zero',()=>browser(async env=>{
 env.overlays=[new env.Element('floating')];const wire=bridge(),facade=new ProductionPreviewFacade(wire,generation,incarnation),observation=observeNativePreviewRect(env.surface,facade,()=>{});env.flush();env.overlays=[];env.mutate();env.flush();
 env.surface.rect={...env.surface.rect,width:0};env.mutate();wire.receive(response(wire.sent[0]));await turn();assert.equal(wire.sent.length,2);assert.equal(wire.sent[1].payload.rect.width,0);assert.equal(wire.sent[1].payload.visible,true);assert.deepEqual(wire.sent[1].payload.occlusion,{modalActive:false,rects:[]});observation.dispose();facade.dispose();await turn();
}));
await check('Overlay size, scroll and visibility events sample current facts without a separate Preview rect owner',()=>browser(async env=>{
 const floating=new env.Element('floating');env.overlays=[floating];const wire=bridge(),facade=new ProductionPreviewFacade(wire,generation,incarnation),observation=observeNativePreviewRect(env.surface,facade,()=>{});env.flush();wire.receive(response(wire.sent[0]));await turn();
 floating.rect={x:140,y:160,width:230,height:100};for(let n=0;n<12;n++)env.resizes[0].callback();assert.equal(env.frames.size,1);env.flush();assert.deepEqual(wire.sent[1].payload.occlusion.rects,[floating.rect]);wire.receive(response(wire.sent[1]));await turn();
 env.surface.rect={...env.surface.rect,y:180};floating.rect={...floating.rect,y:190};env.windowListeners.get('scroll')();env.flush();assert.equal(wire.sent[2].payload.rect.y,180);assert.equal(wire.sent[2].payload.occlusion.rects[0].y,190);wire.receive(response(wire.sent[2]));await turn();
 document.hidden=true;env.documentListeners.get('visibilitychange')();assert.equal(wire.sent[3].payload.visible,false);observation.dispose();facade.dispose();await turn();
}));
await check('Page teardown and old overlay cleanup send no restoration, and new generation samples only current modal truth',()=>browser(async env=>{
 const old=new env.Element('floating');env.overlays=[old];const wire=bridge(),facade=new ProductionPreviewFacade(wire,generation,incarnation),states=[],observation=observeNativePreviewRect(env.surface,facade,state=>states.push(state));env.flush();const oldCommand=wire.sent[0],oldMutation=env.mutations[0],oldProjectionCount=states.length;env.overlays=[];env.mutate();
 const currentGeneration=crypto.randomUUID(),current=new env.Element('modal');env.overlays=[current];const nextFacade=new ProductionPreviewFacade(wire,currentGeneration,incarnation),next=observeNativePreviewRect(env.surface,nextFacade,()=>{});env.flush();assert.equal(wire.sent.length,2);assert.equal(wire.sent[1].generation,currentGeneration);assert.equal(wire.sent[1].payload.occlusion.modalActive,true);
 observation.dispose();facade.dispose();await turn();assert.equal(env.frames.size,0);assert(env.resizes[0].disconnected&&oldMutation.disconnected);assert(env.windowListeners.has('scroll'),'Old disposal does not remove the replacement observation');oldMutation.callback([{type:'childList',target:document.body}]);wire.late(response(oldCommand));env.flush();await turn();assert.equal(wire.sent.length,2);assert.equal(states.length,oldProjectionCount);next.dispose();nextFacade.dispose();await turn();assert.equal(wire.listeners.size,0);
}));
const evidence=new URL('../node_modules/.cache/current-contract/',import.meta.url);mkdirSync(evidence,{recursive:true});writeFileSync(new URL('preview-occlusion-checks.json',evidence),JSON.stringify({pass:checks.every(check=>check.pass),checks,proofScope:'Actual frontend geometry/facade with deterministic DOM scheduling; native clipping, HWND visibility and Host fencing proved separately'},null,2));for(const check of checks)console.log((check.pass?'PASS':'FAIL')+' '+check.name+(check.error?'\n'+check.error:''));console.log(checks.filter(check=>check.pass).length+'/'+checks.length);if(checks.some(check=>!check.pass))process.exitCode=1;
