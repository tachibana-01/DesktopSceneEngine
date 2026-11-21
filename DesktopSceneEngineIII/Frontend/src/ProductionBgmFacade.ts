import type {SessionTransport} from './ProductionSessionFacade.ts';
import {sessionUuid,uint64Revision} from './ProductionSessionIdentity.ts';
import {ProductionAudioEffects,isAudioEffectReceipt,audioEffectRuntime,type AudioEffectReceipt} from './ProductionAudioEffects.ts';
import type {RuntimeOutcome} from './ProductionAuthoredTypes.ts';

export type BgmMode='current-single-loop'|'list-loop'|'shuffle';
export type BgmScope='playlist'|'item';
export interface BgmTrack {id:string;alias:string;source:string;volume:number}
export interface BgmQueue {enabled:boolean;mode:BgmMode;volume:number;tracks:BgmTrack[]}
export interface BgmTarget {permit:string;scope:BgmScope;playlistId:string;itemId?:string}
export interface BgmAuthoredResult {
  audioEffect?:AudioEffectReceipt;runtimeOnApplied?:RuntimeOutcome;
  status:string;playlistId:string|null;itemId:string|null;token:string|null;
  authored:'committed'|'unchanged'|'rejected'|'not-applicable';revision:string;dirty:boolean;
  runtime:RuntimeOutcome;positionRestore:string;
  warnings:{code:string;message:string}[];sources:unknown[];
  bgmTarget?:{scope:BgmScope;playlistId:string;itemId?:string};bgmTrackId?:string;
  bgmReadback?:{enabled?:boolean;mode?:BgmMode;queueVolume?:number;trackVolume?:number;order?:string[]};
  bgmImport?:'completed'|'cancelled'|'rejected-before-picker'|'rejected-at-completion';
  bgmSources?:{source:string;trackId:string|null;authored:'committed'|'unsupported'|'rejected'}[];
}
export interface BgmSnapshot {
  permit:string;documentIncarnation:string;playlist:{id:string;alias:string};items:{id:string;alias:string}[];
  selectedItemId:string|null;playlistQueue:BgmQueue;itemQueue:BgmQueue|null;revision:string;dirty:boolean;
  language:'en'|'zh_CN';busy:boolean;
  pending:null|{token:string;scope:BgmScope;itemId:string|null};lastImport:BgmAuthoredResult|null;
}
export interface BgmHeader {revision:string;dirty:boolean;language:'en'|'zh_CN';playlistAlias:string;busy:boolean;pending:string|null;lastImportToken:string|null}
export interface BgmRuntimeState {
  scope:BgmScope;playlistId:string;itemId:string|null;activePlaylistId:string|null;activeItemId:string|null;
  playbackState:'stopped'|'playing'|'paused';active:boolean;exists:boolean;
  phase:null|'silent-empty-queue'|'pending-start'|'playing'|'no-playable-track'|'waiting-for-executor';
  currentTrackId:string|null;switchEligible:boolean;lastFailedAttempt:null|{trackId:string;stage:string;code:string};
}
export interface BgmRuntimeResult {status:'rejected'|'ready'|'no-change'|'admitted';reason:string;state:BgmRuntimeState|null;audioEffect?:AudioEffectReceipt}
export type BgmEdit=
  |{type:'BgmSetEnabled';enabled:boolean}
  |{type:'BgmSetQueueMode';mode:BgmMode}
  |{type:'BgmSetQueueVolume';value:number}
  |{type:'BgmSetTrackVolume';trackId:string;value:number}
  |{type:'BgmMoveTrack';trackId:string;direction:-1|1}
  |{type:'BgmRemoveTrack';trackId:string}
  |{type:'BgmAddTracks'};

const record=(v:unknown):v is Record<string,unknown>=>v!==null&&typeof v==='object'&&!Array.isArray(v);
const exact=(v:Record<string,unknown>,keys:string[])=>Object.keys(v).length===keys.length&&keys.every(k=>Object.hasOwn(v,k));
const one=(v:unknown,values:readonly unknown[])=>values.includes(v);
const text=(v:unknown):v is string=>typeof v==='string'&&v.length<=4*1024*1024;
const id=(v:unknown):v is string=>sessionUuid(v)&&v!=='00000000-0000-0000-0000-000000000000';
const nullableId=(v:unknown)=>v===null||id(v);
const gain=(v:unknown):v is number=>typeof v==='number'&&Number.isFinite(v)&&v>=0&&v<=1;
const mode=(v:unknown)=>one(v,['current-single-loop','list-loop','shuffle']);
function queue(v:unknown):v is BgmQueue {
  return record(v)&&exact(v,['enabled','mode','volume','tracks'])&&typeof v.enabled==='boolean'&&mode(v.mode)&&gain(v.volume)
    &&Array.isArray(v.tracks)&&v.tracks.every(t=>record(t)&&exact(t,['id','alias','source','volume'])&&id(t.id)&&text(t.alias)&&text(t.source)&&gain(t.volume))
    &&new Set(v.tracks.map(t=>t.id)).size===v.tracks.length;
}
export function isBgmAuthoredResult(v:unknown):v is BgmAuthoredResult {
  if(!record(v))return false;
  const required=['status','playlistId','itemId','token','authored','revision','dirty','runtime','positionRestore','warnings','sources'];
  const optional=['bgmTarget','bgmTrackId','bgmReadback','bgmImport','bgmSources','audioEffect','runtimeOnApplied'];
  if(required.some(k=>!Object.hasOwn(v,k))||Object.keys(v).some(k=>![...required,...optional].includes(k)))return false;
  if(!one(v.status,['accepted','applied','no-change','not-found','wrong-parent','invalid-value','stale-value','stale-revision','busy','picker-cancelled','unavailable','revoked-permit','wrong-document','track-not-found'])
    ||![v.playlistId,v.itemId,v.token].every(nullableId)||!one(v.authored,['committed','unchanged','rejected','not-applicable'])||!uint64Revision(v.revision)||typeof v.dirty!=='boolean'
    ||!one(v.runtime,['not-required','synchronized','unchanged','stopped','warning','failed','pending','cancelled','deferred-unavailable'])||!one(v.positionRestore,['not-required','restored','rebound-at-start','failed'])
    ||!Array.isArray(v.warnings)||!v.warnings.every(w=>record(w)&&exact(w,['code','message'])&&text(w.code)&&text(w.message))||!Array.isArray(v.sources)||v.sources.length!==0)return false;
  if(v.audioEffect!==undefined&&!isAudioEffectReceipt(v.audioEffect)||v.runtimeOnApplied!==undefined&&!one(v.runtimeOnApplied,['not-required','synchronized','unchanged','stopped','warning','failed']))return false;
  if(v.bgmTarget!==undefined&&(!record(v.bgmTarget)||!one(v.bgmTarget.scope,['playlist','item'])||!id(v.bgmTarget.playlistId)
    ||!exact(v.bgmTarget,v.bgmTarget.scope==='item'?['scope','playlistId','itemId']:['scope','playlistId'])||v.bgmTarget.scope==='item'&&!id(v.bgmTarget.itemId)))return false;
  if(v.bgmTrackId!==undefined&&!id(v.bgmTrackId))return false;
  if(v.bgmImport!==undefined&&!one(v.bgmImport,['completed','cancelled','rejected-before-picker','rejected-at-completion']))return false;
  if(v.bgmSources!==undefined&&(!Array.isArray(v.bgmSources)||!v.bgmSources.every(s=>record(s)&&exact(s,['source','trackId','authored'])&&text(s.source)&&nullableId(s.trackId)&&one(s.authored,['committed','unsupported','rejected']))))return false;
  if(v.bgmReadback!==undefined){const r=v.bgmReadback;if(!record(r)||Object.keys(r).some(k=>!['enabled','mode','queueVolume','trackVolume','order'].includes(k))
    ||r.enabled!==undefined&&typeof r.enabled!=='boolean'||r.mode!==undefined&&!mode(r.mode)||r.queueVolume!==undefined&&!gain(r.queueVolume)||r.trackVolume!==undefined&&!gain(r.trackVolume)
    ||r.order!==undefined&&(!Array.isArray(r.order)||!r.order.every(id)||new Set(r.order).size!==r.order.length))return false;}
  return true;
}
export function isBgmSnapshot(v:unknown):v is BgmSnapshot {
  if(!record(v)||!exact(v,['permit','documentIncarnation','playlist','items','selectedItemId','playlistQueue','itemQueue','revision','dirty','language','busy','pending','lastImport']))return false;
  return id(v.permit)&&id(v.documentIncarnation)&&record(v.playlist)&&exact(v.playlist,['id','alias'])&&id(v.playlist.id)&&text(v.playlist.alias)
    &&Array.isArray(v.items)&&v.items.every(i=>record(i)&&exact(i,['id','alias'])&&id(i.id)&&text(i.alias))&&new Set(v.items.map(i=>i.id)).size===v.items.length
    &&nullableId(v.selectedItemId)&&(v.selectedItemId===null?v.itemQueue===null:v.items.some(i=>i.id===v.selectedItemId)&&queue(v.itemQueue))
    &&queue(v.playlistQueue)&&uint64Revision(v.revision)&&typeof v.dirty==='boolean'&&one(v.language,['en','zh_CN'])&&typeof v.busy==='boolean'
    &&(v.pending===null||record(v.pending)&&exact(v.pending,['token','scope','itemId'])&&id(v.pending.token)&&one(v.pending.scope,['playlist','item'])&&nullableId(v.pending.itemId))
    &&(v.lastImport===null||isBgmAuthoredResult(v.lastImport));
}
export function isBgmHeader(v:unknown):v is BgmHeader {
  return record(v)&&exact(v,['revision','dirty','language','playlistAlias','busy','pending','lastImportToken'])&&uint64Revision(v.revision)&&typeof v.dirty==='boolean'
    &&one(v.language,['en','zh_CN'])&&text(v.playlistAlias)&&typeof v.busy==='boolean'&&nullableId(v.pending)&&nullableId(v.lastImportToken);
}
export function isBgmRuntimeResult(v:unknown):v is BgmRuntimeResult {
  if(!record(v)||!exact(v,['status','reason','state',...(Object.hasOwn(v,'audioEffect')?['audioEffect']:[])])||v.audioEffect!==undefined&&!isAudioEffectReceipt(v.audioEffect)||!one(v.status,['rejected','ready','no-change','admitted'])
    ||!one(v.reason,['none','unavailable','revoked-permit','busy','target-missing','wrong-parent','track-missing','disabled','inactive','not-switchable','control-rejected']))return false;
  if(v.state===null)return v.status==='rejected';
  const s=v.state;
  return record(s)&&exact(s,['scope','playlistId','itemId','activePlaylistId','activeItemId','playbackState','active','exists','phase','currentTrackId','switchEligible','lastFailedAttempt'])
    &&one(s.scope,['playlist','item'])&&id(s.playlistId)&&[s.itemId,s.activePlaylistId,s.activeItemId,s.currentTrackId].every(nullableId)
    &&(s.scope==='playlist'?s.itemId===null:id(s.itemId))&&one(s.playbackState,['stopped','playing','paused'])&&typeof s.active==='boolean'&&typeof s.exists==='boolean'
    &&one(s.phase,[null,'silent-empty-queue','pending-start','playing','no-playable-track','waiting-for-executor'])&&typeof s.switchEligible==='boolean'
    &&(s.lastFailedAttempt===null||record(s.lastFailedAttempt)&&exact(s.lastFailedAttempt,['trackId','stage','code'])&&id(s.lastFailedAttempt.trackId)
      &&one(s.lastFailedAttempt.stage,['unknown','source-open','resource-allocation','mixer-registration','playback'])
      &&one(s.lastFailedAttempt.code,['unknown','open-failure','no-audio-stream','decoder-unavailable','decode-failure','resample-failure','resource-failure','allocation-failure','registration-failure','input-failed']));
}

type Pending={type:string;resolve:(v:unknown)=>void;reject:(e:Error)=>void;timer:ReturnType<typeof setTimeout>};
// The page transports captured capabilities; native owns their issuance/lifetime.
export class ProductionBgmFacade {
  readonly audioEffects:ProductionAudioEffects;
  private readonly bridge:SessionTransport|undefined;
  readonly generation:string;
  readonly incarnation:string;
  private disposed=false;
  private readonly pending=new Map<string,Pending>();
  private readonly headers=new Set<(header:BgmHeader)=>void>();
  private readonly runtimes=new Set<(state:BgmRuntimeState)=>void>();
  private readonly receive=(event:MessageEvent<unknown>)=>{
    const v=event.data;if(this.disposed||!record(v)||v.generation!==this.generation||v.sessionIncarnation!==this.incarnation)return;
    if(v.kind==='event'){
      if(v.version!==1||!exact(v,['kind','version','type','generation','sessionIncarnation','payload']))return;
      if(v.type==='BgmEditorInvalidated'&&isBgmHeader(v.payload))for(const f of this.headers)f(v.payload);
      if(v.type==='BgmRuntimeChanged'&&isBgmRuntimeResult(v.payload)&&v.payload.state)for(const f of this.runtimes)f(v.payload.state);
      return;
    }
    if(v.kind!=='result'||typeof v.requestId!=='string')return;
    const p=this.pending.get(v.requestId);if(!p)return;this.pending.delete(v.requestId);clearTimeout(p.timer);
    if(v.version!==1||v.type!==p.type||typeof v.ok!=='boolean'||!exact(v,['kind','version','requestId','type','generation','sessionIncarnation','ok',v.ok?'payload':'error'])){p.reject(new Error('Invalid native BGM result envelope.'));return;}
    if(!v.ok){p.reject(new Error(typeof v.error==='string'?v.error:'Native BGM command unavailable.'));return;}
    if(new TextEncoder().encode(JSON.stringify(v)).length>4*1024*1024){p.reject(new Error('Native BGM result exceeds the bounded contract.'));return;}
    const payload=v.payload;
    const valid=p.type==='GetBgmEditorSnapshot'?isBgmSnapshot(payload):p.type.startsWith('Bgm')?isBgmAuthoredResult(payload)
      :p.type==='GetBgmRuntimeState'||p.type==='UseBgmTrack'?isBgmRuntimeResult(payload)
      :p.type==='OpenBackgroundMusicEditor'?record(payload)&&exact(payload,['status','message'])&&one(payload.status,['opened','unavailable'])&&text(payload.message)
      :p.type==='CloseBgmEditor'?record(payload)&&typeof payload.status==='string':false;
    if(!valid){p.reject(new Error('Invalid native BGM projection.'));return;}p.resolve(payload);
  };
  constructor(bridge:SessionTransport|undefined,generation:string,incarnation:string){
    if(!bridge||!id(generation)||!id(incarnation))throw new Error('Native BGM page context is unavailable.');
    this.bridge=bridge;this.generation=generation;this.incarnation=incarnation;
    this.audioEffects=ProductionAudioEffects.retain(bridge,generation,incarnation);
    bridge.addEventListener('message',this.receive);
  }
  subscribeHeader(f:(header:BgmHeader)=>void){this.headers.add(f);return()=>{this.headers.delete(f);};}
  subscribeRuntime(f:(state:BgmRuntimeState)=>void){this.runtimes.add(f);return()=>{this.runtimes.delete(f);};}
  async read(itemId:string|null):Promise<BgmSnapshot>{await this.audioEffects.ready();return this.request('GetBgmEditorSnapshot',{itemId}) as Promise<BgmSnapshot>;}
  async edit(target:BgmTarget,edit:BgmEdit):Promise<BgmAuthoredResult>{
    const document=this.audioEffects.captureDocument();await this.audioEffects.ready();this.audioEffects.captureDocument(document);const {type,...fields}=edit;
    const result=await this.request(type,{...target,...fields},document) as BgmAuthoredResult;
    if(!result.audioEffect)return result;
    const receipt=await this.audioEffects.wait(result.audioEffect);
    return {...result,audioEffect:receipt,runtime:result.runtime==='pending'?(receipt.phase==='applied'?result.runtimeOnApplied??'synchronized':audioEffectRuntime(receipt)):result.runtime};
  }
  runtime(target:BgmTarget):Promise<BgmRuntimeResult>{return this.request('GetBgmRuntimeState',target) as Promise<BgmRuntimeResult>;}
  async use(target:BgmTarget,trackId:string):Promise<BgmRuntimeResult>{const document=this.audioEffects.captureDocument();await this.audioEffects.ready();this.audioEffects.captureDocument(document);return this.request('UseBgmTrack',{...target,trackId},document) as Promise<BgmRuntimeResult>;}
  open(playlistId:string):Promise<{status:'opened'|'unavailable';message:string}>{return this.request('OpenBackgroundMusicEditor',{playlistId}) as Promise<{status:'opened'|'unavailable';message:string}>;}
  close(){return this.request('CloseBgmEditor',{});}
  private request(type:string,payload:unknown,audioDocument?:string):Promise<unknown>{
    if(this.disposed)return Promise.reject(new Error('The BGM page has been disposed.'));
    return new Promise((resolve,reject)=>{const requestId=crypto.randomUUID(),timer=setTimeout(()=>{this.pending.delete(requestId);reject(new Error('Native BGM outcome is unavailable; refresh to read native truth.'));},10000);
      this.pending.set(requestId,{type,resolve,reject,timer});
      try{this.bridge!.postMessage({kind:'command',version:1,requestId,type,generation:this.generation,sessionIncarnation:this.incarnation,payload,...(audioDocument?{audioEffects:{version:1,documentIncarnation:audioDocument}}:{})});}
      catch{clearTimeout(timer);this.pending.delete(requestId);reject(new Error('Unable to deliver native BGM intent.'));}
    });
  }
  dispose(){if(this.disposed)return;this.disposed=true;this.audioEffects.release();this.bridge?.removeEventListener('message',this.receive);this.headers.clear();this.runtimes.clear();for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(new Error('The BGM page has been disposed.'));}this.pending.clear();}
}

// Reply ordering metadata is not an authored revision source. Only native
// readbacks may enter a projection; older reply values cannot replace newer.
export class BgmScalarReplies {
  private sequence=0;
  private readonly sent=new Map<string,number>();
  private readonly accepted=new Map<string,bigint>();
  next(key:string){const n=++this.sequence;this.sent.set(key,n);return n;}
  accepts(key:string,request:number,revision:string,hydratedRevision:string){
    if(!uint64Revision(revision)||request!==this.sent.get(key)||BigInt(revision)<BigInt(hydratedRevision)||BigInt(revision)<(this.accepted.get(key)??0n))return false;
    this.accepted.set(key,BigInt(revision));return true;
  }
}
