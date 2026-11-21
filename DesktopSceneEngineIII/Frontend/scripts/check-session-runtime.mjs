import assert from 'node:assert/strict';
import {readFileSync,mkdirSync,writeFileSync,existsSync} from 'node:fs';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {ProductionRuntimeFacade,isRuntimePlaybackSample,runtimeHoverSerial} from '../src/ProductionRuntimeFacade.ts';
import {runtimeSessionProjection} from '../src/ProductionRuntimeProjection.ts';
import {timelineValueForTime,timeForValue,valueForGroovePosition} from '../src/sessionTimelineGeometry.ts';
import {timelineHoverPlacement,canonicalTimelineHoverSize} from '../src/timelineHoverPlacement.ts';
import {generation,incarnation,ids,snapshot} from './session-readonly-fixture.mjs';

const sample=(patch={})=>({bindingToken:'binding-1',asOfEffectSequence:'9007199254740993',asOfTimeNs:'9007199254740995',active:{playlistId:ids.a,itemId:ids.image},hasMedia:true,state:'paused',position:3.5,duration:12.75,actualRate:1.1,outputMuted:true,canPlayPause:true,canPrevious:false,canNext:true,canSeek:true,canMute:true,sceneIndex:0,sceneCount:1,mediaKind:'image',...patch});
const documentIncarnation='30000000-0000-0000-0000-000000000001';
const audioState=()=>({version:1,documentIncarnation,execution:'1',capacityRevision:'1',closing:false,stopped:false,desiredMuted:true,appliedMuted:true,muteKnown:true,deviceHealth:'healthy',effects:[]});
const flush=async()=>{for(let n=0;n<12;n++)await Promise.resolve();};
const checks=[];async function check(name,fn){try{await fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.stack??String(error)});}}
function bridge(){const listeners=new Set(),retired=[];return{sent:[],listeners,retired,postMessage(command){this.sent.push(command);},addEventListener(_,fn){assert(!listeners.has(fn));listeners.add(fn);},removeEventListener(_,fn){assert(listeners.delete(fn));retired.push(fn);},receive(value){for(const fn of listeners)fn({data:value});},late(value){for(const fn of retired)fn({data:value});}};}
const result=(command,payload,patch={})=>({kind:'result',version:1,requestId:command.requestId,type:command.type,generation,sessionIncarnation:incarnation,ok:true,payload,...patch});
const event=(type,payload,patch={})=>({kind:'event',version:1,type,generation,sessionIncarnation:incarnation,payload,...patch});
const mutation=(patch={})=>({status:'applied',message:'',sample:sample(),...patch});
const hover=(patch={})=>({status:'ready',bindingToken:'binding-1',itemId:ids.image,serial:1,time:3,...patch});
async function prepareMutations(facade,wire){
 facade.audioEffects.seedNativeSnapshot(audioState());
 const ready=facade.audioEffects.ready(),command=wire.sent.at(-1);
 assert.deepEqual({...command,requestId:'id'},{kind:'command',version:1,requestId:'id',type:'NegotiateAudioEffects',generation,sessionIncarnation:incarnation,payload:{version:1}});
 wire.receive(result(command,audioState()));await ready;
}
await check('Runtime sample is exact, bounded and finite with native eligibility independent of selection',()=>{
 assert(isRuntimePlaybackSample(sample()));assert(isRuntimePlaybackSample(sample({hasMedia:false,state:'unavailable',active:{playlistId:null,itemId:null},duration:0,position:0,mediaKind:null,sceneCount:0,sceneIndex:-1,canPlayPause:false,canSeek:false,canMute:true})));
 for(const patch of [{position:NaN},{duration:Infinity},{actualRate:NaN},{active:{playlistId:'alias',itemId:ids.image}},{bindingToken:123},{canSeek:1},{state:'stopped'},{sceneCount:1.5},{authoredRevision:'4'},{image:'base64'}])assert(!isRuntimePlaybackSample(sample(patch)),JSON.stringify(patch));
 assert(runtimeHoverSerial(0));assert(runtimeHoverSerial(2147483647));for(const value of [-1,2147483648,1.5,Infinity,'1'])assert(!runtimeHoverSerial(value));
});
await check('Runtime projection preserves authored snapshot, selection, Master, configured rate and dirty; active and actual rate use only sample',()=>{
 const authored=snapshot(),original=structuredClone(authored),native=sample({actualRate:1.75,position:8.125,outputMuted:false});
 const projection=runtimeSessionProjection(authored,native);assert.deepEqual(authored,original);assert.equal(projection.selectedItemId,ids.video);assert.equal(projection.activeItemId,ids.image);assert.equal(projection.runtime.rate,1.75);assert.equal(projection.runtime.time,8.125);assert.equal(projection.project.playlists[1].items[0].rate,1.25);assert.equal(projection.project.playlists[1].masterVolume,.4);assert.equal(projection.outputMuted,false);assert.equal(projection.runtime.sceneCount,1);
 const awaiting=runtimeSessionProjection(authored,null);assert.equal(awaiting.runtime,null);assert.equal(awaiting.activeItemId,null,'B4 never falls back to an old editor runtime snapshot');assert.equal(awaiting.selectedItemId,ids.video);
});
await check('Accepted integer groove geometry preserves exact endpoint and float scene-time rendering',()=>{
 assert.equal(valueForGroovePosition(109,10,100),10000);assert.equal(valueForGroovePosition(108,10,100),9899);assert.equal(valueForGroovePosition(-1,10,100),0);assert.equal(timeForValue(10000,12.75),12.75);assert(timeForValue(9999,12.75)<12.75);assert.equal(timelineValueForTime(6.375,12.75),5000);
});
await check('Only explicit start subscribes, repeated start shares one request, ordinary creation has no runtime mutation or replay',async()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation);assert.deepEqual(wire.sent,[]);
 const first=facade.start(),second=facade.start();assert.equal(first,second);assert.equal(wire.sent.length,1);assert.deepEqual({...wire.sent[0],requestId:'id'},{kind:'command',version:1,requestId:'id',type:'GetRuntimePlaybackSample',generation,sessionIncarnation:incarnation,payload:{}});wire.receive(result(wire.sent[0],sample()));assert.deepEqual(await first,sample());assert.deepEqual(facade.sample,sample());facade.dispose();
});
await check('Every runtime intent sends stable targets and only relevant fences, with no authored revision',async()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation),samples=[];facade.subscribe(value=>samples.push(value));
 await prepareMutations(facade,wire);
 const intents=[{type:'PlayPlaylist',payload:{playlistId:ids.a}},{type:'PlayItem',payload:{playlistId:ids.a,itemId:ids.image}},...['PlayPause','Previous','Next','ToggleOutputMute'].map(type=>({type,payload:{}})),... [9999,10000].map(value=>({type:'Seek',payload:{bindingToken:'binding-1',itemId:ids.image,duration:12.75,value}}))];
 for(const intent of intents){const pending=facade.send(intent);await flush();const command=wire.sent.at(-1);assert.deepEqual({...command,requestId:'id'},{kind:'command',version:1,requestId:'id',...intent,generation,sessionIncarnation:incarnation,audioEffects:{version:1,documentIncarnation}});wire.receive(result(command,mutation()));assert.deepEqual(await pending,mutation());}
 assert.equal(samples.length,intents.length);facade.dispose();
});
await check('Mute, seek failure and rejected commands never fabricate state before native sample/result',async()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation);await prepareMutations(facade,wire);wire.receive(event('RuntimePlaybackSample',sample()));const original=facade.sample;
 for(const type of ['ToggleOutputMute','Seek']){const pending=facade.send(type==='Seek'?{type,payload:{bindingToken:'binding-1',itemId:ids.image,duration:12.75,value:5000}}:{type,payload:{}});assert.deepEqual(facade.sample,original);await flush();wire.receive(result(wire.sent.at(-1),mutation({status:'engine-failed',message:'Engine operation failed.'})));assert.equal((await pending).status,'engine-failed');assert.deepEqual(facade.sample,original);}
 facade.dispose();
});
await check('Wrong page/Session/type/shape and oversized result fail or remain fenced',async()=>{
 for(const patch of [{generation:crypto.randomUUID()},{sessionIncarnation:crypto.randomUUID()},{requestId:crypto.randomUUID()}]){
  const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation);let settled=false;const pending=facade.start().then(()=>{settled=true;});wire.receive(result(wire.sent[0],sample(),patch));await Promise.resolve();assert(!settled);assert.equal(facade.sample,null);wire.receive(result(wire.sent[0],sample()));await pending;facade.dispose();
 }
 for(const patch of [{type:'Next'},{version:2},{extra:true},{payload:{success:true}},{payload:mutation({message:'x'.repeat(18000)})}]){
  const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation);await prepareMutations(facade,wire);const pending=facade.send({type:'PlayPause',payload:{}});await flush();wire.receive(result(wire.sent.at(-1),mutation(),patch));await assert.rejects(pending,/Invalid native runtime|bounded contract/);assert.equal(facade.sample,null);facade.dispose();
 }
});
await check('High cadence samples use only event projection; malformed/old-page events are ignored',()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation),received=[];facade.subscribe(value=>received.push(value));for(let n=0;n<1000;n++)wire.receive(event('RuntimePlaybackSample',sample({position:n/10})));assert.equal(received.length,1000);assert.equal(facade.sample.position,99.9);assert.deepEqual(wire.sent,[]);
 for(const patch of [{generation:crypto.randomUUID()},{sessionIncarnation:crypto.randomUUID()},{version:2},{extra:true},{payload:sample({thumbnail:'fake'})}])wire.receive(event('RuntimePlaybackSample',sample({position:100}),patch));assert.equal(received.length,1000);facade.dispose();
});
await check('Late command sample cannot overwrite a newer native event, including same Item rebind',async()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation);await prepareMutations(facade,wire);const pending=facade.send({type:'PlayPause',payload:{}});await flush();const command=wire.sent.at(-1);wire.receive(event('RuntimePlaybackSample',sample({bindingToken:'binding-2',position:0})));wire.receive(result(command,mutation()));await pending;assert.equal(facade.sample.bindingToken,'binding-2');assert.equal(facade.sample.position,0);facade.dispose();
});
await check('Newer native Seek reply outranks an in-flight event; delayed observations cannot regress the native clock',async()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation);await prepareMutations(facade,wire);
 const samples=[];facade.subscribe(value=>samples.push(value.position));
 wire.receive(event('RuntimePlaybackSample',sample({position:3})));
 const seeking=facade.send({type:'Seek',payload:{bindingToken:'binding-1',itemId:ids.image,duration:12.75,value:5000}});await flush();const command=wire.sent.at(-1);
 wire.receive(event('RuntimePlaybackSample',sample({position:3.2,asOfTimeNs:'9007199254740996'})));
 wire.receive(result(command,mutation({sample:sample({position:6.375,asOfTimeNs:'9007199254740999'})})));
 assert.equal((await seeking).sample.position,6.375);
 wire.receive(event('RuntimePlaybackSample',sample({position:3.3,asOfTimeNs:'9007199254740997'})));
 assert.equal(facade.sample.position,6.375);
 wire.receive(event('RuntimePlaybackSample',sample({position:1,asOfTimeNs:'9007199254741000'})));
 assert.deepEqual(samples,[3,3.2,6.375,1],'Native observation order, not the numeric position, owns the playback display');
 facade.dispose();
});
await check('Pending Seek is not terminal until its exact applied receipt and causal native read settle',async()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation);await prepareMutations(facade,wire);
 const base=sample({position:3});wire.receive(event('RuntimePlaybackSample',base));
 let settled=false;
 const seeking=facade.send({type:'Seek',payload:{bindingToken:base.bindingToken,itemId:ids.image,duration:base.duration,value:5000}})
  .then(value=>{settled=true;return value;});
 await flush();
 const command=wire.sent.at(-1),receipt={operation:crypto.randomUUID(),documentIncarnation,bindingToken:base.bindingToken,
  execution:'1',sequence:'8',scope:'structural',phase:'pending',error:'',effectTimeNs:'0',playlistId:ids.a,itemId:ids.image};
 wire.receive(result(command,mutation({status:'pending',sample:base,audioEffect:receipt})));await flush();
 assert.equal(wire.sent.at(-1).type,'GetAudioEffects');
 wire.receive(result(wire.sent.at(-1),audioState()));await flush();assert.equal(settled,false);
 const applied={...receipt,phase:'applied',effectTimeNs:'9007199254740999'};
 wire.receive(event('AudioEffectsChanged',{...audioState(),effects:[applied]}));await flush();
 assert.equal(wire.sent.at(-1).type,'GetRuntimePlaybackSample');
 assert.equal(settled,false,'A terminal effect alone is not the timeline observation');
 wire.receive(event('RuntimePlaybackSample',sample({position:3.2,asOfTimeNs:'9007199254740998'})));
 const confirmed=sample({position:6.375,asOfTimeNs:'9007199254741001',asOfEffectSequence:'9007199254740999'});
 wire.receive(result(wire.sent.at(-1),confirmed));
 assert.deepEqual(await seeking,{status:'applied',message:'',audioEffect:applied,sample:confirmed});
 assert.deepEqual(facade.sample,confirmed);
 facade.dispose();
});
await check('Hover events are metadata-only; retired PNG/pixel deliveries and wrong pages are rejected',async()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation),received=[];facade.subscribeHover(value=>received.push(value));
 const request={bindingToken:'binding-1',itemId:ids.image,serial:1,time:3},pending=facade.requestHover(request);assert.deepEqual(wire.sent[0].payload,request);wire.receive(result(wire.sent[0],{status:'pending',serial:1}));assert.deepEqual(await pending,{status:'pending',serial:1});assert.deepEqual(received,[]);
 for(const patch of [{assetUrl:'https://hover.dse.invalid/retired.png'},{assetUrl:null},{pixels:'base64'},{image:'raw'},{serial:0},{time:NaN},{extra:true}])wire.receive(event('TimelineHoverResult',hover(patch)));
 wire.receive(event('TimelineHoverResult',hover(),{generation:crypto.randomUUID()}));wire.receive(event('TimelineHoverResult',hover(),{sessionIncarnation:crypto.randomUUID()}));assert.deepEqual(received,[]);
 wire.receive(event('TimelineHoverResult',hover()));assert.deepEqual(received,[hover()]);for(const status of ['unavailable','cancelled','stale','decode-failed'])wire.receive(event('TimelineHoverResult',hover({status})));assert.equal(received.length,5);
 const cancel=facade.cancelHover(2);assert.deepEqual(wire.sent.at(-1).payload,{serial:2});wire.receive(result(wire.sent.at(-1),{status:'cancelled',serial:2}));await cancel;assert(wire.sent.every(c=>['RequestTimelineHover','CancelTimelineHover'].includes(c.type)),'Hover sends no seek, pause or runtime mutations');facade.dispose();
});
await check('Native presentation intent carries canonical CSS geometry and exact context without runtime or authored writes',async()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation),original=sample(),seen=[];facade.subscribe(value=>seen.push(value));wire.receive(event('RuntimePlaybackSample',original));
 const payload={bindingToken:'binding-1',itemId:ids.image,serial:1,time:3,rect:{x:300,y:468.5,width:216,height:152.5},lane:{x:235,y:612,width:643,height:35},imageSize:{width:200,height:112.5},viewport:{width:1183,height:711},rasterizationScale:1.5,timeLabel:'00:03'};
 const pending=facade.presentHover(payload),command=wire.sent[0];assert.deepEqual({...command,requestId:'id'},{kind:'command',version:1,requestId:'id',type:'PresentTimelineHover',generation,sessionIncarnation:incarnation,payload});wire.receive(result(command,{status:'shown',serial:1}));assert.deepEqual(await pending,{status:'shown',serial:1});assert.equal(facade.currentHoverSerial,1);assert.deepEqual(facade.sample,original);assert.equal(seen.length,1,'Geometry acknowledgement never publishes a runtime sample');
 for(const bad of [{status:'ready',serial:2},{status:'shown',serial:2,assetUrl:'legacy'},{status:'shown',serial:NaN},mutation()]){const request=facade.presentHover({...payload,serial:2});wire.receive(result(wire.sent.at(-1),bad));await assert.rejects(request,/Invalid native runtime result/);}
 const late=facade.presentHover({...payload,serial:3});facade.dispose();await assert.rejects(late,/disposed/);wire.late(result(wire.sent.at(-1),{status:'shown',serial:3}));assert.equal(seen.length,1);
});
await check('Fifty page recreations remove one subscription handler and settle pending work without replay or late delivery',async()=>{
 const wire=bridge();for(let n=0;n<50;n++){const facade=new ProductionRuntimeFacade(wire,generation,incarnation);let received=0;facade.subscribe(()=>received++);facade.subscribeHover(()=>received++);const start=facade.start();facade.dispose();await assert.rejects(start,/disposed/);wire.late(result(wire.sent.at(-1),sample()));wire.late(event('RuntimePlaybackSample',sample()));wire.late(event('TimelineHoverResult',hover()));assert.equal(received,0);assert.equal(wire.listeners.size,0);}assert.equal(wire.sent.length,50);assert(wire.sent.every(c=>c.type==='GetRuntimePlaybackSample'));
});
await check('Hover serial belongs to the page facade, survives view remount allocation and never wraps the bounded native integer',async()=>{
 const wire=bridge(),facade=new ProductionRuntimeFacade(wire,generation,incarnation);assert.equal(facade.nextHoverSerial(),1);assert.equal(facade.nextHoverSerial(),2);const pending=facade.cancelHover(200);wire.receive(result(wire.sent.at(-1),{status:'cancelled',serial:200}));await pending;assert.equal(facade.currentHoverSerial,200);assert.equal(facade.nextHoverSerial(),201);const end=facade.cancelHover(2147483647);wire.receive(result(wire.sent.at(-1),{status:'cancelled',serial:2147483647}));await end;assert.equal(facade.nextHoverSerial(),null);assert.equal(facade.currentHoverSerial,2147483647);facade.dispose();assert.equal(facade.nextHoverSerial(),null);
});
await check('Canonical native card dimensions are independent of pointer position and remaining DOM height',()=>{
 const bounds={left:0,top:0,right:1183,bottom:711},anchor={left:254.28125,top:629,right:787.875,bottom:631},lane={left:235,top:612,right:878,bottom:647};
 assert.deepEqual(canonicalTimelineHoverSize([16,9]),{contentWidth:200,contentHeight:112.5,width:216,height:152.5});
 let previous=-Infinity;
 for(const fraction of [.2,.3,.4,.5,.6,.7,.8]){const pointer=anchor.left+(anchor.right-anchor.left-1)*fraction,card=timelineHoverPlacement(bounds,anchor,{width:1183,height:711},pointer,[16,9],lane);assert(card);assert.equal(card.contentWidth,200);assert.equal(card.contentHeight,112.5);assert.equal(card.width,216);assert.equal(card.height,152.5);assert.equal(card.top,468.5);assert(card.left>previous);assert(Math.abs(card.left+card.width/2-pointer)<1e-8);assert(card.left>=lane.left+8&&card.left+card.width<=lane.right-8);previous=card.left;}
});
await check('Native edge clamps change position only; a physically impossible lane never shrinks or chooses side slots',()=>{
 const bounds={left:0,top:0,right:1337,bottom:810},anchor={left:263,top:728,right:933,bottom:730},lane={left:235,top:711,right:1032,bottom:746};
 for(const pointer of [-100,8,598,1329,2000])for(const aspect of [[16,9],[4,3],[1,1],[9,16],[32,9]]){const card=timelineHoverPlacement(bounds,anchor,{width:1337,height:810},pointer,aspect,lane),canonical=canonicalTimelineHoverSize(aspect);assert(card);assert.equal(card.width,canonical.width);assert.equal(card.height,canonical.height);assert(card.left>=lane.left+8&&card.left+card.width<=lane.right-8);}
 const left=timelineHoverPlacement(bounds,anchor,{width:1337,height:810},-100,[16,9],lane),right=timelineHoverPlacement(bounds,anchor,{width:1337,height:810},2000,[16,9],lane);assert.equal(left.left,lane.left+8);assert.equal(right.left+right.width,lane.right-8);
 assert.equal(timelineHoverPlacement(bounds,anchor,{width:1337,height:810},598,[16,9],{...lane,right:lane.left+200}),null);assert.equal(timelineHoverPlacement(bounds,{...anchor,top:100},{width:1337,height:810},598,[16,9],lane),null);
});
await check('Prototype DOM placement stays unchanged and malformed canonical aspect stays unavailable',()=>{
 const bounds={left:0,top:0,right:1440,bottom:900},anchor={left:0,top:750,right:800,bottom:752},viewport={width:1440,height:900};
 assert.deepEqual(timelineHoverPlacement(bounds,anchor,viewport,40,[16,9]),{left:8,top:589.5,width:216,height:152.5,contentWidth:200,contentHeight:112.5});for(const aspect of [[0,9],[16,0],[NaN,1]])assert.equal(canonicalTimelineHoverSize(aspect),null);
});
await check('Production import graph contains no playback kernel, prototype decoder, clock, domain repair, or browser media fallback',()=>{
 const root=fileURLToPath(new URL('../src/',import.meta.url)),visited=new Set();function inspect(path){if(visited.has(path))return;visited.add(path);const source=readFileSync(path,'utf8');assert(!/(?:sessionPlayback|sessionTimelineModel|sessionTimelinePreviewMock|sessionHoverComposition|sessionFixtures|useSessionPrototype|sessionEntities|sessionSequence)\.(?:ts|tsx)$/.test(path),path);assert(!/setInterval\s*\(|<video\b|new\s+VideoDecoder\b|natural-completion|localStorage|sessionStorage/.test(source),path);for(const match of source.matchAll(/(?:^|\n)import(?!\s+type\b)[^;]*?from\s*['"](\.[^'"]+)['"]/g)){const base=resolve(dirname(path),match[1]),found=[base,base+'.ts',base+'.tsx'].find(existsSync);if(found&&!found.endsWith('.css'))inspect(found);}}inspect(resolve(root,'ProductionSessionShell.tsx'));assert(visited.has(resolve(root,'ProductionRuntimeFacade.ts')));assert(visited.has(resolve(root,'ProductionRuntimeTimeline.tsx')));
});
const report={pass:checks.every(c=>c.pass),checks,proofScope:'Actual frontend transport validators, projections, lifecycle and production import graph; no claims about native Engine/sequence/decoder',red:'Canonical-size failure retained in hover-canonical-size/focused-red.log and dom-red.log before native presentation integration'};
const evidence=new URL('../node_modules/.cache/current-contract/',import.meta.url);mkdirSync(evidence,{recursive:true});writeFileSync(new URL('session-runtime-focused.json',evidence),JSON.stringify(report,null,2));for(const c of checks)console.log((c.pass?'PASS':'FAIL')+' '+c.name+(c.error?'\n'+c.error:''));console.log(checks.filter(c=>c.pass).length+'/'+checks.length);if(!report.pass)process.exitCode=1;
