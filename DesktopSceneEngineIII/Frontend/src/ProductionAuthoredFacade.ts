import type {SessionTransport} from './ProductionSessionFacade.ts';
import {sessionUuid} from './ProductionSessionTypes.ts';
import {isAuthoredResult,type AuthoredIntent,type AuthoredResult} from './ProductionAuthoredTypes.ts';
import {ProductionAudioEffects,audioEffectRuntime} from './ProductionAudioEffects.ts';

type Pending={type:string;playlistId?:string;resolve:(value:AuthoredResult)=>void;reject:(error:Error)=>void;timer:ReturnType<typeof setTimeout>};
const record=(value:unknown):value is Record<string,unknown>=>typeof value==='object'&&value!==null&&!Array.isArray(value);

// Page-scoped delivery. Disposal cannot cancel an authored commit or an import
// owned by the native Session, and a newly created page never replays an intent.
export class ProductionAuthoredFacade {
  readonly audioEffects:ProductionAudioEffects;
  private readonly bridge:SessionTransport;
  private readonly generation:string;
  private readonly incarnation:string;
  private readonly pending=new Map<string,Pending>();
  private disposed=false;
  private readonly receive=(event:MessageEvent<unknown>)=>{
    const value=event.data;
    if(this.disposed||!record(value)||value.kind!=='result'||value.generation!==this.generation||value.sessionIncarnation!==this.incarnation||typeof value.requestId!=='string')return;
    const pending=this.pending.get(value.requestId);if(!pending)return;
    this.pending.delete(value.requestId);clearTimeout(pending.timer);
    const expected=['kind','version','requestId','type','generation','sessionIncarnation','ok',value.ok?'payload':'error'];
    if(value.version!==1||value.type!==pending.type||typeof value.ok!=='boolean'||Object.keys(value).length!==expected.length||expected.some(k=>!Object.hasOwn(value,k))){pending.reject(new Error('Invalid native authored result envelope.'));return;}
    if(!value.ok){pending.reject(new Error('Native authored intent was rejected by the Session bridge. Refreshing native state.'));return;}
    if(new TextEncoder().encode(JSON.stringify(value)).length>4*1024*1024||!isAuthoredResult(value.payload)){pending.reject(new Error('Invalid native authored result.'));return;}
    if(value.payload.playlistMasterVolume!==undefined&&(pending.type!=='SetPlaylistMaster'||value.payload.playlistId!==pending.playlistId)){
      pending.reject(new Error('Native Playlist Master projection does not match the requested Playlist.'));return;
    }
    pending.resolve(value.payload);
  };
  constructor(bridge:SessionTransport|undefined,generation:string,incarnation:string){
    if(!bridge||!sessionUuid(generation)||!sessionUuid(incarnation)||[generation,incarnation].includes('00000000-0000-0000-0000-000000000000'))throw new Error('The production authored page context is unavailable.');
    this.bridge=bridge;this.generation=generation;this.incarnation=incarnation;bridge.addEventListener('message',this.receive);
    this.audioEffects=ProductionAudioEffects.retain(bridge,generation,incarnation);
  }
  async send(intent:AuthoredIntent,expectedDocument?:string):Promise<AuthoredResult>{
    if(this.disposed)return Promise.reject(new Error('The authored page has been disposed.'));
    const document=this.audioEffects.captureDocument(expectedDocument);
    await this.audioEffects.ready();
    this.audioEffects.captureDocument(document);
    if(this.disposed)throw new Error('The authored page has been disposed.');
    const result=await new Promise<AuthoredResult>((resolve,reject)=>{
      const requestId=crypto.randomUUID();
      const timer=setTimeout(()=>{this.pending.delete(requestId);reject(new Error('Native authored result is unavailable; the outcome is unknown. Refreshing native state.'));},10000);
      this.pending.set(requestId,{type:intent.type,playlistId:intent.type==='SetPlaylistMaster'?intent.payload.playlistId:undefined,resolve,reject,timer});
      try{this.bridge.postMessage({kind:'command',version:1,requestId,type:intent.type,generation:this.generation,sessionIncarnation:this.incarnation,payload:intent.payload,audioEffects:{version:1,documentIncarnation:document}});}
      catch{this.pending.delete(requestId);clearTimeout(timer);reject(new Error('Unable to deliver the authored intent. Refreshing native state.'));}
    });
    if(!result.audioEffect)return result;
    const receipt=await this.audioEffects.wait(result.audioEffect);
    return {...result,audioEffect:receipt,runtime:result.runtime==='pending'?(receipt.phase==='applied'?result.runtimeOnApplied??'synchronized':audioEffectRuntime(receipt)):result.runtime};
  }
  dispose(){
    if(this.disposed)return;this.disposed=true;this.bridge.removeEventListener('message',this.receive);
    this.audioEffects.release();
    for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(new Error('The authored page has been disposed.'));}this.pending.clear();
  }
}
