import assert from'node:assert/strict';import fs from'node:fs';
import{ProductionSessionFacade,ProductionSessionController}from'../src/ProductionSessionFacade.ts';
import{isAuthoredResult}from'../src/ProductionAuthoredTypes.ts';import{fixtureAudioEffects,generation,incarnation,ids,snapshot}from'./session-readonly-fixture.mjs';
import{ProductionAuthoredFacade}from'../src/ProductionAuthoredFacade.ts';
const checks=[];async function check(name,run){await run();checks.push({name,pass:true});}
const result=(patch={})=>({status:'applied',playlistId:ids.b,itemId:null,token:null,authored:'committed',revision:'12',dirty:true,runtime:'not-required',positionRestore:'not-required',warnings:[],sources:[],playlistMasterVolume:.85,...patch});
await check('Narrow Master schema validates native read-back, including zero/rejection; rejects malformed/unknown values',()=>{for(const value of[0,.35,.85,1])assert(isAuthoredResult(result({playlistMasterVolume:value})));assert(isAuthoredResult(result({authored:'rejected',status:'invalid-value',playlistMasterVolume:.4})));for(const value of[-1,1.1,NaN,Infinity,'85',null])assert(!isAuthoredResult(result({playlistMasterVolume:value})));assert(!isAuthoredResult(result({playlistId:null})));assert(!isAuthoredResult({...result(),optimisticMaster:85}));});
const audio=fixtureAudioEffects();
const listeners=new Set(),bridge={sent:[],postMessage(v){if(v.type==='NegotiateAudioEffects'){queueMicrotask(()=>this.reply(v,audio));return;}this.sent.push(v);},addEventListener(_,fn){listeners.add(fn);},removeEventListener(_,fn){listeners.delete(fn);},reply(command,payload){for(const f of listeners)f({data:{kind:'result',version:1,type:command.type,requestId:command.requestId,generation,sessionIncarnation:incarnation,ok:true,payload}});}};
const controller=new ProductionSessionController(new ProductionSessionFacade(bridge,generation,incarnation));const value=snapshot();value.document={...value.document,authoredRevision:'11',cleanRevision:'11',dirty:false};value.documentOperation={pending:null,last:null};value.mediaImport={pending:null,last:null};
await check('Narrow read-back is fenced to pending SetMaster and exact stable Playlist ID',async()=>{
 const facade=new ProductionAuthoredFacade(bridge,generation,incarnation);
 async function exchange(intent,reply){
  const promise=facade.send(intent,audio.documentIncarnation),previous=bridge.sent.length;
  for(let turn=0;turn<5&&bridge.sent.length===previous;turn++)await Promise.resolve();
  assert.equal(bridge.sent.length,previous+1);
  const command=bridge.sent.at(-1);
  assert.deepEqual(command.audioEffects,{version:1,documentIncarnation:audio.documentIncarnation});
  bridge.reply(command,reply);
  return promise;
 }
 await assert.rejects(exchange({type:'SetPlaylistMaster',payload:{playlistId:ids.a,value:.85}},result({playlistId:ids.b})),/requested Playlist/);
 await assert.rejects(exchange({type:'RenamePlaylist',payload:{playlistId:ids.b,alias:'New',expectedAlias:'Old'}},result()),/requested Playlist/);
 assert.equal((await exchange({type:'SetPlaylistMaster',payload:{playlistId:ids.b,value:.85}},result())).playlistMasterVolume,.85);
 facade.dispose();
});
let ready=controller.refresh();bridge.reply(bridge.sent.at(-1),structuredClone(value));await ready;
await check('Master-only projection updates one stable Playlist, keeps siblings/items/selection and does not read a snapshot',async()=>{const before=controller.getSnapshot().snapshot,n=bridge.sent.length;assert(await controller.projectPlaylistMaster(result()));const after=controller.getSnapshot().snapshot;assert.equal(after.project.playlists[1].masterVolume,.85);assert.equal(after.project.playlists[0],before.project.playlists[0]);assert.equal(after.project.playlists[1].items,before.project.playlists[1].items);assert.equal(after.selection,before.selection);assert.equal(after.active,before.active);assert.equal(after.document.authoredRevision,'12');assert.equal(after.document.dirty,true);assert.equal(bridge.sent.length,n);});
await check('Late acknowledgement cannot regress newer authored revision or manufacture dirty after Save',async()=>{await controller.projectPlaylistMaster(result({revision:'13',playlistMasterVolume:.7}));await controller.projectPlaylistMaster(result({revision:'12',playlistMasterVolume:.2}));assert.equal(controller.getSnapshot().snapshot.project.playlists[1].masterVolume,.7);const saved=structuredClone(controller.getSnapshot().snapshot);saved.document.cleanRevision='13';saved.document.dirty=false;ready=controller.refresh();bridge.reply(bridge.sent.at(-1),saved);await ready;await controller.projectPlaylistMaster(result({revision:'13',dirty:true,playlistMasterVolume:.7}));assert.equal(controller.getSnapshot().snapshot.document.dirty,false);});
await check('An already-inflight full read finishes before newer narrow acknowledgement; no second read or late overwrite',async()=>{ready=controller.refresh();const read=bridge.sent.at(-1),older=structuredClone(controller.getSnapshot().snapshot);const n=bridge.sent.length,projected=controller.projectPlaylistMaster(result({revision:'14',playlistMasterVolume:.85}));bridge.reply(read,older);await ready;await projected;assert.equal(bridge.sent.length,n);assert.equal(controller.getSnapshot().snapshot.document.authoredRevision,'14');assert.equal(controller.getSnapshot().snapshot.project.playlists[1].masterVolume,.85);});
await check('Rejected command can reconcile actual read-back, absent capability/missing Playlist requires full refresh',async()=>{await controller.projectPlaylistMaster(result({revision:'14',authored:'rejected',status:'invalid-value',playlistMasterVolume:.85}));assert.equal(controller.getSnapshot().snapshot.project.playlists[1].masterVolume,.85);assert.equal(await controller.projectPlaylistMaster(result({playlistMasterVolume:undefined})),false);assert.equal(await controller.projectPlaylistMaster(result({revision:'15',playlistId:crypto.randomUUID()})),false);});
controller.dispose();
await check('Different Playlist partial acknowledgements in reverse order cannot lose an intermediate native truth',async()=>{const other=new ProductionSessionController(new ProductionSessionFacade(bridge,generation,incarnation));const read=other.refresh();bridge.reply(bridge.sent.at(-1),structuredClone(value));await read;await other.projectPlaylistMaster(result({playlistId:ids.b,revision:'13',playlistMasterVolume:.3}));await other.projectPlaylistMaster(result({playlistId:ids.a,revision:'12',playlistMasterVolume:.2}));const actual=other.getSnapshot().snapshot;try{assert.equal(actual.project.playlists[0].masterVolume,.2);assert.equal(actual.project.playlists[1].masterVolume,.3);assert.equal(actual.document.authoredRevision,'13');}finally{other.dispose();}});
const out=new URL('../node_modules/.cache/current-contract/playlist-master/',import.meta.url);
fs.mkdirSync(out,{recursive:true});
fs.writeFileSync(new URL('projection-green.json',out),JSON.stringify({pass:true,checks},null,2)+'\n');console.log(JSON.stringify({pass:true,checks:checks.length}));
