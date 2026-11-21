import assert from 'node:assert/strict';
import {LatestUnsentAudioScalar,UnsentAudioMuteParity} from '../src/LatestUnsentAudioScalar.ts';
import {ProductionAudioEffects,isAudioEffectReceipt,isAudioEffectsState} from '../src/ProductionAudioEffects.ts';
import {ProductionAuthoredFacade} from '../src/ProductionAuthoredFacade.ts';
import {ProductionRuntimeFacade,isRuntimePlaybackSample} from '../src/ProductionRuntimeFacade.ts';
import {runtimeSessionProjection} from '../src/ProductionRuntimeProjection.ts';
import {ProductionSessionFacade,ProductionSessionController} from '../src/ProductionSessionFacade.ts';
import {isMainWindowSnapshot} from '../src/ProductionSessionTypes.ts';
import {fixtureRuntimeSample,snapshot as sessionSnapshot} from './session-readonly-fixture.mjs';

const checks=[];
const check=async(name,run)=>{try{await run();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}};
const flush=async()=>{for(let n=0;n<12;n++)await Promise.resolve();};
const deferred=()=>{let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {promise,resolve,reject};};
const generation='11111111-1111-1111-1111-111111111111',session='22222222-2222-2222-2222-222222222222',document='33333333-3333-3333-3333-333333333333',operation='44444444-4444-4444-4444-444444444444',playlist='55555555-5555-5555-5555-555555555555';
const receipt=(patch={})=>({operation,documentIncarnation:document,bindingToken:'binding',execution:'18446744073709551615',sequence:'9007199254740993',scope:'embedded-gain',phase:'pending',error:'',effectTimeNs:'0',playlistId:playlist,itemId:null,...patch});
const state=(effects=[],patch={})=>({version:1,documentIncarnation:document,execution:'18446744073709551615',capacityRevision:'1',closing:false,stopped:false,desiredMuted:false,appliedMuted:false,muteKnown:true,deviceHealth:'healthy',effects,...patch});
function wire(){const listeners=new Set();return {sent:[],addEventListener(_,fn){listeners.add(fn);},removeEventListener(_,fn){listeners.delete(fn);},postMessage(command){this.sent.push(command);},receive(v){for(const fn of [...listeners])fn({data:v});},reply(command,payload,ok=true){this.receive({kind:'result',version:1,requestId:command.requestId,type:command.type,generation:command.generation,sessionIncarnation:command.sessionIncarnation,ok,...(ok?{payload}:{error:'unsupported'})});},event(payload,patch={}){this.receive({kind:'event',version:1,type:'AudioEffectsChanged',generation,sessionIncarnation:session,payload,...patch});},listeners};}

await check('rapid drag holds exactly one admitted request and preserves final unsent value',async()=>{
  const sent=[],jobs=[];const d=new LatestUnsentAudioScalar((id,value)=>{sent.push([id,value]);const job=deferred();jobs.push(job);return job.promise;});d.setIdentity('doc/playlist/gesture');
  const first=d.submit(.1);await flush();const middle=d.submit(.2),last=d.submit(.9);await flush();assert.deepEqual(sent,[['doc/playlist/gesture',.1]]);assert.equal(await middle,false);
  jobs[0].resolve('terminal');await flush();assert.equal(await first,true);assert.deepEqual(sent,[['doc/playlist/gesture',.1],['doc/playlist/gesture',.9]]);
  jobs[1].resolve('terminal');assert.equal(await last,true);d.dispose();
});
await check('Busy performs no retry until an explicit matching capacity observation',async()=>{
  const sent=[];let response='busy';const d=new LatestUnsentAudioScalar(async(id,value)=>{sent.push(value);return response;});d.setIdentity('doc/A');const first=d.submit(.2);await flush();const final=d.submit(.8);await flush();assert.equal(sent.length,1);
  response='terminal';d.capacityAvailable('otherdoc/A');await flush();assert.equal(sent.length,1);d.capacityAvailable('doc/A');await flush();assert.equal(await first,false);assert.equal(await final,true);assert.deepEqual(sent,[.2,.8]);d.dispose();
});
await check('capacity observed before Busy reply is latched without a lost wake or timer',async()=>{
  const jobs=[],sent=[];const d=new LatestUnsentAudioScalar(async(id,value)=>{sent.push(value);const job=deferred();jobs.push(job);return job.promise;});d.setIdentity('doc/A');
  const first=d.submit(.1);await flush();const last=d.submit(.8);d.capacityAvailable('doc/A','native-frontier-2');jobs[0].resolve('busy');await flush();assert.deepEqual(sent,[.1,.8]);assert.equal(await first,false);jobs[1].resolve('terminal');assert.equal(await last,true);d.dispose();
  const muteJobs=[],muteSent=[];const mute=new UnsentAudioMuteParity(async()=>{muteSent.push(1);const job=deferred();muteJobs.push(job);return job.promise;});mute.setIdentity('doc/out');mute.click();await flush();mute.capacityAvailable('doc/out','native-frontier-2');muteJobs[0].resolve('busy');await flush();assert.equal(muteSent.length,2);muteJobs[1].resolve('terminal');await flush();mute.dispose();
});
await check('internal capacity revision wakes the final Busy draft without any effect metadata change',async()=>{
  const w=wire(),owner=ProductionAudioEffects.retain(w,generation,session),sent=[];
  let response='busy';const d=new LatestUnsentAudioScalar(async(id,value)=>{sent.push(value);return response;});d.setIdentity('doc/A');
  const unsubscribe=owner.subscribe(value=>d.capacityAvailable('doc/A',JSON.stringify(value)));
  const ready=owner.ready();w.reply(w.sent[0],state());await ready;
  const first=d.submit(.1);await flush();const last=d.submit(.9);await flush();assert.deepEqual(sent,[.1]);
  w.event(state());await flush();assert.deepEqual(sent,[.1]);response='terminal';
  const released=state([],{capacityRevision:'2'});assert.deepEqual(released.effects,owner.state.effects);w.event(released);await flush();
  assert.equal(await first,false);assert.equal(await last,true);assert.deepEqual(sent,[.1,.9]);unsubscribe();d.dispose();owner.release();
});
await check('document/target/Close invalidation prevents late terminal from dispatching draft',async()=>{
  const job=deferred(),sent=[];const d=new LatestUnsentAudioScalar(async(id,value)=>{sent.push([id,value]);return job.promise;});d.setIdentity('old-doc/A');const first=d.submit(.1);await flush();const final=d.submit(.8);d.setIdentity(null);job.resolve('terminal');await flush();assert.equal(await first,false);assert.equal(await final,false);assert.equal(sent.length,1);d.dispose();
});
await check('unknown outcome suspends the live interaction without timeout replay',async()=>{
  const sent=[];const d=new LatestUnsentAudioScalar(async(id,value)=>{sent.push(value);return 'unknown';});d.setIdentity('doc/A');assert.equal(await d.submit(.2),false);assert.equal(await d.submit(.7),false);d.capacityAvailable('doc/A');await flush();assert.deepEqual(sent,[.2]);d.dispose();
});
await check('invalidation before the dispatch microtask sends no stale scalar or mute intent',async()=>{
  const sent=[];const d=new LatestUnsentAudioScalar(async(id,value)=>{sent.push(value);return 'terminal';});d.setIdentity('old');const pending=d.submit(.7);d.setIdentity(null);
  const mute=new UnsentAudioMuteParity(async()=>{sent.push('mute');return 'terminal';});mute.setIdentity('old');mute.click();mute.setIdentity(null);await flush();assert.equal(await pending,false);assert.deepEqual(sent,[]);d.dispose();mute.dispose();
});
await check('mute retains unsent relative click parity and invalidation clears it',async()=>{
  const sent=[],jobs=[];const d=new UnsentAudioMuteParity(async id=>{sent.push(id);const job=deferred();jobs.push(job);return job.promise;});d.setIdentity('doc/output');d.click();await flush();d.click();d.click();jobs[0].resolve('terminal');await flush();assert.equal(sent.length,1);
  d.click();await flush();d.click();d.setIdentity(null);jobs[1].resolve('terminal');await flush();assert.equal(sent.length,2);d.dispose();
});
await check('an unrelated mute completion invalidates Busy parity instead of replaying the click',async()=>{
  const job=deferred(),sent=[];const d=new UnsentAudioMuteParity(async()=>{sent.push(1);return job.promise;});d.setIdentity('doc/output');d.observeNative('doc/output','1','7','pending');d.click();await flush();
  d.observeNative('doc/output','1','7','applied');d.capacityAvailable('doc/output','external-7-applied');job.resolve('busy');await flush();assert.equal(sent.length,1);d.dispose();
});
await check('own mute admission permits parity, later unrelated native mute clears it',async()=>{
  const jobs=[],sent=[];const d=new UnsentAudioMuteParity(async(id,admitted)=>{sent.push(1);admitted('1',String(10+sent.length));const job=deferred();jobs.push(job);return job.promise;});d.setIdentity('doc/output');d.observeNative('doc/output','1','10','applied');d.click();await flush();d.click();d.observeNative('doc/output','1','11','applied');jobs[0].resolve('terminal');await flush();assert.equal(sent.length,2);
  d.click();d.observeNative('doc/output','1','13','applied');jobs[1].resolve('terminal');await flush();assert.equal(sent.length,2);d.dispose();
});
await check('delayed own predecessor observation does not discard parity behind the next own flight',async()=>{
  const jobs=[];const d=new UnsentAudioMuteParity(async(id,admitted)=>{const job=deferred();jobs.push({...job,admitted});return job.promise;});d.setIdentity('doc/output');d.observeNative('doc/output','1','10','applied');
  d.click();await flush();jobs[0].admitted('1','11');d.click();jobs[0].resolve('terminal');await flush();assert.equal(jobs.length,2);
  d.click();d.observeNative('doc/output','1','11','applied');jobs[1].admitted('1','12');jobs[1].resolve('terminal');await flush();assert.equal(jobs.length,3);jobs[2].admitted('1','13');jobs[2].resolve('terminal');await flush();d.dispose();
});
await check('strict receipts preserve full uint64 sequence and reject invented/oversized state',()=>{
  assert(isAudioEffectReceipt(receipt()));assert(isAudioEffectsState(state([receipt()])));assert(isAudioEffectsState(state([],{capacityRevision:'18446744073709551615'})));assert(!isAudioEffectsState(state([],{capacityRevision:9007199254740993})));assert(!isAudioEffectsState(state([],{capacityRevision:'18446744073709551616'})));assert(!isAudioEffectsState(state([],{capacityRevision:undefined})));assert(!isAudioEffectReceipt(receipt({sequence:9007199254740993})));assert(!isAudioEffectReceipt(receipt({execution:'18446744073709551616'})));assert(!isAudioEffectReceipt(receipt({phase:'queued-success'})));assert(!isAudioEffectsState(state([receipt(),receipt()])));
});
await check('runtime as-of frontier and time preserve 64-bit strings and reject rounded or invalid values',()=>{
  const sample={bindingToken:'binding',asOfEffectSequence:'18446744073709551615',asOfTimeNs:'9223372036854775807',active:{playlistId:playlist,itemId:null},hasMedia:false,state:'unavailable',position:0,duration:0,actualRate:1,outputMuted:false,canPlayPause:false,canPrevious:false,canNext:false,canSeek:false,canMute:true,sceneIndex:-1,sceneCount:0,mediaKind:null};
  assert(isRuntimePlaybackSample(sample));assert(isRuntimePlaybackSample({...sample,asOfEffectSequence:'0',asOfTimeNs:'-9223372036854775808'}));
  for(const patch of [{asOfEffectSequence:9007199254740993},{asOfEffectSequence:'18446744073709551616'},{asOfEffectSequence:'01'},{asOfTimeNs:9007199254740993},{asOfTimeNs:'9223372036854775808'},{asOfTimeNs:'-9223372036854775809'},{asOfTimeNs:'-0'},{asOfTimeNs:'1.5'},{asOfTimeNs:undefined}])assert(!isRuntimePlaybackSample({...sample,...patch}),JSON.stringify(patch));
});
await check('production Session bootstrap keeps logical binding facts separate from applied runtime samples',async()=>{
  const w=wire(),facade=new ProductionSessionFacade(w,generation,session),controller=new ProductionSessionController(facade);
  const runtime=new ProductionRuntimeFacade(w,generation,session);
  const native=sessionSnapshot();native.session.incarnation=session;
  const binding=receipt({scope:'structural',playlistId:native.active.playlistId,itemId:native.active.itemId});
  const pending={...native,audioEffects:state([binding])};assert(isMainWindowSnapshot(pending,session));
  const bootstrap=controller.refresh();w.reply(w.sent.at(-1),pending);await bootstrap;
  let view=controller.getSnapshot();assert.equal(view.status,'ready');assert.equal(runtimeSessionProjection(view.snapshot,runtime.sample).runtime,null);assert.deepEqual(view.snapshot.active,native.active);assert.deepEqual(view.snapshot.selection,native.selection);assert.equal(view.snapshot.audioEffects.effects[0].phase,'pending');
  const applied={...native,audioEffects:state([{...binding,phase:'applied',effectTimeNs:'44'}],{capacityRevision:'2'})};
  const refresh=controller.refresh();w.reply(w.sent.at(-1),applied);await refresh;view=controller.getSnapshot();
  assert.equal(view.status,'ready');assert.equal(view.snapshot.audioEffects.effects[0].phase,'applied');
  assert.equal(runtimeSessionProjection(view.snapshot,runtime.sample).runtime,null);
  const sample=runtime.start(),runtimeRead=w.sent.at(-1);
  assert.equal(runtimeRead.type,'GetRuntimePlaybackSample');
  w.reply(runtimeRead,{...fixtureRuntimeSample(native),asOfEffectSequence:binding.sequence,asOfTimeNs:'44'});await sample;
  assert.equal(runtimeSessionProjection(view.snapshot,runtime.sample).runtime.rate,1.1);
  runtime.dispose();controller.dispose();assert.equal(w.listeners.size,0);
});
await check('native capability failure prevents any authored mutation from being sent',async()=>{
  const w=wire(),f=new ProductionAuthoredFacade(w,generation,session);f.audioEffects.seedNativeSnapshot(state());const result=f.send({type:'SetPlaylistMaster',payload:{playlistId:playlist,value:.4}}).then(()=>false,()=>true);await flush();assert.deepEqual(w.sent.map(c=>c.type),['NegotiateAudioEffects']);w.reply(w.sent[0],null,false);assert.equal(await result,true);assert.equal(w.sent.length,1);f.dispose();
});
await check('pending authored reply does not complete before a matching applied fact',async()=>{
  const w=wire(),f=new ProductionAuthoredFacade(w,generation,session);f.audioEffects.seedNativeSnapshot(state());let complete=false;
  const result=f.send({type:'SetPlaylistMaster',payload:{playlistId:playlist,value:.4}}).then(v=>{complete=true;return v;});await flush();w.reply(w.sent[0],state());await flush();
  const command=w.sent.find(c=>c.type==='SetPlaylistMaster');assert(command);assert.deepEqual(command.audioEffects,{version:1,documentIncarnation:document});w.reply(command,{status:'applied',playlistId:playlist,itemId:null,token:null,authored:'committed',revision:'2',dirty:true,runtime:'pending',runtimeOnApplied:'synchronized',positionRestore:'not-required',warnings:[],sources:[],playlistMasterVolume:.4,audioEffect:receipt()});await flush();assert.equal(complete,false);
  const read=w.sent.find(c=>c.type==='GetAudioEffects');if(read)w.reply(read,state([receipt()]));w.event(state([receipt({phase:'applied',effectTimeNs:'9007199254740993'})]));const value=await result;assert(value.dirty);assert.equal(value.revision,'2');assert.equal(value.runtime,'synchronized');assert.equal(value.audioEffect.effectTimeNs,'9007199254740993');f.dispose();
});
await check('document replacement during capability await cannot admit an old same-ID intent',async()=>{
  const w=wire(),f=new ProductionAuthoredFacade(w,generation,session);f.audioEffects.seedNativeSnapshot(state());
  const result=f.send({type:'SetPlaylistMaster',payload:{playlistId:playlist,value:.4}},document).then(()=>false,()=>true);await flush();
  w.reply(w.sent[0],{...state(),documentIncarnation:'77777777-7777-7777-7777-777777777777'});assert.equal(await result,true);assert.deepEqual(w.sent.map(c=>c.type),['NegotiateAudioEffects']);f.dispose();
});
await check('Expired bounded native history is unknown rather than fabricated success',async()=>{
  const w=wire(),owner=ProductionAudioEffects.retain(w,generation,session);const ready=owner.ready();w.reply(w.sent[0],state());await ready;
  const waiting=owner.wait(receipt()).then(()=>false,()=>true);await flush();w.event(state([receipt({operation:'66666666-6666-6666-6666-666666666666',sequence:'9007199254740994',phase:'applied'})]));assert.equal(await waiting,true);
  const read=w.sent.find(c=>c.type==='GetAudioEffects');if(read)w.reply(read,state());owner.release();assert.equal(w.listeners.size,0);
});
await check('a delayed native read reply cannot overwrite a newer Applied event',async()=>{
  const w=wire(),owner=ProductionAudioEffects.retain(w,generation,session);const ready=owner.ready();w.reply(w.sent[0],state([receipt()]));await ready;
  const read=owner.refresh();await flush();const command=w.sent.at(-1);w.event(state([receipt({phase:'applied',effectTimeNs:'44'})]));w.reply(command,state([receipt()]));await read;assert.equal(owner.state.effects[0].phase,'applied');owner.release();
});
await check('older Applied, Failed and Cancelled receipts leave newer admitted work pending',async()=>{
  const stateWithPredecessor=phase=>state([receipt({
    operation:'66666666-6666-6666-6666-666666666666',sequence:'9007199254740992',phase,
  })]);
  const w=wire(),owner=ProductionAudioEffects.retain(w,generation,session);
  const ready=owner.ready();w.reply(w.sent[0],stateWithPredecessor('applied'));await ready;
  let outcome;
  const waiting=owner.wait(receipt()).then(value=>{outcome=value;},error=>{outcome=error;});
  try{
    await flush();assert.equal(outcome,undefined);
    const read=w.sent.find(command=>command.type==='GetAudioEffects');
    w.reply(read,stateWithPredecessor('failed'));await flush();assert.equal(outcome,undefined);
    w.event(stateWithPredecessor('cancelled'));await flush();assert.equal(outcome,undefined);
    const matching=receipt({phase:'applied',effectTimeNs:'42'});w.event(state([matching]));
    await waiting;assert.deepEqual(outcome,matching);
  }finally{owner.release();}
});
await check('a terminal receipt must match document, binding, target, operation and scope',async()=>{
  const w=wire(),owner=ProductionAudioEffects.retain(w,generation,session);
  const ready=owner.ready();w.reply(w.sent[0],state());await ready;
  const admitted=receipt({scope:'item-bgm-gain',itemId:'77777777-7777-7777-7777-777777777777'});
  let outcome;
  const waiting=owner.wait(admitted).then(value=>{outcome=value;},error=>{outcome=error;});
  try{
    await flush();
    const read=w.sent.find(command=>command.type==='GetAudioEffects');
    w.reply(read,state([admitted]));
    const terminal={...admitted,phase:'applied'};
    for(const patch of [
      {documentIncarnation:'88888888-8888-8888-8888-888888888888'},
      {bindingToken:'other-binding'},
      {playlistId:'99999999-9999-9999-9999-999999999999'},
      {itemId:'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa'},
      {scope:'playlist-bgm-gain',itemId:null},
      {execution:'17'},
      {operation:'66666666-6666-6666-6666-666666666666'},
    ]){
      w.event(state([{...terminal,...patch}]));await flush();assert.equal(outcome,undefined);
    }
    w.event(state([terminal]));await waiting;assert.deepEqual(outcome,terminal);
  }finally{owner.release();}
});
await check('optional read failure cannot settle admitted work; matching failure and next success stay isolated',async()=>{
  const w=wire(),owner=ProductionAudioEffects.retain(w,generation,session);
  const ready=owner.ready();w.reply(w.sent[0],state());await ready;
  const pending=receipt({scope:'playlist-bgm-gain'});
  let firstOutcome;
  const first=owner.wait(pending).then(value=>{firstOutcome=value;},error=>{firstOutcome=error;});
  try{
    await flush();w.reply(w.sent.at(-1),null,false);await flush();assert.equal(firstOutcome,undefined);
    const failure={...pending,phase:'failed',error:'audio-source-gain-failed'};
    const itemPending=receipt({operation:'77777777-7777-7777-7777-777777777777',scope:'item-bgm-gain',
      itemId:'88888888-8888-8888-8888-888888888888',sequence:'9007199254740994'});
    let itemOutcome;
    const item=owner.wait(itemPending).then(value=>{itemOutcome=value;},error=>{itemOutcome=error;});
    const appliedItem={...itemPending,phase:'applied'};
    w.event(state([failure,itemPending]));await first;
    assert.deepEqual(firstOutcome,failure);assert.equal(itemOutcome,undefined);
    w.event(state([failure,appliedItem]));await item;assert.deepEqual(itemOutcome,appliedItem);
    const next={...pending,operation:'99999999-9999-9999-9999-999999999999',sequence:'9007199254740995'};
    const second=owner.wait(next);
    const appliedNext={...next,phase:'applied'};
    w.event(state([appliedNext,appliedItem]));assert.deepEqual(await second,appliedNext);
  }finally{owner.release();}
});
await check('page replacement observes current native truth without replay; old-page envelopes cannot settle new waits',async()=>{
  const w=wire(),owner=ProductionAudioEffects.retain(w,generation,session);
  let ready=owner.ready();w.reply(w.sent[0],state());await ready;
  const retired=assert.rejects(owner.wait(receipt()),/disposed/);
  owner.release();await retired;
  const newGeneration='bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb';
  const next=ProductionAudioEffects.retain(w,newGeneration,session);
  const beforeReload=receipt({phase:'applied'});
  ready=next.ready();w.reply(w.sent.at(-1),state([beforeReload]));await ready;
  assert.deepEqual(next.state.effects,[beforeReload]);
  const admitted=receipt({operation:'66666666-6666-6666-6666-666666666666',sequence:'9007199254740994'});
  let outcome;
  const waiting=next.wait(admitted).then(value=>{outcome=value;},error=>{outcome=error;});
  try{
    await flush();assert.equal(outcome,undefined);
    const read=w.sent.at(-1);assert.equal(read.generation,newGeneration);w.reply(read,state([admitted]));
    const matching={...admitted,phase:'applied'};
    w.event(state([matching]));await flush();assert.equal(outcome,undefined);
    w.event(state([matching]),{generation:newGeneration});await waiting;
    assert.deepEqual(outcome,matching);
    assert.deepEqual(w.sent.map(command=>command.type),['NegotiateAudioEffects','NegotiateAudioEffects','GetAudioEffects']);
  }finally{next.release();}
});
const report={suite:'CP1 native audio effect transport and unsent scalar interactions',checks:checks.length,passed:checks.filter(c=>c.pass).length,results:checks};report.status=report.passed===report.checks?'PASS':'FAIL';console.log(JSON.stringify(report,null,2));if(report.status!=='PASS')process.exitCode=1;
