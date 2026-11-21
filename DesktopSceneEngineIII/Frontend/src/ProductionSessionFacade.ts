import { isMainWindowSnapshot, sessionUuid, type MainWindowFailure, type MainWindowSnapshot } from './ProductionSessionTypes.ts';
import type {AuthoredResult} from './ProductionAuthoredTypes.ts';
import {ProductionAudioEffects} from './ProductionAudioEffects.ts';

export interface SessionTransport {
  postMessage(message:unknown):void;
  addEventListener(type:'message',callback:(event:MessageEvent<unknown>)=>void):void;
  removeEventListener(type:'message',callback:(event:MessageEvent<unknown>)=>void):void;
}
export class SessionReadFailure extends Error {
  readonly code:MainWindowFailure;
  constructor(code:MainWindowFailure,message:string){super(message);this.code=code;this.name='SessionReadFailure';}
}
type Pending={audioVersion:number;resolve:(value:MainWindowSnapshot)=>void;reject:(reason:Error)=>void;timer:ReturnType<typeof setTimeout>};
const record=(v:unknown):v is Record<string,unknown>=>v!==null&&typeof v==='object'&&!Array.isArray(v);

// One read capability. No write dispatcher, speculative transactions or domain
// reconstruction timers are present in the production Session boundary.
export class ProductionSessionFacade {
  private readonly audioEffects:ProductionAudioEffects;
  private readonly bridge:SessionTransport|undefined;
  readonly generation:string;
  readonly incarnation:string;
  private readonly pending=new Map<string,Pending>();
  private readonly invalidations=new Set<()=>void>();
  private disposed=false;
  private readonly receive=(event:MessageEvent<unknown>)=>{
    const data=event.data;
    if(this.disposed||!record(data)||data.generation!==this.generation||data.sessionIncarnation!==this.incarnation)return;
    if(data.kind==='event'){
      if(data.version===1&&data.type==='MainWindowSnapshotInvalidated'&&record(data.payload)&&Object.keys(data.payload).length===0
        &&Object.keys(data).every(k=>['kind','version','type','generation','sessionIncarnation','payload'].includes(k)))for(const fn of this.invalidations)fn();
      return;
    }
    if(data.kind!=='result'||typeof data.requestId!=='string')return;
    const pending=this.pending.get(data.requestId);if(!pending)return;
    this.pending.delete(data.requestId);clearTimeout(pending.timer);
    const expected=['kind','version','requestId','type','generation','sessionIncarnation','ok',data.ok?'payload':'error'];
    if(data.version!==1||data.type!=='GetMainWindowSnapshot'||typeof data.ok!=='boolean'||Object.keys(data).length!==expected.length
      ||expected.some(k=>!Object.hasOwn(data,k))||Object.keys(data).some(k=>!expected.includes(k))){pending.reject(new SessionReadFailure('SnapshotValidationFailure','Invalid native snapshot envelope.'));return;}
    if(!data.ok){
      const code=data.error==='NativeOwnerFailure'?'NativeOwnerFailure':'SnapshotUnavailable';
      pending.reject(new SessionReadFailure(code,typeof data.error==='string'?data.error:'Native snapshot unavailable.'));return;
    }
    if(new TextEncoder().encode(JSON.stringify(data)).length>4*1024*1024||!isMainWindowSnapshot(data.payload,this.incarnation)){pending.reject(new SessionReadFailure('SnapshotValidationFailure','Native snapshot does not satisfy the Session contract.'));return;}
    this.audioEffects.seedNativeSnapshot(data.payload.audioEffects,pending.audioVersion);
    pending.resolve(data.payload);
  };
  constructor(bridge:SessionTransport|undefined,generation:string,incarnation:string){
    if(!bridge)throw new SessionReadFailure('BridgeUnavailable','The production Session native bridge is unavailable.');
    if(!sessionUuid(generation)||!sessionUuid(incarnation)||[generation,incarnation].includes('00000000-0000-0000-0000-000000000000'))throw new SessionReadFailure('InvalidPageContext','The production Session page context is invalid.');
    this.bridge=bridge;this.generation=generation;this.incarnation=incarnation;bridge.addEventListener('message',this.receive);
    this.audioEffects=ProductionAudioEffects.retain(bridge,generation,incarnation);
  }
  read():Promise<MainWindowSnapshot>{
    if(this.disposed)return Promise.reject(new SessionReadFailure('TransportFailure','The Session page has been disposed.'));
    return new Promise((resolve,reject)=>{
      const requestId=crypto.randomUUID();
      const timer=setTimeout(()=>{this.pending.delete(requestId);reject(new SessionReadFailure('TransportFailure','Native snapshot request timed out.'));},10000);
      this.pending.set(requestId,{audioVersion:this.audioEffects.version,resolve,reject,timer});
      try{this.bridge!.postMessage({kind:'command',version:1,requestId,type:'GetMainWindowSnapshot',generation:this.generation,sessionIncarnation:this.incarnation,payload:{}});}
      catch{this.pending.delete(requestId);clearTimeout(timer);reject(new SessionReadFailure('TransportFailure','Unable to send the native snapshot request.'));}
    });
  }
  subscribeInvalidation(fn:()=>void){this.invalidations.add(fn);return()=>{this.invalidations.delete(fn);};}
  dispose(){
    if(this.disposed)return;this.disposed=true;this.bridge!.removeEventListener('message',this.receive);this.invalidations.clear();
    this.audioEffects.release();
    for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(new SessionReadFailure('TransportFailure','The Session page has been disposed.'));}this.pending.clear();
  }
}

export type SessionReadState={status:'loading'}|{status:'ready';snapshot:MainWindowSnapshot}|{status:'failure';code:MainWindowFailure;message:string};
export class ProductionSessionController {
  private readonly facade:ProductionSessionFacade;
  private state:SessionReadState={status:'loading'};
  private readonly listeners=new Set<()=>void>();
  // Delivery watermarks for partial read-backs, using native revisions only.
  // A newer B result does not imply that A has already been projected.
  private readonly masterReadBackRevisions=new Map<string,bigint>();
  private disposed=false;
  private reading=false;
  private refreshPromise:Promise<void>|null=null;
  private invalidated=false;
  private readonly unsubscribe:()=>void;
  constructor(facade:ProductionSessionFacade){this.facade=facade;this.unsubscribe=facade.subscribeInvalidation(()=>{void this.refresh();});}
  getSnapshot=()=>this.state;
  subscribe=(listener:()=>void)=>{this.listeners.add(listener);return()=>{this.listeners.delete(listener);};};
  async projectPlaylistMaster(result:AuthoredResult):Promise<boolean>{
    if(result.playlistMasterVolume===undefined||!result.playlistId)return false;
    // A previously requested full read must not land after this newer native
    // acknowledgement and overwrite it. No new full read belongs to this path.
    if(this.refreshPromise)await this.refreshPromise;
    if(this.disposed||this.state.status!=='ready')return false;
    const snapshot=this.state.snapshot;
    const index=snapshot.project.playlists.findIndex(p=>p.id===result.playlistId);
    if(index<0)return false;
    const revision=BigInt(result.revision);
    if(revision<(this.masterReadBackRevisions.get(result.playlistId)??BigInt(snapshot.document.authoredRevision)))return true;
    this.masterReadBackRevisions.set(result.playlistId,revision);
    const documentRevision=revision>BigInt(snapshot.document.authoredRevision)?result.revision:snapshot.document.authoredRevision;
    const playlists=snapshot.project.playlists.slice();
    playlists[index]={...playlists[index],masterVolume:result.playlistMasterVolume};
    this.state={status:'ready',snapshot:{...snapshot,project:{...snapshot.project,playlists},
      document:{...snapshot.document,authoredRevision:documentRevision,dirty:documentRevision!==snapshot.document.cleanRevision}}};
    for(const listener of this.listeners)listener();
    return true;
  }
  refresh():Promise<void>{
    if(this.disposed)return Promise.resolve();this.invalidated=true;if(this.reading)return this.refreshPromise!;
    this.reading=true;
    // Install the shared completion before starting a read, so a synchronous
    // invalidation also joins this drain rather than releasing its caller.
    let resolve!:()=>void,reject!:(reason:unknown)=>void;
    const promise=new Promise<void>((yes,no)=>{resolve=yes;reject=no;});this.refreshPromise=promise;
    void this.drain().then(resolve,reject);return promise;
  }
  private async drain(){
    try{
      while(this.invalidated&&!this.disposed){
        this.invalidated=false;
        try{const snapshot=await this.facade.read();if(this.disposed)return;this.state={status:'ready',snapshot};
          this.masterReadBackRevisions.clear();for(const playlist of snapshot.project.playlists)this.masterReadBackRevisions.set(playlist.id,BigInt(snapshot.document.authoredRevision));}
        catch(error){if(this.disposed)return;this.invalidated=false;this.state={status:'failure',code:error instanceof SessionReadFailure?error.code:'TransportFailure',message:error instanceof Error?error.message:'Snapshot unavailable.'};}
        for(const listener of this.listeners)listener();
      }
    }finally{this.reading=false;this.refreshPromise=null;}
  }
  dispose(){if(this.disposed)return;this.disposed=true;this.unsubscribe();this.listeners.clear();this.facade.dispose();}
}
