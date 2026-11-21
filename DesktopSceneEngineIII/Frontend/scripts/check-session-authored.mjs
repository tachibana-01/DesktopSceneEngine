import assert from 'node:assert/strict';
import {readFileSync,existsSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {isMainWindowSnapshot} from '../src/ProductionSessionTypes.ts';
import {isAuthoredResult,isMediaImport} from '../src/ProductionAuthoredTypes.ts';
import {ProductionAuthoredFacade} from '../src/ProductionAuthoredFacade.ts';
import {authoredOutcomeMessages} from '../src/ProductionAuthoredFeedback.ts';
import {fixtureAudioEffects,incarnation,generation,ids,snapshot} from './session-readonly-fixture.mjs';

const checks=[];
async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
const token='707c10bd-ad78-4371-ab44-d47dd7c79cb1';
const audio=fixtureAudioEffects(),stamp={version:1,documentIncarnation:audio.documentIncarnation};
const turn=async()=>{for(let i=0;i<5;i++)await Promise.resolve();};
const outcome=(patch={})=>({status:'applied',playlistId:ids.b,itemId:null,token:null,authored:'committed',revision:'18446744073709551614',dirty:true,runtime:'not-required',positionRestore:'not-required',warnings:[],sources:[],...patch});
function bridge(){const listeners=new Set(),retired=[];return {sent:[],listeners,retired,postMessage(value){if(value.type==='NegotiateAudioEffects'){assert.deepEqual(value.payload,{version:1});queueMicrotask(()=>this.receive(response(value,audio)));return;}assert.deepEqual(value.audioEffects,stamp);this.sent.push(value);},addEventListener(_,fn){assert(!listeners.has(fn));listeners.add(fn);},removeEventListener(_,fn){assert(listeners.delete(fn));retired.push(fn);},receive(value){for(const fn of listeners)fn({data:value});},late(value){for(const fn of retired)fn({data:value});}};}
const response=(command,payload=outcome(),patch={})=>({kind:'result',version:1,type:command.type,requestId:command.requestId,generation:command.generation,sessionIncarnation:command.sessionIncarnation,ok:true,payload,...patch});

await check('Exact native media import capability projects authored state',()=>{
 const value=snapshot();value.mediaImport={pending:null,last:null};assert(isMainWindowSnapshot(value,incarnation),'An exact native B3 mediaImport capability must bootstrap without replacing native authored state');
 assert.equal(value.project.playlists[1].items[0].rate,1.25);assert.equal(value.document.authoredRevision,'18446744073709551614');
 for(const media of [{},{pending:null,last:null,queue:[]},{pending:{token,playlistId:'alias'},last:null},{pending:{token,playlistId:ids.b},last:{success:true}}]){value.mediaImport=media;assert(!isMainWindowSnapshot(value,incarnation));}
 value.mediaImport={pending:{token,playlistId:ids.b},last:outcome({token})};assert(isMainWindowSnapshot(value,incarnation));
});
await check('Duplicate aliases and media paths remain legal; selection stays separate from active',()=>{
 const value=snapshot();value.mediaImport={pending:null,last:null};value.project.playlists[0].alias=value.project.playlists[1].alias;
 const a=value.project.playlists[0].items[0],b=value.project.playlists[1].items[0];a.alias=b.alias;a.source=b.source;
 assert(isMainWindowSnapshot(value,incarnation));assert.equal(value.selection.playlistId,ids.b);assert.equal(value.active.playlistId,ids.a);
 value.project.playlists.reverse();assert(isMainWindowSnapshot(value,incarnation));assert.equal(value.selection.itemId,ids.video);
});
await check('Authored and runtime outcomes retain independent partial results and ordered per-source warnings',()=>{
 const value=outcome({token,runtime:'warning',positionRestore:'rebound-at-start',warnings:[{code:'runtime-sync',message:'Authored change kept'}],sources:[
  {source:'same.mp4',itemId:ids.video,authored:'committed',runtime:'warning',warnings:[{code:'runtime',message:'Bind warning'}]},
  {source:'unsupported.txt',itemId:null,authored:'unsupported',runtime:'not-required',warnings:[{code:'unsupported',message:'Unsupported source'}]},
  {source:'same.mp4',itemId:ids.image,authored:'committed',runtime:'synchronized',warnings:[]}]});
 assert(isAuthoredResult(value));assert.equal(value.authored,'committed');assert(value.dirty);assert.deepEqual(value.sources.map(s=>s.source),['same.mp4','unsupported.txt','same.mp4']);
 assert(isMediaImport({pending:null,last:value}));
 for(const change of [v=>v.extra=true,v=>v.revision=18446744073709551614,v=>v.status='success',v=>v.authored=true,v=>v.runtime='committed',v=>v.warnings=[{message:'missing code'}],v=>v.sources[0].itemId='source-as-id',v=>v.itemId=ids.image,v=>{v.status='accepted';v.token=null;}]){
  const bad=structuredClone(value);if(change.toString().includes('v.itemId='))bad.playlistId=null;change(bad);assert(!isAuthoredResult(bad));
 }
});
await check('Every typed authored command sends its exact payload and stable native identities',async()=>{
 const wire=bridge(),facade=new ProductionAuthoredFacade(wire,generation,incarnation);
 const pl={playlistId:ids.b},it={...pl,itemId:ids.video};
 const intents=[['SelectPlaylist',pl],['AddPlaylist',{}],['RenamePlaylist',{...pl,alias:'same',expectedAlias:'same'}],['RemovePlaylist',{...pl,expectedAlias:'same'}],['MovePlaylist',{...pl,direction:-1}],['SetPlaylistEnabled',{...pl,enabled:false}],['SetProjectPolicy',{mode:'random'}],['SetPlaylistPolicy',{...pl,mode:'repeat-current'}],['SetPlaylistMaster',{...pl,value:.37}],['SelectItem',it],['AddMedia',pl],['RenameItem',{...it,alias:'same',expectedAlias:'same'}],['RemoveItem',{...it,expectedAlias:'same'}],['MoveItem',{...it,direction:1}],['SetItemEnabled',{...it,enabled:true}],['SetImageDuration',{...it,value:14.2}],['ApplyVideoRate',{...it,value:1.25}],['SetItemGain',{...it,value:.63}],['SetItemPresentation',{...it,patch:{positionX:.35}}]];
 for(const [type,payload]of intents){const sent=facade.send({type,payload},stamp.documentIncarnation);await turn();const command=wire.sent.at(-1);assert.deepEqual({...command,requestId:'request'},{kind:'command',version:1,requestId:'request',type,generation,sessionIncarnation:incarnation,payload,audioEffects:stamp});wire.receive(response(command));assert.deepEqual(await sent,outcome());}
 assert(wire.sent.every(c=>!Object.hasOwn(c.payload,'index')&&!Object.hasOwn(c.payload,'source')));assert.deepEqual(wire.sent.at(-1).payload.patch,{positionX:.35});facade.dispose();
});
await check('Aggregated import warnings do not repeat ordered per-source or shared runtime warnings',()=>{
 const unsupported={code:'unsupported-media',message:'Unsupported source'},runtime={code:'runtime-sync-warning',message:'Committed, bind warning'};
 const source=(name,authored,warnings)=>({source:name,itemId:authored==='committed'?ids.video:null,authored,runtime:'warning',warnings});
 const value=outcome({runtime:'warning',warnings:[unsupported,runtime],sources:[source('one.mp4','committed',[runtime]),source('bad.txt','unsupported',[unsupported]),source('two.mp4','committed',[runtime])]});
 assert.deepEqual(authoredOutcomeMessages(value,message=>message),['one.mp4: Committed, bind warning','bad.txt: Unsupported source']);
 value.sources.push(source('bad.txt','unsupported',[unsupported]));assert.equal(authoredOutcomeMessages(value,message=>message).filter(message=>message==='bad.txt: Unsupported source').length,2,'Distinct source occurrences retain their outcome');
});
await check('Wrong generation, Session and request cannot settle current page intent',async()=>{
 const wire=bridge(),facade=new ProductionAuthoredFacade(wire,generation,incarnation);let settled=false;
 const pending=facade.send({type:'SetPlaylistMaster',payload:{playlistId:ids.b,value:.2}},stamp.documentIncarnation).then(value=>{settled=true;return value;});await turn();const command=wire.sent[0];
 wire.receive(response(command,undefined,{generation:crypto.randomUUID()}));wire.receive(response(command,undefined,{sessionIncarnation:crypto.randomUUID()}));wire.receive(response(command,undefined,{requestId:crypto.randomUUID()}));await Promise.resolve();assert(!settled);
 wire.receive(response(command,outcome({runtime:'warning'})));assert.equal((await pending).runtime,'warning');facade.dispose();
});
await check('Malformed/cross-command envelopes and boolean success results fail explicitly',async()=>{
 for(const patch of [{type:'AddMedia'},{version:2},{extra:true},{payload:{success:true}},{payload:outcome({dirty:'true'})}]){
  const wire=bridge(),facade=new ProductionAuthoredFacade(wire,generation,incarnation),pending=facade.send({type:'AddPlaylist',payload:{}},stamp.documentIncarnation);await turn();wire.receive(response(wire.sent[0],undefined,patch));await assert.rejects(pending,/Invalid native authored result/);facade.dispose();
 }
});
await check('Fifty disposal/recreation cycles ignore old-page results and never cancel/replay native commits',async()=>{
 const wire=bridge();for(let n=0;n<50;n++){
  const facade=new ProductionAuthoredFacade(wire,generation,incarnation),pending=facade.send({type:'AddMedia',payload:{playlistId:ids.b}},stamp.documentIncarnation);await turn();const command=wire.sent.at(-1);facade.dispose();await assert.rejects(pending,/disposed/);wire.late(response(command,outcome({status:'accepted',token,authored:'not-applicable'})));assert.equal(wire.listeners.size,0);assert.equal(wire.sent.length,n+1);
 }
 for(let n=0;n<5;n++){const facade=new ProductionAuthoredFacade(wire,generation,incarnation);facade.dispose();}assert.equal(wire.sent.length,50);assert(wire.sent.every(c=>c.type==='AddMedia'));
});
await check('Transport failure has one submission, unknown outcome and no retry',async()=>{
 const wire=bridge(),facade=new ProductionAuthoredFacade(wire,generation,incarnation),deliver=wire.postMessage;wire.postMessage=function(c){deliver.call(this,c);if(c.type==='AddPlaylist')throw new Error('wire lost');};await assert.rejects(facade.send({type:'AddPlaylist',payload:{}},stamp.documentIncarnation),/Unable to deliver/);assert.equal(wire.sent.length,1);facade.dispose();
});
await check('Production runtime import graph has no prototype authority, persistence or background mutation timer',()=>{
 const root=fileURLToPath(new URL('../src/',import.meta.url)),visited=new Set();
 function inspect(path){if(visited.has(path))return;visited.add(path);const source=readFileSync(path,'utf8');assert(!/(?:sessionFixtures|sessionDocumentMock|useSessionPrototype|SessionDialogs|sessionInspectorModel|sessionEntities)\.(?:ts|tsx)$/.test(path),path);assert(!/localStorage|sessionStorage|setInterval\s*\(/.test(source),path);
  for(const match of source.matchAll(/(?:^|\n)import(?!\s+type\b)[^;]*?from\s*['"](\.[^'"]+)['"]/g)){const base=resolve(dirname(path),match[1]),found=[base,base+'.ts',base+'.tsx'].find(existsSync);if(found&&!found.endsWith('.css'))inspect(found);}
 }inspect(resolve(root,'ProductionSessionShell.tsx'));assert(visited.has(resolve(root,'ProductionAuthoredFacade.ts')));assert(visited.has(resolve(root,'ProductionEntityCommands.tsx')));
});
const report={pass:checks.every(c=>c.pass),checks,proofScope:'Actual frontend validators/delivery; native mutation/persistence/runtime are verified separately'};
const evidence=new URL('../node_modules/.cache/current-contract/authored/',import.meta.url);mkdirSync(evidence,{recursive:true});writeFileSync(new URL('focused-checks.json',evidence),JSON.stringify(report,null,2));
for(const check of checks)console.log((check.pass?'PASS':'FAIL')+' '+check.name+(check.error?'\n'+check.error:''));console.log(checks.filter(c=>c.pass).length+'/'+checks.length);if(!report.pass)process.exitCode=1;
