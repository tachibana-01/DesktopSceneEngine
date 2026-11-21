import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync,readFileSync,existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve,dirname} from 'node:path';
import {ProductionDocumentFacade} from '../src/ProductionDocumentFacade.ts';
import {ProductionSessionController,ProductionSessionFacade} from '../src/ProductionSessionFacade.ts';
import {isMainWindowSnapshot,isDocumentOperation} from '../src/ProductionSessionTypes.ts';
import {fixtureAudioEffects,generation,incarnation,snapshot} from './session-readonly-fixture.mjs';

const checks=[];async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
const token='707c10bd-ad78-4371-ab44-d47dd7c79cb1';
const audio=fixtureAudioEffects(),stamp={version:1,documentIncarnation:audio.documentIncarnation};
const turn=async()=>{for(let i=0;i<5;i++)await Promise.resolve();};
function bridge(){const listeners=new Set(),retired=[];return {listeners,retired,sent:[],postMessage(c){if(c.type==='NegotiateAudioEffects'){assert.deepEqual(c.payload,{version:1});queueMicrotask(()=>this.receive(result(c,audio)));return;}if(c.type!=='GetMainWindowSnapshot')assert.deepEqual(c.audioEffects,stamp);this.sent.push(c);},addEventListener(_,f){assert(!listeners.has(f));listeners.add(f);},removeEventListener(_,f){assert(listeners.delete(f));retired.push(f);},receive(v){for(const f of listeners)f({data:v});},late(v){for(const f of retired)f({data:v});}};}
const result=(c,payload={status:'accepted',token},patch={})=>({kind:'result',version:1,requestId:c.requestId,type:c.type,generation:c.generation,sessionIncarnation:c.sessionIncarnation,ok:true,payload,...patch});
const page=(b=bridge())=>({b,f:new ProductionDocumentFacade(b,generation,incarnation)});

await check('The current Session accepts only an exact native document operation projection',()=>{
 const s=snapshot();assert(isMainWindowSnapshot(s,incarnation));delete s.documentOperation;assert(!isMainWindowSnapshot(s,incarnation));
 s.documentOperation={pending:{token,action:'open',stage:'decision'},last:{token,action:'save-as',outcome:'saved',message:''}};assert(isMainWindowSnapshot(s,incarnation));
 for(const op of [{},{pending:null,last:null,queue:[]},{pending:{token:'title-as-token',action:'new',stage:'decision'},last:null},{pending:{token,action:'add-media',stage:'decision'},last:null},{pending:{token,action:'close',stage:'react-owned'},last:null},{pending:null,last:{token,action:'open',outcome:'true',message:''}},{pending:null,last:{token,action:'open',outcome:'failed',message:2}}]){assert(!isDocumentOperation(op));s.documentOperation=op;assert(!isMainWindowSnapshot(s,incarnation));}
});
await check('Dirty truth remains native revision inequality even when document operations are present',()=>{
 const s=snapshot();s.documentOperation={pending:null,last:null};assert(isMainWindowSnapshot(s,incarnation));s.document.dirty=false;assert(!isMainWindowSnapshot(s,incarnation));
 s.document.cleanRevision=s.document.authoredRevision;assert(isMainWindowSnapshot(s,incarnation));
});
await check('Production runtime import graph has no prototype document authority or persistence',()=>{
 const root=fileURLToPath(new URL('../src/',import.meta.url)),visited=new Set();
 function inspect(path){if(visited.has(path))return;visited.add(path);const source=readFileSync(path,'utf8');
  assert(!/(?:sessionFixtures|sessionDocumentMock|useSessionPrototype|SessionDialogs)\.(?:ts|tsx)$/.test(path),path);assert(!/localStorage|sessionStorage|setInterval\s*\(/.test(source),path);
  for(const m of source.matchAll(/(?:^|\n)import(?!\s+type\b)[^;]*?from\s*['"](\.[^'"]+)['"]/g)){const base=resolve(dirname(path),m[1]),found=[base,base+'.ts',base+'.tsx'].find(existsSync);if(found&&!found.endsWith('.css'))inspect(found);}
 }
 inspect(resolve(root,'ProductionSessionShell.tsx'));assert(visited.has(resolve(root,'SessionDocumentDecision.tsx')));
});
await check('Missing bridge and malformed page context fail explicitly',()=>{
 assert.throws(()=>new ProductionDocumentFacade(undefined,generation,incarnation));for(const id of ['',token.toUpperCase(),'00000000-0000-0000-0000-000000000000'])assert.throws(()=>new ProductionDocumentFacade(bridge(),id,incarnation));
});
await check('Each document intent sends exactly the frozen command and empty payload',async()=>{
 const p=page();for(const [action,type]of [['new','NewDocument'],['open','OpenDocument'],['save','SaveDocument'],['save-as','SaveDocumentAs'],['close','RequestClose']]){
  const ack=p.f.begin(action,stamp.documentIncarnation);await turn();const c=p.b.sent.at(-1);assert.deepEqual({...c,requestId:'correlated'}, {kind:'command',version:1,requestId:'correlated',type,generation,sessionIncarnation:incarnation,payload:{},audioEffects:stamp});p.b.receive(result(c));assert.deepEqual(await ack,{status:'accepted',token});
 }p.f.dispose();
});
await check('Save/Discard/Cancel continue the exact opaque operation and never perform a document mutation',async()=>{
 const p=page();for(const choice of ['save','discard','cancel']){const ack=p.f.continue(token,choice,stamp.documentIncarnation);await turn();const c=p.b.sent.at(-1);assert.equal(c.type,'ContinueDocumentDecision');assert.deepEqual(c.payload,{token,choice});p.b.receive(result(c));await ack;}
 await assert.rejects(p.f.continue('Untitled','discard'));assert.equal(p.b.sent.length,3);p.f.dispose();
});
await check('Accepted, Busy, stale and unavailable are distinct acknowledgements, never filesystem completion',async()=>{
 const p=page();for(const status of ['accepted','busy','stale-operation','unavailable']){const ack=p.f.begin('save',stamp.documentIncarnation);await turn();const c=p.b.sent.at(-1),payload={status,token:status==='accepted'?token:null};p.b.receive(result(c,payload));assert.deepEqual(await ack,payload);}p.f.dispose();
});
await check('Old generation, other Session and unknown request cannot settle current page delivery',async()=>{
 const p=page();let settled=false;const ack=p.f.begin('open',stamp.documentIncarnation).then(x=>{settled=true;return x;});await turn();const c=p.b.sent.at(-1);
 p.b.receive(result(c,undefined,{generation:crypto.randomUUID()}));p.b.receive(result(c,undefined,{sessionIncarnation:crypto.randomUUID()}));p.b.receive(result(c,undefined,{requestId:crypto.randomUUID()}));await Promise.resolve();assert.equal(settled,false);p.b.receive(result(c));await ack;p.f.dispose();
});
await check('Malformed/cross-command envelope and fake successful completion are rejected',async()=>{
 for(const patch of [{type:'SaveDocumentAs'},{version:2},{extra:true},{payload:{status:'saved',token}},{payload:{status:'accepted',token:null}},{payload:{status:'accepted',token,dirty:false}}]){const p=page(),ack=p.f.begin('save',stamp.documentIncarnation);await turn();const c=p.b.sent.at(-1);p.b.receive(result(c,undefined,patch));await assert.rejects(ack,/Invalid native document acknowledgement/);p.f.dispose();}
});
await check('Native rejection is an unacknowledged intent, not a claim that Save failed',async()=>{
 const p=page(),ack=p.f.begin('save',stamp.documentIncarnation);await turn();const c=p.b.sent.at(-1);const {payload,...failure}=result(c);p.b.receive({...failure,ok:false,error:'owner-unavailable'});await assert.rejects(ack,/not acknowledged/);p.f.dispose();
});
await check('Transport send failure has no automatic retry or implicit cancellation',async()=>{
 const p=page(),deliver=p.b.postMessage;p.b.postMessage=function(c){deliver.call(this,c);if(c.type==='OpenDocument')throw new Error('wire lost');};await assert.rejects(p.f.begin('open',stamp.documentIncarnation),/Unable to deliver/);assert.equal(p.b.sent.length,1);p.f.dispose();assert.equal(p.b.sent.length,1);
});
await check('Thirty-two page disposal cycles release handlers, ignore late delivery and send no cancel/replay',async()=>{
 const b=bridge();for(let n=0;n<32;n++){const p=page(b),ack=p.f.continue(token,'save',stamp.documentIncarnation);await turn();const c=b.sent.at(-1);p.f.dispose();await assert.rejects(ack,/disposed/);b.late(result(c));assert.equal(b.listeners.size,0);assert.equal(b.sent.length,n+1);}assert(b.sent.every(c=>c.type==='ContinueDocumentDecision'&&c.payload.choice==='save'));
});
await check('Idle page creation/recreation never begins or continues a document operation',()=>{
 const b=bridge();for(let n=0;n<8;n++){const p=page(b);p.f.dispose();}assert.deepEqual(b.sent,[]);assert.equal(b.listeners.size,0);
});
await check('Overlapping snapshot refresh callers await the same drain through the queued authoritative read',async()=>{
 const b=bridge(),f=new ProductionSessionFacade(b,generation,incarnation),c=new ProductionSessionController(f);
 let firstDone=false,secondDone=false;const first=c.refresh(),second=c.refresh();assert.equal(first,second);first.then(()=>firstDone=true);second.then(()=>secondDone=true);
 assert.equal(b.sent.length,1);b.receive(result(b.sent[0],snapshot()));await Promise.resolve();await Promise.resolve();assert.equal(b.sent.length,2);assert.equal(firstDone,false);assert.equal(secondDone,false);
 const fresh=snapshot();fresh.document.displayName='Fresh authoritative filename';b.receive(result(b.sent[1],fresh));await Promise.all([first,second]);assert.equal(c.getSnapshot().snapshot.document.displayName,'Fresh authoritative filename');assert(firstDone&&secondDone);c.dispose();
});
await check('Disposal settles all shared refresh delivery waiters without stale notifications or replay',async()=>{
 const b=bridge(),c=new ProductionSessionController(new ProductionSessionFacade(b,generation,incarnation));let notifications=0;c.subscribe(()=>notifications++);const first=c.refresh(),second=c.refresh();c.dispose();await Promise.all([first,second]);assert.equal(notifications,0);assert.equal(b.listeners.size,0);assert.equal(b.sent.length,1);b.late(result(b.sent[0],snapshot()));assert.equal(notifications,0);
});
const evidence=new URL('../node_modules/.cache/current-contract/document/',import.meta.url);mkdirSync(evidence,{recursive:true});writeFileSync(new URL('focused-checks.json',evidence),JSON.stringify({checks,pass:checks.every(c=>c.pass),pageDisposalCycles:32,proofScope:'frontend contract/delivery only; native transaction and persistence proved separately'},null,2));for(const c of checks)console.log((c.pass?'PASS':'FAIL')+' '+c.name+(c.error?'\n'+c.error:''));console.log(checks.filter(c=>c.pass).length+'/'+checks.length);if(checks.some(c=>!c.pass))process.exitCode=1;
