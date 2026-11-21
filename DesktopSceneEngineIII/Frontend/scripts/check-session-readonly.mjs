import assert from 'node:assert/strict';
import {readFileSync,mkdirSync,writeFileSync,existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve,dirname} from 'node:path';
import {ProductionSessionFacade,ProductionSessionController} from '../src/ProductionSessionFacade.ts';
import {ProductionViewFacade} from '../src/ProductionViewFacade.ts';
import {isMainWindowSnapshot,uint64Revision} from '../src/ProductionSessionTypes.ts';
import {runtimeSessionProjection} from '../src/ProductionRuntimeProjection.ts';
import {fixtureRuntimeSample,generation,incarnation,ids,snapshot} from './session-readonly-fixture.mjs';

const checks=[];async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
const turn=async()=>{await Promise.resolve();await Promise.resolve();};
function bridge(){const listeners=new Set(),retired=[];return {listeners,retired,sent:[],adds:0,removes:0,
  postMessage(c){this.sent.push(c);},addEventListener(_,fn){assert(!listeners.has(fn));listeners.add(fn);this.adds++;},removeEventListener(_,fn){assert(listeners.delete(fn));retired.push(fn);this.removes++;},
  receive(v){for(const fn of listeners)fn({data:v});},late(v){for(const fn of retired)fn({data:v});}};}
const result=(c,p=snapshot(),patch={})=>({kind:'result',version:1,requestId:c.requestId,type:c.type,generation:c.generation,sessionIncarnation:c.sessionIncarnation,ok:true,payload:p,...patch});
const event=(patch={})=>({kind:'event',version:1,type:'MainWindowSnapshotInvalidated',generation,sessionIncarnation:incarnation,payload:{},...patch});
function page(b=bridge()){const f=new ProductionSessionFacade(b,generation,incarnation),c=new ProductionSessionController(f);return {b,f,c,dispose:()=>c.dispose()};}

await check('Production Session route exists independently of explicit DEV PrototypeApp',()=>{
 const main=readFileSync(new URL('../src/main.tsx',import.meta.url),'utf8');assert.match(main,/mode === 'production' \? view === 'background-music' \? <ProductionBackgroundMusicShell \/> : view === 'session' \? <ProductionSessionShell/);assert.match(main,/import\.meta\.env\.DEV \? lazy/);
});
await check('Production runtime import graph contains no prototype authority or clocks',()=>{
 const root=fileURLToPath(new URL('../src/',import.meta.url)),visited=new Set();
 function inspect(path){if(visited.has(path))return;visited.add(path);const source=readFileSync(path,'utf8');
  assert(!/(?:sessionFixtures|sessionDocumentMock|sessionTimelinePreviewMock|useSessionPrototype|sessionEntities|sessionPlayback|sessionInspectorModel|sessionPresentationModel|sessionDisplayModel|sessionViewModel)\.(?:ts|tsx)$/.test(path),path);
  assert(!/setInterval\s*\(|localStorage|sessionStorage/.test(source),path);
  for(const match of source.matchAll(/(?:^|\n)import(?!\s+type\b)[^;]*?from\s*['"](\.[^'"]+)['"]/g)){
    const base=resolve(dirname(path),match[1]);const found=[base,base+'.ts',base+'.tsx'].find(existsSync);if(found&&!found.endsWith('.css'))inspect(found);
  }
 }
 inspect(resolve(root,'ProductionSessionShell.tsx'));assert(visited.size>12);assert(![...visited].some(p=>p.includes('Prototype')));
});
await check('Bridge absence and malformed page context fail explicitly',()=>{
 assert.throws(()=>new ProductionSessionFacade(undefined,generation,incarnation),/native bridge is unavailable/);
 for(const id of ['', 'not-uuid','00000000-0000-0000-0000-000000000000'])assert.throws(()=>new ProductionSessionFacade(bridge(),id,incarnation),/page context is invalid/);
});
await check('Coherent snapshot preserves uint64 revisions, stable IDs, order and selected≠active',()=>{
 const s=snapshot();assert(isMainWindowSnapshot(s,incarnation));const p=runtimeSessionProjection(s,fixtureRuntimeSample(s));assert.equal(p.selectedPlaylistId,ids.b);assert.equal(p.activePlaylistId,ids.a);assert.equal(s.document.authoredRevision,'18446744073709551614');
 s.project.playlists.reverse();assert(isMainWindowSnapshot(s,incarnation));assert.equal(runtimeSessionProjection(s,fixtureRuntimeSample(s)).selectedPlaylistId,ids.b);assert.equal(s.project.playlists[0].id,ids.b);
 for(const bad of [Number.MAX_SAFE_INTEGER,'018','18446744073709551616','-1','1.0'])assert.equal(uint64Revision(bad),false);
});
await check('Dirty is exact revision inequality; cleanRevision is never treated as identity',()=>{
 for(const dirty of [true,false]){const s=snapshot();s.document.cleanRevision=dirty?'18446744073709551613':s.document.authoredRevision;s.document.dirty=dirty;assert(isMainWindowSnapshot(s,incarnation));s.document.dirty=!dirty;assert(!isMainWindowSnapshot(s,incarnation));}
});
await check('Configured/actual rate, Playlist Master, embedded gain and Output Mute remain distinct',()=>{
 const s=snapshot(),p=runtimeSessionProjection(s,fixtureRuntimeSample(s));assert.equal(p.project.playlists[1].items[0].rate,1.25);assert.equal(p.runtime.rate,1.1);assert.equal(p.project.playlists[1].masterVolume,.4);assert.equal(p.outputMuted,true);assert.equal(p.project.playlists[1].items[0].mediaGain,.5);
 assert.equal(Object.hasOwn(p,'sequence'),false);assert.equal(Object.hasOwn(p,'files'),false);assert.equal(Object.hasOwn(p,'faults'),false);
});
await check('Schema rejects duplicate IDs, dangling selection, cross-owner, wrong incarnation and non-finite facts',()=>{
 for(const change of [s=>s.project.playlists.push(structuredClone(s.project.playlists[0])),s=>s.selection.itemId=ids.image,s=>s.active.itemId=ids.video,s=>s.session.incarnation=generation,s=>s.project.displayOwner='other',s=>s.project.playlists[0].items[0].rate=Infinity,s=>s.project.playlists[0].mock=true,s=>delete s.documentOperation,s=>delete s.mediaImport]){const s=snapshot();change(s);assert(!isMainWindowSnapshot(s,incarnation));}
});
await check('Main snapshot and its facade reject retired runtime and mute fields',async()=>{
 const current=snapshot();
 const runtime={time:3,duration:12,paused:true,rate:1.1,volume:.7,sceneIndex:0,itemIds:[ids.image],mediaKind:'image'};
 for(const retired of [{runtime},{outputMuted:true},{runtime,outputMuted:true}]){
  const payload={...current,...retired};
  assert.equal(isMainWindowSnapshot(payload,incarnation),false,'Retired runtime data is not part of the closed Main snapshot');
  const p=page(),request=p.c.refresh();p.b.receive(result(p.b.sent[0],payload));await request;
  assert.equal(p.c.getSnapshot().code,'SnapshotValidationFailure');p.dispose();
 }
});
await check('Read request is the sole command with an empty payload and exact page/Session context',async()=>{
 const p=page();const request=p.f.read();assert.deepEqual(p.b.sent.map(({requestId,...c})=>c),[{kind:'command',version:1,type:'GetMainWindowSnapshot',generation,sessionIncarnation:incarnation,payload:{}}]);p.b.receive(result(p.b.sent[0]));assert.deepEqual(await request,snapshot());p.dispose();
});
await check('Old-generation and other-Session results/events cannot enter the current page',async()=>{
 const p=page();const request=p.c.refresh();const command=p.b.sent[0];p.b.receive(result(command,snapshot(),{generation:crypto.randomUUID()}));p.b.receive(result(command,snapshot(),{sessionIncarnation:crypto.randomUUID()}));p.b.receive(event({generation:crypto.randomUUID()}));assert.equal(p.c.getSnapshot().status,'loading');assert.equal(p.b.sent.length,1);p.b.receive(result(command));await request;assert.equal(p.c.getSnapshot().status,'ready');p.dispose();
});
await check('Correlated native failure and malformed snapshot produce explicit failure, never an empty editor',async()=>{
 for(const malformed of [false,true]){const p=page(),request=p.c.refresh(),c=p.b.sent[0];if(malformed)p.b.receive(result(c,{}));else{const {payload,...failure}=result(c);p.b.receive({...failure,ok:false,error:'snapshot-unavailable'});}await request;assert.equal(p.c.getSnapshot().status,'failure');assert.equal(p.c.getSnapshot().code,malformed?'SnapshotValidationFailure':'SnapshotUnavailable');p.dispose();}
});
await check('Malformed result envelope fails immediately and cannot be mistaken for success',async()=>{
 const p=page(),request=p.c.refresh();p.b.receive(result(p.b.sent[0],snapshot(),{version:2}));await request;assert.equal(p.c.getSnapshot().code,'SnapshotValidationFailure');p.dispose();
});
await check('Burst invalidations coalesce to one subsequent coherent read without command replay',async()=>{
 const p=page(),request=p.c.refresh();for(let i=0;i<25;i++)p.b.receive(event());assert.equal(p.b.sent.length,1);p.b.receive(result(p.b.sent[0]));await turn();assert.equal(p.b.sent.length,2);const fresh=snapshot();fresh.project.playlists[0].alias='Fresh native';p.b.receive(result(p.b.sent[1],fresh));await request;assert.equal(p.c.getSnapshot().snapshot.project.playlists[0].alias,'Fresh native');assert(p.b.sent.every(c=>c.type==='GetMainWindowSnapshot'));p.dispose();
});
await check('Fifty page disposal/recreation cycles release handlers and reject all late deliveries',async()=>{
 const b=bridge();for(let i=0;i<50;i++){const p=page(b);assert.equal(b.listeners.size,2,'snapshot and shared audio-effects listeners belong to this page');let notifications=0;p.c.subscribe(()=>notifications++);const read=p.c.refresh(),command=b.sent.at(-1);p.dispose();await read;assert.equal(b.listeners.size,0);b.late(result(command));b.late(event());assert.equal(notifications,0);assert.equal(p.c.getSnapshot().status,'loading');}assert.equal(b.adds,100);assert.equal(b.removes,100);
});
await check('Fresh pages reread changed native truth and perform no lifecycle/domain replay',async()=>{
 const b=bridge();for(let n=0;n<2;n++){const p=page(b),request=p.c.refresh(),s=snapshot();s.document.authoredRevision=String(n);s.document.cleanRevision=String(n);s.document.dirty=false;p.b.receive(result(b.sent.at(-1),s));await request;assert.equal(p.c.getSnapshot().snapshot.document.authoredRevision,String(n));p.dispose();}assert.deepEqual(b.sent.map(c=>c.type),['GetMainWindowSnapshot','GetMainWindowSnapshot']);
});
await check('View and deployment requests match only their page, Session, request and typed outcome',async()=>{
 const b=bridge(),view=new ProductionViewFacade(b,generation,incarnation);
 for(const [type,payload,status] of [
  ['SetCanvasBoundaryHighlight',{highlighted:true},'applied'],['SetDesktopBackground',{mode:'reveal'},'already-satisfied'],
  ['DeployDesktop',{},'completed'],['UndeployDesktop',{},'failed'],
 ]){
  const resultPromise=view.send({type,payload});let settled=false;void resultPromise.then(()=>{settled=true;});
  const {requestId,...command}=b.sent.at(-1);
  assert.deepEqual(command,{kind:'command',version:1,type,generation,sessionIncarnation:incarnation,payload});
  const answer={kind:'result',version:1,requestId,type,generation,sessionIncarnation:incarnation,ok:true,payload:{status}};
  b.receive({...answer,generation:crypto.randomUUID()});b.receive({...answer,sessionIncarnation:crypto.randomUUID()});b.receive({...answer,requestId:crypto.randomUUID()});
  await turn();assert.equal(settled,false);
  b.receive(answer);assert.equal(await resultPromise,status);
 }
 const invalid=view.send({type:'DeployDesktop',payload:{}});
 b.receive(result(b.sent.at(-1),{status:'applied'}));await assert.rejects(invalid,/result is invalid/);
 const waiting=view.send({type:'UndeployDesktop',payload:{}});const retired=b.sent.at(-1);
 view.dispose();await assert.rejects(waiting,/page has retired/);b.late(result(retired,{status:'completed'}));
 assert.equal(b.listeners.size,0);
});
const dir=new URL('../node_modules/.cache/current-contract/',import.meta.url);mkdirSync(dir,{recursive:true});writeFileSync(new URL('session-snapshot.json',dir),JSON.stringify({checks,pass:checks.every(c=>c.pass),pageDisposalCycles:50},null,2));for(const c of checks)console.log((c.pass?'PASS':'FAIL')+' '+c.name+(c.error?'\n'+c.error:''));console.log(checks.filter(c=>c.pass).length+'/'+checks.length);if(checks.some(c=>!c.pass))process.exitCode=1;
