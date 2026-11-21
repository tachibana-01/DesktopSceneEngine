import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { launcherRenderModule } from './launcher-render-module.mjs';
import { ProductionLauncherFacade, readLauncherSessionsSnapshot, readLauncherSnapshot } from '../src/ProductionLauncherFacade.ts';
import { ProductionLauncherController } from '../src/ProductionLauncherController.ts';
import { launcherDisplayAction } from '../src/launcherSessionPresentation.ts';
import { launcherCopy, launcherSessionActionMessage } from '../src/launcherLanguage.ts';

// Meaningful RED captured before implementation: the real facade Create entry
// was undefined. No test creates local domain state to simulate production.
assert.equal(typeof ProductionLauncherFacade.prototype.createSession, 'function');
assert.equal(typeof ProductionLauncherFacade.prototype.openSession, 'function');
const generation='24d93f2f-13d5-454b-b7bb-aa3402aa2311';
const epoch='dba10904-5b14-43f0-995b-17bc99181d43', nextEpoch='d7951708-aa40-497e-9160-37a96e1063e3';
const incarnation='ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2', replacement='b902bc98-e64a-43e0-ae66-16e257e5cd5b';
const operationId='6812eb13-67c4-4fc1-8bae-0216f2472645';
const rows=['A','B','Weak'].map((name,index)=>({id:'opaque exact target 日本語 '+name,identityKind:index===2?'SourceDeviceName':'TargetDevicePath',persistentOwnerCapable:index!==2,
 friendlyName:'Panel '+name,sourceDeviceName:'QA_SOURCE_'+name,desktopX:index*1920,desktopY:0,pixelWidth:1920,pixelHeight:1080,primary:index===0,displayNumber:index+1}));
const catalog=(displays=rows,status='Success')=>({status,displays});
const role=(patch={})=>({boundDisplayId:rows[0].id,incarnation,controlConnected:true,...patch});
const operation=(patch={})=>({displayId:rows[0].id,operationId,state:'Pending',...patch});
const sessions=(revision=0,roles=[],operations=[],owner=epoch)=>({coordinatorEpoch:owner,sessionRevision:revision,registeredSessionsScope:'verified-registered-only',registeredSessions:roles,createOperations:operations});
const snapshot=(projection=sessions())=>({...projection,displayEnumeration:catalog(),language:{readStatus:'Success',committed:'en',effective:'en',revision:0},host:{ready:true,launcherRoleReady:true}});
const create=(status='Accepted',patch={})=>({coordinatorEpoch:epoch,displayId:rows[0].id,status,...(['Accepted','AlreadyPending','LaunchFailed'].includes(status)?{operationId}:{}),...(status==='AlreadyRegistered'?{incarnation}:{}),...patch});
const open=(status='ActivationRequested',patch={})=>({coordinatorEpoch:epoch,incarnation,status,...patch});
const checks=[];
async function check(group,name,run){try{await run();checks.push({group,name,pass:true});}catch(error){checks.push({group,name,pass:false,error:String(error)});}}
function fixture(){
 const listeners=new Set();const bridge={sent:[],postMessage(command){this.sent.push(command);},addEventListener(_t,fn){listeners.add(fn);},removeEventListener(_t,fn){listeners.delete(fn);},receive(data){for(const fn of listeners)fn({data});}};
 const facade=new ProductionLauncherFacade(bridge,generation),controller=new ProductionLauncherController(facade);
 const reply=(payload,command=bridge.sent.at(-1),patch={})=>bridge.receive({kind:'result',version:1,type:command.type,requestId:command.requestId,generation,ok:true,payload,...patch});
 const emit=(payload,patch={})=>bridge.receive({kind:'event',version:1,type:'LauncherSessionsChanged',generation,payload,...patch});
 return {bridge,facade,controller,reply,emit,async boot(payload=snapshot()){const p=controller.bootstrap();reply(payload);await p;},dispose(){controller.dispose();}};
}
await check('contract','Shared bootstrap/live strict operation facts preserve exact opaque ID and Registered incarnation',()=>{
 for(const state of ['Pending','Registered','LaunchFailed','ExitedBeforeRegistration','Uncertain']){
  const facts=sessions(1,[],[operation({state,...(state==='Registered'?{incarnation}:{})})]);
  assert.deepEqual(readLauncherSessionsSnapshot(facts),facts);assert.deepEqual(readLauncherSnapshot(snapshot(facts)),snapshot(facts));
 }
});
await check('contract','Missing/unknown/overstated operation facts reject whole snapshot',()=>{
 for(const mutate of [v=>delete v.createOperations,v=>v.createOperations=null,v=>v.createOperations=Array(65).fill(operation()),
  v=>v.createOperations[0].state='ACTIVE',v=>v.createOperations[0].operationId='',v=>v.createOperations[0].displayId='',
  v=>v.createOperations[0].pid=8,v=>v.createOperations[0].hwnd=9,v=>v.createOperations[0].incarnation=incarnation,
  v=>{v.createOperations[0].state='Registered';},v=>{v.createOperations[0].state='Registered';v.createOperations[0].incarnation='pid:12';},
  v=>v.createOperations.push(operation({operationId:replacement})),v=>v.createOperations.push(operation({displayId:rows[1].id,operationId:'{'+operationId.toUpperCase()+'}'}))]){
  const facts=sessions(0,[],[operation()]);mutate(facts);assert.throws(()=>readLauncherSessionsSnapshot(facts));assert.throws(()=>readLauncherSnapshot(snapshot(facts)));
 }
});
await check('contract','Create sends only epoch+exact Display and each explicit native status remains distinct',async()=>{
 const f=fixture();try{for(const status of ['Accepted','AlreadyPending','AlreadyRegistered','NotOwnerCapable','Unresolved','CatalogUnavailable','CapacityExceeded','LaunchFailed','StaleCoordinator','Unavailable']){
  const p=f.facade.createSession(epoch,rows[0].id),command=f.bridge.sent.at(-1);assert.equal(command.type,'CreateSession');assert.deepEqual(command.payload,{coordinatorEpoch:epoch,displayId:rows[0].id});
  const result=create(status,status==='StaleCoordinator'?{coordinatorEpoch:nextEpoch}:{});f.reply(result);assert.deepEqual(await p,result);
 }}finally{f.dispose();}
});
await check('contract','Open sends exact incarnation+epoch and native activation is a request, not a foreground guarantee',async()=>{
 const f=fixture();try{for(const status of ['ActivationRequested','RoleGone','ControlUnavailable','Closing','StaleCoordinator','ApiFailure','Unavailable']){
  const p=f.facade.openSession(epoch,incarnation),command=f.bridge.sent.at(-1);assert.equal(command.type,'OpenSession');assert.deepEqual(command.payload,{coordinatorEpoch:epoch,incarnation});
  const result=open(status,status==='StaleCoordinator'?{coordinatorEpoch:nextEpoch}:{});f.reply(result);assert.deepEqual(await p,result);
 }}finally{f.dispose();}
});
await check('contract','Malformed Create result cannot masquerade as capability outcome',async()=>{
 for(const payload of [create('Accepted',{operationId:undefined}),create('Accepted',{incarnation}),create('Accepted',{displayId:rows[1].id}),
  create('Accepted',{coordinatorEpoch:nextEpoch}),create('AlreadyRegistered',{operationId}),create('AlreadyRegistered',{incarnation:undefined}),
  create('Unavailable',{operationId}),create('Unavailable',{extra:true}),create('READY'),create('Accepted',{operationId:'pid:123'})]){
  const f=fixture();try{const p=f.facade.createSession(epoch,rows[0].id);const rejected=assert.rejects(p,/invalid/);f.reply(payload);await rejected;}finally{f.dispose();}
 }
});
await check('contract','Malformed Open result rejects wrong incarnation/epoch, extra data and fake success',async()=>{
 for(const payload of [open('ActivationRequested',{incarnation:replacement}),open('ActivationRequested',{coordinatorEpoch:nextEpoch}),open('Shown'),open('ActivationRequested',{hwnd:12}),open('ActivationRequested',{incarnation:'pid:12'})]){
  const f=fixture();try{const p=f.facade.openSession(epoch,incarnation),rejected=assert.rejects(p,/invalid/);f.reply(payload);await rejected;}finally{f.dispose();}
 }
});
await check('contract','UUID-normalized normal result is accepted while opaque Display identity stays exact',async()=>{
 const f=fixture();try{const p=f.facade.openSession(epoch,incarnation);f.reply(open('ActivationRequested',{coordinatorEpoch:'{'+epoch.toUpperCase()+'}',incarnation:'{'+incarnation.toUpperCase()+'}'}));assert.equal((await p).status,'ActivationRequested');}finally{f.dispose();}
});
await check('contract','Old page and unrelated request cannot settle a current Create; retire rejects pending Open',async()=>{
 const f=fixture();try{let settled=false;const p=f.facade.createSession(epoch,rows[0].id).then(v=>{settled=true;return v;});const c=f.bridge.sent.at(-1);
  f.reply(create(),c,{generation:nextEpoch});f.reply(create(),c,{requestId:'retired_request'});await Promise.resolve();assert.equal(settled,false);f.reply(create(),c);await p;
  const pending=f.facade.openSession(epoch,incarnation),rejected=assert.rejects(pending,/retired/);f.dispose();await rejected;
 }finally{f.dispose();}
});
await check('contract','Invalid command identities reject without sending a command',async()=>{
 const f=fixture();try{for(const p of [f.facade.createSession('bad',rows[0].id),f.facade.createSession(epoch,''),f.facade.openSession(epoch,'pid:1'),f.facade.openSession('bad',incarnation)])await assert.rejects(p);assert.equal(f.bridge.sent.length,0);}finally{f.dispose();}
});
await check('mapping','Exact owner-capable absence exposes Create without asserting slot availability',()=>{
 const action=launcherDisplayAction(sessions(),catalog(),rows[0].id);assert.equal(action.kind,'Create');assert.equal(action.enabled,true);assert.equal(action.status,'noRegisteredSession');
});
await check('mapping','Weak, stale exact Display, failed catalog and unavailable catalog cannot Create',()=>{
 for(const [cat,id] of [[catalog(),rows[2].id],[catalog(), 'not this display'],[catalog([],'ApiFailure'),rows[0].id],[null,rows[0].id]]) assert.equal(launcherDisplayAction(sessions(),cat,id).enabled,false);
});
await check('mapping','Only actual native Pending maps STARTING and disables Create, even if Display disappears',()=>{
 for(const cat of [catalog(),catalog([]),catalog([],'ApiFailure')]){const a=launcherDisplayAction(sessions(1,[],[operation()]),cat,rows[0].id);assert.equal(a.status,'STARTING');assert.equal(a.enabled,false);}
});
await check('mapping','All terminal receipts alone remain no registered Session, never ACTIVE or STARTING',()=>{
 for(const state of ['Registered','LaunchFailed','ExitedBeforeRegistration','Uncertain']){const a=launcherDisplayAction(sessions(1,[],[operation({state,...(state==='Registered'?{incarnation}:{})})]),catalog(),rows[0].id);assert.equal(a.status,'noRegisteredSession');assert.equal(a.enabled,true);}
});
await check('mapping','Real registered incarnation outranks Pending and terminal operation; control+Close governs Open',()=>{
 for(const state of ['Pending','Registered','Uncertain']){const a=launcherDisplayAction(sessions(1,[role()],[operation({state,...(state==='Registered'?{incarnation}:{})})]),catalog(),rows[0].id);assert.equal(a.status,'ACTIVE');assert.equal(a.kind,'Open');assert.equal(a.enabled,true);}
 for(const result of ['Received','Pending','CloseAccepted']){const a=launcherDisplayAction(sessions(1,[role({closeOperation:{generation:epoch,request:'1',result}})]),catalog(),rows[0].id);assert.equal(a.status,'CLOSING');assert.equal(a.enabled,false);}
 assert.equal(launcherDisplayAction(sessions(1,[role({controlConnected:false})]),catalog(),rows[0].id).enabled,false);
});
await check('mapping','Unresolved or unknown Display does not forbid valid registered Session management Open',()=>{
 for(const cat of [catalog([]),catalog([],'ApiFailure'),null]){const a=launcherDisplayAction(sessions(1,[role()]),cat,rows[0].id);assert.equal(a.kind,'Open');assert.equal(a.enabled,true);}
});
await check('controller','Button intent and Accepted response never synthesize STARTING or insert a Session',async()=>{
 const f=fixture();try{await f.boot();const p=f.controller.createSession(rows[0].id);assert.deepEqual(f.controller.getState().createRequestDisplayIds,[rows[0].id]);assert.equal(launcherDisplayAction(f.controller.getState().bootstrap,catalog(),rows[0].id).status,'noRegisteredSession');
 f.reply(create());await p;assert.equal(f.controller.getState().bootstrap.registeredSessions.length,0);assert.equal(f.controller.getState().bootstrap.createOperations.length,0);assert.equal(f.controller.getState().sessionActionFeedback.result.status,'Accepted');}finally{f.dispose();}
});
await check('controller','Synchronous duplicate submit guard sends one command for same Display',async()=>{
 const f=fixture();try{await f.boot();const a=f.controller.createSession(rows[0].id),b=f.controller.createSession(rows[0].id);assert.equal(f.bridge.sent.filter(c=>c.type==='CreateSession').length,1);f.reply(create());await Promise.all([a,b]);}finally{f.dispose();}
});
await check('controller','Exact card target captured across selection changes; distinct Display transport requests independent',async()=>{
 const f=fixture();try{await f.boot();const a=f.controller.createSession(rows[0].id),ca=f.bridge.sent.at(-1);f.controller.select(rows[1].id);const b=f.controller.createSession(rows[1].id),cb=f.bridge.sent.at(-1);
 assert.deepEqual(ca.payload,{coordinatorEpoch:epoch,displayId:rows[0].id});assert.deepEqual(cb.payload,{coordinatorEpoch:epoch,displayId:rows[1].id});
 f.reply(create(),ca);await a;assert.deepEqual(f.controller.getState().createRequestDisplayIds,[rows[1].id]);f.reply(create('Accepted',{displayId:rows[1].id,operationId:replacement}),cb);await b;
 assert.equal(f.controller.getState().selectedDisplayId,rows[1].id);assert.equal(f.controller.getState().bootstrap.registeredSessions.length,0);}finally{f.dispose();}
});
await check('controller','Native Pending and actual Join push are the only STARTING/ACTIVE transitions',async()=>{
 const f=fixture();try{await f.boot();const p=f.controller.createSession(rows[0].id);f.emit(sessions(1,[],[operation()]));assert.equal(launcherDisplayAction(f.controller.getState().bootstrap,catalog(),rows[0].id).status,'STARTING');
 f.emit(sessions(2,[role()],[operation({state:'Registered',incarnation})]));f.reply(create());await p;assert.equal(launcherDisplayAction(f.controller.getState().bootstrap,catalog(),rows[0].id).status,'ACTIVE');assert.equal(f.controller.getState().bootstrap.registeredSessions[0].incarnation,incarnation);
 f.emit(sessions(3,[],[operation({state:'Registered',incarnation})]));assert.equal(launcherDisplayAction(f.controller.getState().bootstrap,catalog(),rows[0].id).status,'noRegisteredSession');}finally{f.dispose();}
});
await check('controller','AlreadyRegistered result alone cannot insert returned incarnation',async()=>{
 const f=fixture();try{await f.boot();const p=f.controller.createSession(rows[0].id);f.reply(create('AlreadyRegistered'));await p;assert.equal(f.controller.getState().bootstrap.registeredSessions.length,0);assert.equal(f.controller.getState().bootstrap.createOperations.length,0);}finally{f.dispose();}
});
await check('controller','Native launch failure and transport failure remain distinct without fake state',async()=>{
 const f=fixture();try{await f.boot();let p=f.controller.createSession(rows[0].id);f.reply(create('LaunchFailed'));await p;assert.equal(f.controller.getState().sessionActionFeedback.result.status,'LaunchFailed');
 p=f.controller.createSession(rows[0].id);f.reply(undefined,f.bridge.sent.at(-1),{ok:false,error:'transport unavailable'});await p;assert.equal(f.controller.getState().sessionActionFeedback.result.status,'TransportFailure');assert.equal(f.controller.getState().bootstrap.registeredSessions.length,0);assert.equal(f.controller.getState().bootstrap.createOperations.length,0);}finally{f.dispose();}
});
await check('controller','Reload/recreation restores Pending from bootstrap without replaying Create',async()=>{
 for(let n=0;n<2;n++){const f=fixture();try{await f.boot(snapshot(sessions(4,[],[operation()])));await f.controller.createSession(rows[0].id);assert.equal(launcherDisplayAction(f.controller.getState().bootstrap,catalog(),rows[0].id).status,'STARTING');assert.deepEqual(f.bridge.sent.map(c=>c.type),['GetLauncherBootstrapSnapshot']);}finally{f.dispose();}}
});
await check('controller','Weak/unresolved/failed catalog and native Pending refuse frontend Create dispatch',async()=>{
 for(const value of [snapshot(sessions(1,[],[operation()])),{...snapshot(),displayEnumeration:catalog([],'ApiFailure')},{...snapshot(),displayEnumeration:catalog([rows[1]])}]){
 const f=fixture();try{await f.boot(value);await f.controller.createSession(rows[0].id);await f.controller.createSession(rows[2].id);assert.equal(f.bridge.sent.filter(c=>c.type==='CreateSession').length,0);}finally{f.dispose();}}
});
await check('controller','Refresh failure/disappearance never mutates Pending operations or registered facts',async()=>{
 const f=fixture();try{await f.boot(snapshot(sessions(1,[],[operation()])));const before=f.controller.getState().bootstrap;const p=f.controller.refresh();f.reply(catalog([]));await p;assert.deepEqual(f.controller.getState().bootstrap,before);assert.equal(launcherDisplayAction(before,f.controller.getState().catalog,rows[0].id).status,'STARTING');}finally{f.dispose();}
});
await check('controller','Open sends displayed incarnation and duplicate transport submission coalesces',async()=>{
 const f=fixture();try{await f.boot(snapshot(sessions(1,[role()])));const a=f.controller.openSession(rows[0].id,incarnation),b=f.controller.openSession(rows[0].id,incarnation);assert.equal(f.bridge.sent.filter(c=>c.type==='OpenSession').length,1);assert.deepEqual(f.bridge.sent.at(-1).payload,{coordinatorEpoch:epoch,incarnation});f.reply(open());await Promise.all([a,b]);assert.equal(f.controller.getState().bootstrap.registeredSessions[0].incarnation,incarnation);}finally{f.dispose();}
});
await check('controller','Stale Open result never creates or retargets same-Display replacement',async()=>{
 const f=fixture();try{await f.boot(snapshot(sessions(1,[role()])));const p=f.controller.openSession(rows[0].id,incarnation);f.emit(sessions(2,[role({incarnation:replacement})]));f.reply(open('RoleGone'));await p;
 await f.controller.openSession(rows[0].id,incarnation);assert.equal(f.bridge.sent.filter(c=>c.type==='OpenSession').length,1);assert.equal(f.bridge.sent.filter(c=>c.type==='CreateSession').length,0);assert.equal(f.controller.getState().bootstrap.registeredSessions[0].incarnation,replacement);}finally{f.dispose();}
});
await check('controller','Open control loss or CLOSING cannot cancel/rewrite native operation or dispatch command',async()=>{
 for(const r of [role({controlConnected:false}),role({closeOperation:{generation:epoch,request:'4',result:'Pending'}})]){
 const f=fixture();try{await f.boot(snapshot(sessions(1,[r])));await f.controller.openSession(rows[0].id,incarnation);assert.deepEqual(f.bridge.sent.map(c=>c.type),['GetLauncherBootstrapSnapshot']);}finally{f.dispose();}}
});
await check('controller','StaleCoordinator command result invalidates owner view and never replays command',async()=>{
 for(const kind of ['Create','Open']){const f=fixture();try{await f.boot(snapshot(sessions(1,kind==='Open'?[role()]:[])));const p=kind==='Create'?f.controller.createSession(rows[0].id):f.controller.openSession(rows[0].id,incarnation);f.reply(kind==='Create'?create('StaleCoordinator',{coordinatorEpoch:nextEpoch}):open('StaleCoordinator',{coordinatorEpoch:nextEpoch}));await p;
 assert.equal(f.controller.getState().bootstrap,null);assert.equal(f.controller.getState().catalog,null);assert.deepEqual(f.controller.getState().createRequestDisplayIds,[]);assert.deepEqual(f.controller.getState().openRequestIncarnations,[]);
 await f.boot(snapshot(sessions(0,[],[],nextEpoch)));assert.equal(f.controller.getState().bootstrap.coordinatorEpoch,nextEpoch);assert.equal(f.bridge.sent.filter(c=>c.type===kind+'Session').length,1);}finally{f.dispose();}}
});
await check('controller','New Coordinator event fences delayed old Create/Open results',async()=>{
 for(const kind of ['Create','Open']){const f=fixture();try{await f.boot(snapshot(sessions(1,kind==='Open'?[role()]:[])));const p=kind==='Create'?f.controller.createSession(rows[0].id):f.controller.openSession(rows[0].id,incarnation),c=f.bridge.sent.at(-1);
 f.emit(sessions(0,[],[],nextEpoch));f.reply(kind==='Create'?create():open(),c);await p;assert.equal(f.controller.getState().bootstrap,null);assert.equal(f.controller.getState().sessionActionFeedback,null);}finally{f.dispose();}}
});
await check('controller','Dispose fences late result; fresh page only requests current bootstrap',async()=>{
 const f=fixture();await f.boot();const p=f.controller.createSession(rows[0].id),c=f.bridge.sent.at(-1);f.dispose();f.reply(create(),c);await p;const fresh=fixture();try{await fresh.boot(snapshot(sessions(2,[],[operation()])));assert.deepEqual(fresh.bridge.sent.map(c=>c.type),['GetLauncherBootstrapSnapshot']);}finally{fresh.dispose();}
});
const cache=new URL('../node_modules/.cache/launcher-create-open-a6/',import.meta.url);mkdirSync(cache,{recursive:true});
const {ProductionLauncherView}=await import(launcherRenderModule(new URL('../src/ProductionLauncherShell.tsx',import.meta.url),cache).href+'?run='+Date.now());
function render(projection=sessions(),cat=catalog(),props={},language='en') {const value=snapshot(projection);value.language={...value.language,committed:language,effective:language};return renderToStaticMarkup(React.createElement(ProductionLauncherView,{snapshot:value,catalog:cat,selectedId:cat?.displays[0]?.id??null,onSelect(){},onRefresh(){},...props}));}
await check('presentation','Owner Create and exact-incarnation Open retain approved primary action path',()=>{
 const html=render();assert.match(html,/class="editorial-action" data-session-action="Create"/);assert(!/data-session-action="Create"[^>]*disabled=/.test(html));assert(html.includes(launcherCopy('en').sessionUnknown));
 const active=render(sessions(1,[role()]));assert(active.includes('data-session-action="Open"'));assert(active.includes('data-action-incarnation="'+incarnation+'"'));assert(!active.includes('data-session-action="Create"'));
});
await check('presentation','Native Pending creates visible STARTING; request pending alone keeps neutral registration',()=>{
 const native=render(sessions(1,[],[operation()]));assert(native.includes('data-session-status="STARTING"'));assert.match(native,/data-session-action="Create"[^>]*disabled=""/);
 const transport=render(sessions(),catalog(),{createRequestDisplayIds:[rows[0].id]});assert(!transport.includes('STARTING'));assert(transport.includes('Opening a workspace…'));assert.match(transport,/data-session-action="Create"[^>]*disabled=""/);
});
await check('presentation','Open survives unresolved Display, remains disabled for ControlUnavailable/CLOSING',()=>{
 const html=render(sessions(1,[role()]),catalog([rows[1]]));assert(html.includes('data-session-status="DISCONNECTED"'));assert.match(html,/data-session-action="Open"[^>]*data-action-incarnation="ae0581a3/);assert(!/data-session-action="Open"[^>]*disabled=/.test(html));
 for(const r of [role({controlConnected:false}),role({closeOperation:{generation:epoch,request:'1',result:'Pending'}})])assert.match(render(sessions(1,[r])),/data-session-action="Open"[^>]*disabled=""/);
});
await check('presentation','Terminal receipts show localized truthful failure without fake ACTIVE',()=>{
 for(const language of ['en','zh_CN'])for(const [state,key] of [['LaunchFailed','createFailed'],['ExitedBeforeRegistration','createExited'],['Uncertain','createUncertain']]){const html=render(sessions(1,[],[operation({state})]),catalog(),{},language);assert(html.includes(launcherCopy(language)[key]));assert(!html.includes('data-session-status="ACTIVE"'));assert(!html.includes('data-session-status="STARTING"'));}
});
await check('presentation','Stale Display and stale incarnation command feedback never describes new selection/replacement',()=>{
 const feedback={kind:'Create',displayId:rows[1].id,result:create('LaunchFailed',{displayId:rows[1].id})};assert(!render(sessions(),catalog(),{sessionActionFeedback:feedback}).includes(launcherCopy('en').createFailed));
 const staleOpen={kind:'Open',displayId:rows[0].id,incarnation,result:open('RoleGone')};assert(!render(sessions(2,[role({incarnation:replacement})]),catalog(),{sessionActionFeedback:staleOpen}).includes(launcherCopy('en').sessionUnavailable));
});
await check('presentation','All Create/Open failure copy is localized; successful activation has no long success toast',()=>{
 for(const language of ['en','zh_CN']){for(const status of ['NotOwnerCapable','Unresolved','CatalogUnavailable','CapacityExceeded','LaunchFailed','Unavailable','TransportFailure'])assert(launcherSessionActionMessage({kind:'Create',displayId:rows[0].id,result:{status}},language));for(const status of ['RoleGone','ControlUnavailable','Closing','ApiFailure','Unavailable','TransportFailure'])assert(launcherSessionActionMessage({kind:'Open',displayId:rows[0].id,incarnation,result:{status}},language));assert.equal(launcherSessionActionMessage({kind:'Open',displayId:rows[0].id,incarnation,result:open()},language),null);}
});
await check('presentation','Production controller/mapping/shell has no lifecycle timer, mock authority or Close command',()=>{
 for(const name of ['ProductionLauncherController.ts','ProductionLauncherShell.tsx','launcherSessionPresentation.ts']){const source=readFileSync(new URL('../src/'+name,import.meta.url),'utf8');for(const forbidden of ['setTimeout(','setInterval(','1700','useLauncherPrototype','CloseSession','ForceClose','RequestClose'])assert(!source.includes(forbidden),name+forbidden);}
 const facade=readFileSync(new URL('../src/ProductionLauncherFacade.ts',import.meta.url),'utf8');assert(!facade.includes("this.request('Close"));assert(!render().includes('AVAILABLE'));
});
const groups=Object.fromEntries(['contract','mapping','controller','presentation'].map(group=>{const rows=checks.filter(c=>c.group===group);return [group,{passed:rows.filter(c=>c.pass).length,total:rows.length}];}));
const output={pass:checks.every(c=>c.pass),red:'Before implementation createSession was undefined (runtime assertion failed).',groups,checks};writeFileSync(new URL('focused-checks.json',cache),JSON.stringify(output,null,2));console.log(JSON.stringify({pass:output.pass,groups,total:checks.length,failed:checks.filter(c=>!c.pass)},null,2));assert(output.pass,'A6 focused checks must pass');
