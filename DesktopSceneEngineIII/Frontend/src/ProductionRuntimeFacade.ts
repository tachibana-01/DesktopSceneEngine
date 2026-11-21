import type {SessionTransport} from './ProductionSessionFacade.ts';
import {sessionUuid} from './ProductionSessionTypes.ts';
import {uint64Revision} from './ProductionSessionIdentity.ts';
import {ProductionAudioEffects,isAudioEffectReceipt,type AudioEffectReceipt} from './ProductionAudioEffects.ts';

export interface RuntimePlaybackSample {
  bindingToken:string;
  asOfEffectSequence:string;asOfTimeNs:string;
  active:{playlistId:string|null;itemId:string|null};
  hasMedia:boolean;state:'playing'|'paused'|'unavailable';
  position:number;duration:number;actualRate:number;outputMuted:boolean;
  canPlayPause:boolean;canPrevious:boolean;canNext:boolean;canSeek:boolean;canMute:boolean;
  sceneIndex:number;sceneCount:number;mediaKind:'image'|'video'|null;
}
export type RuntimeIntent=
  |{type:'PlayPlaylist';payload:{playlistId:string}}
  |{type:'PlayItem';payload:{playlistId:string;itemId:string}}
  |{type:'PlayPause'|'Previous'|'Next'|'ToggleOutputMute';payload:Record<string,never>}
  |{type:'Seek';payload:{bindingToken:string;itemId:string;duration:number;value:number}};
export type RuntimeSeekTarget=Extract<RuntimeIntent,{type:'Seek'}>['payload'];
export interface RuntimeResult {status:string;message:string;sample:RuntimePlaybackSample;audioEffect?:AudioEffectReceipt}

// Native monotonic observations can reach the page through a command reply or
// the coalesced event relay. Arrival order alone does not identify a newer clock.
export function compareRuntimeSamples(left:RuntimePlaybackSample,right:RuntimePlaybackSample){
  const time=BigInt(left.asOfTimeNs)-BigInt(right.asOfTimeNs);
  const order=time||BigInt(left.asOfEffectSequence)-BigInt(right.asOfEffectSequence);
  return order<0n?-1:order>0n?1:0;
}
export interface NativeTimelineHoverRequest {bindingToken:string;itemId:string;serial:number;time:number}
export interface NativeTimelineHoverPresentation extends NativeTimelineHoverRequest {
  rect:{x:number;y:number;width:number;height:number};
  lane:{x:number;y:number;width:number;height:number};
  imageSize:{width:number;height:number};
  viewport:{width:number;height:number};
  rasterizationScale:number;
  timeLabel:string;
}
export interface NativeTimelineHoverResult extends NativeTimelineHoverRequest {
  status:'ready'|'unavailable'|'decode-failed'|'cancelled'|'stale';
}
type HoverAck={status:'pending'|'stale'|'unavailable'|'cancelled';serial:number};
type HoverPresentationAck={status:'shown'|'stale'|'unavailable';serial:number};
type Pending={type:string;eventVersion:number;resolve:(value:unknown)=>void;reject:(error:Error)=>void;timer:ReturnType<typeof setTimeout>};
const record=(value:unknown):value is Record<string,unknown>=>value!==null&&typeof value==='object'&&!Array.isArray(value);
const exact=(value:Record<string,unknown>,keys:string[])=>Object.keys(value).length===keys.length&&keys.every(key=>Object.hasOwn(value,key));
const finite=(value:unknown):value is number=>typeof value==='number'&&Number.isFinite(value);
const boundedText=(value:unknown,max:number):value is string=>typeof value==='string'&&value.length<=max;
const monotonicNs=(value:unknown):value is string=>typeof value==='string'&&/^(0|-?[1-9][0-9]{0,18})$/.test(value)&&BigInt(value)>=-9223372036854775808n&&BigInt(value)<=9223372036854775807n;
export const runtimeHoverSerial=(value:unknown):value is number=>Number.isInteger(value)&&Number(value)>=0&&Number(value)<=2147483647;
export function isRuntimePlaybackSample(value:unknown):value is RuntimePlaybackSample {
  if(!record(value)||!exact(value,['bindingToken','asOfEffectSequence','asOfTimeNs','active','hasMedia','state','position','duration','actualRate','outputMuted','canPlayPause','canPrevious','canNext','canSeek','canMute','sceneIndex','sceneCount','mediaKind']))return false;
  return boundedText(value.bindingToken,256)&&uint64Revision(value.asOfEffectSequence)&&monotonicNs(value.asOfTimeNs)&&record(value.active)&&exact(value.active,['playlistId','itemId'])
    &&[value.active.playlistId,value.active.itemId].every(id=>id===null||sessionUuid(id))
    &&typeof value.hasMedia==='boolean'&&['playing','paused','unavailable'].includes(String(value.state))
    &&[value.position,value.duration,value.actualRate].every(finite)
    &&['outputMuted','canPlayPause','canPrevious','canNext','canSeek','canMute'].every(key=>typeof value[key]==='boolean')
    &&Number.isInteger(value.sceneIndex)&&Number(value.sceneIndex)>=-1&&Number.isInteger(value.sceneCount)&&Number(value.sceneCount)>=0
    &&(value.mediaKind===null||value.mediaKind==='image'||value.mediaKind==='video');
}
function isRuntimeResult(value:unknown):value is RuntimeResult {
  return record(value)&&exact(value,['status','message','sample',...(Object.hasOwn(value,'audioEffect')?['audioEffect']:[])])&&boundedText(value.status,128)&&boundedText(value.message,4096)&&isRuntimePlaybackSample(value.sample)
    &&(value.audioEffect===undefined||isAudioEffectReceipt(value.audioEffect));
}
function isHoverAck(value:unknown):value is HoverAck {
  return record(value)&&exact(value,['status','serial'])&&['pending','stale','unavailable','cancelled'].includes(String(value.status))&&runtimeHoverSerial(value.serial);
}
function isHoverPresentationAck(value:unknown):value is HoverPresentationAck {
  return record(value)&&exact(value,['status','serial'])&&['shown','stale','unavailable'].includes(String(value.status))&&runtimeHoverSerial(value.serial);
}
function isHoverResult(value:unknown):value is NativeTimelineHoverResult {
  return record(value)&&exact(value,['status','bindingToken','itemId','serial','time'])&&boundedText(value.bindingToken,256)
    &&sessionUuid(value.itemId)&&runtimeHoverSerial(value.serial)&&value.serial>0&&finite(value.time)
    &&['ready','unavailable','decode-failed','cancelled','stale'].includes(String(value.status));
}

// One page-scoped wire boundary. It projects native samples and sends intents;
// it has no playback clock, successor policy, optimistic mute or seek state.
export class ProductionRuntimeFacade {
  readonly audioEffects:ProductionAudioEffects;
  private readonly bridge:SessionTransport;
  readonly generation:string;
  readonly incarnation:string;
  private disposed=false;
  private eventVersion=0;
  private hoverSerial=0;
  private startPromise:Promise<RuntimePlaybackSample>|null=null;
  private readonly pending=new Map<string,Pending>();
  private readonly samples=new Set<(sample:RuntimePlaybackSample)=>void>();
  private readonly hoverResults=new Set<(result:NativeTimelineHoverResult)=>void>();
  private latest:RuntimePlaybackSample|null=null;
  private readonly receive=(event:MessageEvent<unknown>)=>{
    const value=event.data;
    if(this.disposed||!record(value)||value.generation!==this.generation||value.sessionIncarnation!==this.incarnation)return;
    if(value.kind==='event'){
      if(value.version!==1||!exact(value,['kind','version','type','generation','sessionIncarnation','payload']))return;
      if(value.type==='RuntimePlaybackSample'&&isRuntimePlaybackSample(value.payload)){
        this.publish(value.payload,true);
      }else if(value.type==='TimelineHoverResult'&&isHoverResult(value.payload)){
        for(const listener of this.hoverResults)listener(value.payload);
      }
      return;
    }
    if(value.kind!=='result'||typeof value.requestId!=='string')return;
    const pending=this.pending.get(value.requestId);if(!pending)return;
    this.pending.delete(value.requestId);clearTimeout(pending.timer);
    if(value.version!==1||value.type!==pending.type||typeof value.ok!=='boolean'
      ||!exact(value,['kind','version','requestId','type','generation','sessionIncarnation','ok',value.ok?'payload':'error'])){
      pending.reject(new Error('Invalid native runtime result envelope.'));return;
    }
    if(!value.ok){pending.reject(new Error('The native runtime command was rejected by the Session bridge.'));return;}
    if(JSON.stringify(value).length>16384){pending.reject(new Error('Native runtime result exceeds the bounded contract.'));return;}
    const payload=value.payload;
    if(pending.type==='GetRuntimePlaybackSample'&&isRuntimePlaybackSample(payload)){
      this.publishReply(payload,pending.eventVersion);
    }else if(['RequestTimelineHover','CancelTimelineHover'].includes(pending.type)&&isHoverAck(payload)){
      // Decode delivery state only. The native popup owns the actual frame.
    }else if(pending.type==='PresentTimelineHover'&&isHoverPresentationAck(payload)){
      // Presentation acknowledgement never becomes authored/runtime truth.
    }else if(isRuntimeResult(payload)&&!['GetRuntimePlaybackSample','RequestTimelineHover','CancelTimelineHover','PresentTimelineHover'].includes(pending.type)){
      this.publishReply(payload.sample,pending.eventVersion);
    }else{pending.reject(new Error('Invalid native runtime result.'));return;}
    pending.resolve(payload);
  };
  constructor(bridge:SessionTransport|undefined,generation:string,incarnation:string){
    if(!bridge||!sessionUuid(generation)||!sessionUuid(incarnation)||[generation,incarnation].includes('00000000-0000-0000-0000-000000000000'))throw new Error('The production runtime page context is unavailable.');
    this.bridge=bridge;this.generation=generation;this.incarnation=incarnation;bridge.addEventListener('message',this.receive);
    this.audioEffects=ProductionAudioEffects.retain(bridge,generation,incarnation);
  }
  get sample(){return this.latest;}
  get currentHoverSerial(){return this.hoverSerial;}
  nextHoverSerial():number|null {
    if(this.disposed||this.hoverSerial>=2147483647)return null;
    return ++this.hoverSerial;
  }
  private publishReply(sample:RuntimePlaybackSample,eventVersion:number){
    // A native event wins a tie when it arrived during the request. A strictly
    // newer reply can still carry the first post-commit Seek observation.
    if(eventVersion===this.eventVersion||!this.latest||compareRuntimeSamples(sample,this.latest)>0)this.publish(sample);
  }
  private publish(sample:RuntimePlaybackSample,fromEvent=false){
    if(this.latest&&compareRuntimeSamples(sample,this.latest)<0)return;
    if(fromEvent)++this.eventVersion;
    this.latest=sample;
    for(const listener of this.samples)listener(sample);
  }
  subscribe(listener:(sample:RuntimePlaybackSample)=>void){this.samples.add(listener);return()=>{this.samples.delete(listener);};}
  subscribeHover(listener:(result:NativeTimelineHoverResult)=>void){this.hoverResults.add(listener);return()=>{this.hoverResults.delete(listener);};}
  start():Promise<RuntimePlaybackSample>{
    return this.startPromise??=this.request('GetRuntimePlaybackSample',{}) as Promise<RuntimePlaybackSample>;
  }
  async send(intent:RuntimeIntent,expectedDocument?:string,onAdmitted?:(receipt:AudioEffectReceipt)=>void):Promise<RuntimeResult>{
    const document=this.audioEffects.captureDocument(expectedDocument);
    await this.audioEffects.ready();
    this.audioEffects.captureDocument(document);
    const result=await this.request(intent.type,intent.payload,document) as RuntimeResult;
    if(result.audioEffect)onAdmitted?.(result.audioEffect);
    if(!result.audioEffect||result.status!=='pending')return result;
    const receipt=await this.audioEffects.wait(result.audioEffect);
    const sample=await this.request('GetRuntimePlaybackSample',{}) as RuntimePlaybackSample;
    return {...result,audioEffect:receipt,status:receipt.phase==='applied'?'applied':receipt.phase,sample};
  }
  requestHover(request:NativeTimelineHoverRequest):Promise<HoverAck>{
    if(runtimeHoverSerial(request.serial))this.hoverSerial=Math.max(this.hoverSerial,request.serial);
    return this.request('RequestTimelineHover',request) as Promise<HoverAck>;
  }
  presentHover(presentation:NativeTimelineHoverPresentation):Promise<HoverPresentationAck>{
    if(runtimeHoverSerial(presentation.serial))this.hoverSerial=Math.max(this.hoverSerial,presentation.serial);
    return this.request('PresentTimelineHover',presentation) as Promise<HoverPresentationAck>;
  }
  cancelHover(serial:number):Promise<HoverAck>{
    if(runtimeHoverSerial(serial))this.hoverSerial=Math.max(this.hoverSerial,serial);
    return this.request('CancelTimelineHover',{serial}) as Promise<HoverAck>;
  }
  private request(type:string,payload:unknown,audioDocument?:string):Promise<unknown>{
    if(this.disposed)return Promise.reject(new Error('The runtime page has been disposed.'));
    return new Promise((resolve,reject)=>{
      const requestId=crypto.randomUUID(),timer=setTimeout(()=>{this.pending.delete(requestId);reject(new Error('Native runtime result is unavailable; the outcome is unknown.'));},10000);
      this.pending.set(requestId,{type,eventVersion:this.eventVersion,resolve,reject,timer});
      try{this.bridge.postMessage({kind:'command',version:1,requestId,type,generation:this.generation,sessionIncarnation:this.incarnation,payload,...(audioDocument?{audioEffects:{version:1,documentIncarnation:audioDocument}}:{})});}
      catch{this.pending.delete(requestId);clearTimeout(timer);reject(new Error('Unable to deliver the native runtime intent.'));}
    });
  }
  dispose(){
    if(this.disposed)return;this.disposed=true;this.bridge.removeEventListener('message',this.receive);this.samples.clear();this.hoverResults.clear();
    this.audioEffects.release();
    for(const pending of this.pending.values()){clearTimeout(pending.timer);pending.reject(new Error('The runtime page has been disposed.'));}this.pending.clear();
  }
}
