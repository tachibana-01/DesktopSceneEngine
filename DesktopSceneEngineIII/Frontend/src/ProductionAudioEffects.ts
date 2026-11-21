import type {SessionTransport} from './ProductionSessionFacade.ts';
import {sessionUuid} from './ProductionSessionIdentity.ts';

export type AudioEffectScope='structural'|'embedded-gain'|'playlist-bgm-gain'|'item-bgm-gain'|'output-mute';
export type AudioEffectPhase='rejected'|'preparing'|'pending'|'applied'|'deferred-unavailable'|'failed'|'cancelled'|'expired';
export interface AudioEffectReceipt {
  operation:string;documentIncarnation:string;bindingToken:string;execution:string;sequence:string;
  scope:AudioEffectScope;phase:AudioEffectPhase;error:string;effectTimeNs:string;playlistId:string|null;itemId:string|null;
}
export interface AudioEffectsState {
  version:1;documentIncarnation:string;execution:string;capacityRevision:string;closing:boolean;stopped:boolean;
  desiredMuted:boolean;appliedMuted:boolean;muteKnown:boolean;deviceHealth:'healthy'|'device-unavailable'|'contract-faulted';
  effects:AudioEffectReceipt[];
}
const record=(v:unknown):v is Record<string,unknown>=>v!==null&&typeof v==='object'&&!Array.isArray(v);
const exact=(v:Record<string,unknown>,keys:string[])=>Object.keys(v).length===keys.length&&keys.every(k=>Object.hasOwn(v,k));
const uuid=(v:unknown):v is string=>sessionUuid(v)&&v!=='00000000-0000-0000-0000-000000000000';
const uint64=(v:unknown):v is string=>typeof v==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(v)&&BigInt(v)<=18446744073709551615n;
const int64=(v:unknown):v is string=>typeof v==='string'&&/^-?(0|[1-9][0-9]{0,18})$/.test(v)&&BigInt(v)>=-9223372036854775808n&&BigInt(v)<=9223372036854775807n;
const scopes=['structural','embedded-gain','playlist-bgm-gain','item-bgm-gain','output-mute'];
const phases=['rejected','preparing','pending','applied','deferred-unavailable','failed','cancelled','expired'];
export function isAudioEffectReceipt(v:unknown):v is AudioEffectReceipt {
  return record(v)&&exact(v,['operation','documentIncarnation','bindingToken','execution','sequence','scope','phase','error','effectTimeNs','playlistId','itemId'])
    &&uuid(v.operation)&&uuid(v.documentIncarnation)&&typeof v.bindingToken==='string'&&v.bindingToken.length<=256
    &&uint64(v.execution)&&v.execution!=='0'&&uint64(v.sequence)&&v.sequence!=='0'
    &&scopes.includes(String(v.scope))&&phases.includes(String(v.phase))&&typeof v.error==='string'&&v.error.length<=4096
    &&int64(v.effectTimeNs)&&[v.playlistId,v.itemId].every(id=>id===null||uuid(id))&&(v.itemId===null||v.playlistId!==null);
}
export function isAudioEffectsState(v:unknown):v is AudioEffectsState {
  return record(v)&&exact(v,['version','documentIncarnation','execution','capacityRevision','closing','stopped','desiredMuted','appliedMuted','muteKnown','deviceHealth','effects'])
    &&v.version===1&&(uuid(v.documentIncarnation)||v.closing===true&&v.documentIncarnation==='')&&uint64(v.execution)&&uint64(v.capacityRevision)
    &&['closing','stopped','desiredMuted','appliedMuted','muteKnown'].every(k=>typeof v[k]==='boolean')
    &&['healthy','device-unavailable','contract-faulted'].includes(String(v.deviceHealth))
    &&Array.isArray(v.effects)&&v.effects.length<=5&&v.effects.every(isAudioEffectReceipt)&&new Set(v.effects.map(e=>e.scope)).size===v.effects.length;
}
export const audioEffectPending=(receipt:AudioEffectReceipt)=>receipt.phase==='preparing'||receipt.phase==='pending';
export const audioEffectRuntime=(receipt:AudioEffectReceipt)=>receipt.phase==='applied'?'synchronized':receipt.phase==='deferred-unavailable'?'deferred-unavailable':receipt.phase==='cancelled'?'cancelled':receipt.phase==='pending'||receipt.phase==='preparing'?'pending':'failed';

function sameEffect(left:AudioEffectReceipt,right:AudioEffectReceipt){
  return left.operation===right.operation&&left.execution===right.execution&&left.sequence===right.sequence
    &&left.documentIncarnation===right.documentIncarnation&&left.scope===right.scope
    &&left.bindingToken===right.bindingToken&&left.playlistId===right.playlistId&&left.itemId===right.itemId;
}

type Pending={type:string;observationVersion:number;resolve:(v:AudioEffectsState)=>void;reject:(e:Error)=>void;timer:ReturnType<typeof setTimeout>};
type Waiter={receipt:AudioEffectReceipt;resolve:(v:AudioEffectReceipt)=>void;reject:(e:Error)=>void;timer:ReturnType<typeof setTimeout>};
const owners=new WeakMap<SessionTransport,Map<string,ProductionAudioEffects>>();

// One page-scoped transport owner, shared by the finite production facades.
// Native operations outlive this object. It never retries a mutation.
export class ProductionAudioEffects {
  private readonly bridge:SessionTransport;
  readonly generation:string;
  readonly incarnation:string;
  private references=0;
  private disposed=false;
  private bootstrap:Promise<AudioEffectsState>|null=null;
  private refreshPending:Promise<AudioEffectsState>|null=null;
  private latest:AudioEffectsState|null=null;
  private observationVersion=0;
  private readonly requests=new Map<string,Pending>();
  private readonly waiters=new Map<string,Waiter>();
  private readonly listeners=new Set<(v:AudioEffectsState)=>void>();
  private constructor(bridge:SessionTransport,generation:string,incarnation:string){this.bridge=bridge;this.generation=generation;this.incarnation=incarnation;bridge.addEventListener('message',this.receive);}
  static retain(bridge:SessionTransport,generation:string,incarnation:string){
    let pages=owners.get(bridge);if(!pages){pages=new Map();owners.set(bridge,pages);}
    const key=generation+':'+incarnation;let owner=pages.get(key);
    if(!owner){owner=new ProductionAudioEffects(bridge,generation,incarnation);pages.set(key,owner);}++owner.references;return owner;
  }
  get state(){return this.latest;}
  get version(){return this.observationVersion;}
  seedNativeSnapshot(value:AudioEffectsState,expectedVersion?:number){if(!isAudioEffectsState(value))throw new Error('Invalid native audio effects bootstrap.');if(expectedVersion===undefined||expectedVersion===this.observationVersion)this.observe(value);}
  captureDocument(expected?:string){
    const document=expected??this.latest?.documentIncarnation;
    if(!uuid(document)||this.latest?.closing||this.latest&&this.latest.documentIncarnation!==document)throw new Error('The native document context changed before audio effect admission.');
    return document;
  }
  subscribe(listener:(v:AudioEffectsState)=>void){this.listeners.add(listener);return()=>{this.listeners.delete(listener);};}
  ready(){return this.bootstrap??=this.request('NegotiateAudioEffects',{version:1});}
  async refresh(){await this.ready();return this.refreshPending??=this.request('GetAudioEffects',{}).finally(()=>{this.refreshPending=null;});}
  private observe(value:AudioEffectsState){
    ++this.observationVersion;
    this.latest=value;
    this.resolveWaiters(value);
    for(const listener of this.listeners)listener(value);
  }
  private removeWaiter(waiter:Waiter){
    this.waiters.delete(waiter.receipt.operation);
    clearTimeout(waiter.timer);
  }
  private resolveWaiters(value:AudioEffectsState){
    for(const waiter of this.waiters.values()){
      const admitted=waiter.receipt;
      const sameExecution=value.execution===admitted.execution;
      if(sameExecution&&value.documentIncarnation===admitted.documentIncarnation){
        const slot=value.effects.find(effect=>effect.scope===admitted.scope);
        if(slot&&sameEffect(slot,admitted)&&!audioEffectPending(slot)){
          this.removeWaiter(waiter);
          waiter.resolve(slot);
          continue;
        }
        // CP1 retains one receipt per scope. Only a strictly newer operation
        // in this execution and document proves this slot was replaced.
        if(slot&&slot.documentIncarnation===admitted.documentIncarnation&&slot.execution===admitted.execution
          &&slot.operation!==admitted.operation&&BigInt(slot.sequence)>BigInt(admitted.sequence)){
          this.removeWaiter(waiter);
          waiter.reject(new Error('Native effect receipt expired; read current native state.'));
          continue;
        }
      }
      if(sameExecution&&value.stopped||BigInt(value.execution)>BigInt(admitted.execution)){
        this.removeWaiter(waiter);
        waiter.reject(new Error('The native audio execution has retired.'));
      }
    }
  }
  async wait(receipt:AudioEffectReceipt):Promise<AudioEffectReceipt>{
    if(!audioEffectPending(receipt))return receipt;
    if(this.disposed)throw new Error('The audio effects page has been disposed.');
    if(this.waiters.has(receipt.operation))throw new Error('An effect receipt already has a page waiter.');
    const promise=new Promise<AudioEffectReceipt>((resolve,reject)=>{
      const timer=setTimeout(()=>{this.waiters.delete(receipt.operation);reject(new Error('Native audio effect outcome is unknown; refresh native state.'));},10000);
      this.waiters.set(receipt.operation,{receipt,resolve,reject,timer});
    });
    // Covers terminal publication arriving before the admission reply. A
    // best-effort read can catch up to the native frontier; its failure says
    // nothing about the admitted operation, whose update can still arrive.
    if(this.latest)this.resolveWaiters(this.latest);
    if(this.waiters.has(receipt.operation))void this.refresh().catch(()=>{});
    return promise;
  }
  private readonly receive=(event:MessageEvent<unknown>)=>{
    const v=event.data;if(this.disposed||!record(v)||v.generation!==this.generation||v.sessionIncarnation!==this.incarnation)return;
    if(v.kind==='event'){
      if(v.version===1&&v.type==='AudioEffectsChanged'&&exact(v,['kind','version','type','generation','sessionIncarnation','payload'])&&isAudioEffectsState(v.payload))this.observe(v.payload);
      return;
    }
    if(v.kind!=='result'||typeof v.requestId!=='string')return;
    const pending=this.requests.get(v.requestId);if(!pending)return;this.requests.delete(v.requestId);clearTimeout(pending.timer);
    if(v.version!==1||v.type!==pending.type||typeof v.ok!=='boolean'||!exact(v,['kind','version','requestId','type','generation','sessionIncarnation','ok',v.ok?'payload':'error'])||!v.ok||!isAudioEffectsState(v.payload)){
      pending.reject(new Error('The native audioEffects v1 contract is unavailable.'));return;
    }
    if(pending.observationVersion===this.observationVersion)this.observe(v.payload);
    pending.resolve(this.latest??v.payload);
  };
  private request(type:string,payload:unknown):Promise<AudioEffectsState>{
    if(this.disposed)return Promise.reject(new Error('The audio effects page has been disposed.'));
    return new Promise((resolve,reject)=>{
      const requestId=crypto.randomUUID(),timer=setTimeout(()=>{this.requests.delete(requestId);reject(new Error('Native audio effect state is unavailable.'));},10000);
      this.requests.set(requestId,{type,observationVersion:this.observationVersion,resolve,reject,timer});
      try{this.bridge.postMessage({kind:'command',version:1,requestId,type,generation:this.generation,sessionIncarnation:this.incarnation,payload});}
      catch{this.requests.delete(requestId);clearTimeout(timer);reject(new Error('Unable to negotiate native audio effects.'));}
    });
  }
  release(){
    if(--this.references>0)return;this.disposed=true;this.bridge.removeEventListener('message',this.receive);
    for(const p of this.requests.values()){clearTimeout(p.timer);p.reject(new Error('The audio effects page has been disposed.'));}
    for(const w of this.waiters.values()){clearTimeout(w.timer);w.reject(new Error('The audio effects page has been disposed.'));}
    this.requests.clear();this.waiters.clear();this.listeners.clear();owners.get(this.bridge)?.delete(this.generation+':'+this.incarnation);
  }
}
