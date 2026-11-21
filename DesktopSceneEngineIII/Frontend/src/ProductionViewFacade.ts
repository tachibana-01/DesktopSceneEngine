import type {SessionTransport} from './ProductionSessionFacade.ts';
import {sessionUuid} from './ProductionSessionIdentity.ts';

export type ViewIntent=
  |{type:'SetCanvasBoundaryHighlight';payload:{highlighted:boolean}}
  |{type:'SetDesktopBackground';payload:{mode:'cover'|'reveal'}}
  |{type:'AcknowledgeSessionNotice';payload:{noticeId:string}}
  |{type:'IdentifyBoundDisplay'|'DeployDesktop'|'UndeployDesktop';payload:Record<string,never>};
export type ViewStatus='shown'|'applied'|'completed'|'already-satisfied'|'rejected'|'failed'|'unavailable';

type Pending={type:ViewIntent['type'];resolve:(status:ViewStatus)=>void;reject:(error:Error)=>void;timer:ReturnType<typeof setTimeout>};
const record=(value:unknown):value is Record<string,unknown>=>typeof value==='object'&&value!==null&&!Array.isArray(value);
const exact=(value:Record<string,unknown>,keys:string[])=>Object.keys(value).length===keys.length&&keys.every(key=>Object.hasOwn(value,key));
function statusFor(type:ViewIntent['type'],value:unknown):value is ViewStatus{
  if(type==='IdentifyBoundDisplay')return value==='shown'||value==='failed'||value==='unavailable';
  if(type==='AcknowledgeSessionNotice')return value==='applied';
  if(value==='already-satisfied'||value==='unavailable')return true;
  return type==='DeployDesktop'||type==='UndeployDesktop'
    ? value==='completed'||value==='rejected'||value==='failed' : value==='applied';
}

// These Session commands neither authorize media/document changes nor retain
// their values. The next native Main snapshot is the source of visible truth.
export class ProductionViewFacade{
  private readonly pending=new Map<string,Pending>();
  private readonly bridge:SessionTransport;
  private readonly generation:string;
  private readonly incarnation:string;
  private disposed=false;

  constructor(bridge:SessionTransport,generation:string,incarnation:string){
    if(!sessionUuid(generation)||!sessionUuid(incarnation)||[generation,incarnation].includes('00000000-0000-0000-0000-000000000000'))
      throw new Error('The production View page context is invalid.');
    this.bridge=bridge;this.generation=generation;this.incarnation=incarnation;
    bridge.addEventListener('message',this.receive);
  }

  private readonly receive=(event:MessageEvent<unknown>)=>{
    const message=event.data;
    if(this.disposed||!record(message)||message.kind!=='result'||message.generation!==this.generation
      ||message.sessionIncarnation!==this.incarnation||typeof message.requestId!=='string')return;
    const pending=this.pending.get(message.requestId);
    if(!pending)return;
    this.pending.delete(message.requestId);clearTimeout(pending.timer);
    const expected=['kind','version','requestId','type','generation','sessionIncarnation','ok',message.ok?'payload':'error'];
    if(message.version!==1||message.type!==pending.type||typeof message.ok!=='boolean'||!exact(message,expected)){
      pending.reject(new Error('Native View result envelope is invalid.'));
    }else if(!message.ok){
      pending.reject(new Error('The native Session did not accept the View command.'));
    }else if(!record(message.payload)||!exact(message.payload,['status'])||!statusFor(pending.type,message.payload.status)){
      pending.reject(new Error('Native View result is invalid.'));
    }else pending.resolve(message.payload.status);
  };

  send(intent:ViewIntent):Promise<ViewStatus>{
    if(this.disposed)return Promise.reject(new Error('The production View page has retired.'));
    return new Promise((resolve,reject)=>{
      const requestId=crypto.randomUUID();
      const timer=setTimeout(()=>{
        this.pending.delete(requestId);
        reject(new Error('The production View result is unavailable.'));
      },10000);
      this.pending.set(requestId,{type:intent.type,resolve,reject,timer});
      try{this.bridge.postMessage({kind:'command',version:1,requestId,type:intent.type,generation:this.generation,sessionIncarnation:this.incarnation,payload:intent.payload});}
      catch(error){this.pending.delete(requestId);clearTimeout(timer);reject(error);}
    });
  }

  dispose(){
    if(this.disposed)return;
    this.disposed=true;
    this.bridge.removeEventListener('message',this.receive);
    for(const pending of this.pending.values()){
      clearTimeout(pending.timer);
      pending.reject(new Error('The production View page has retired.'));
    }
    this.pending.clear();
  }
}
