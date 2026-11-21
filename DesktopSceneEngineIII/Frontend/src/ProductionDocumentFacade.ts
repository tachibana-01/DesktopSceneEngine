import type { SessionTransport } from './ProductionSessionFacade.ts';
import {sessionUuid,type DocumentAction,type DocumentChoice} from './ProductionSessionTypes.ts';
import {ProductionAudioEffects} from './ProductionAudioEffects.ts';

export interface DocumentAcknowledgement {status:'accepted'|'busy'|'stale-operation'|'unavailable';token:string|null}
type Pending={type:string;resolve:(value:DocumentAcknowledgement)=>void;reject:(reason:Error)=>void;timer:ReturnType<typeof setTimeout>};
const record=(value:unknown):value is Record<string,unknown>=>value!==null&&typeof value==='object'&&!Array.isArray(value);
const commandTypes:Record<DocumentAction,string>={new:'NewDocument',open:'OpenDocument',save:'SaveDocument','save-as':'SaveDocumentAs',close:'RequestClose'};

// Page-scoped delivery only. Native owns the operation beyond this prompt ack;
// disposing a page neither cancels nor continues a native document transaction.
export class ProductionDocumentFacade {
  private readonly audioEffects:ProductionAudioEffects;
  private readonly bridge:SessionTransport;
  private readonly generation:string;
  private readonly incarnation:string;
  private readonly pending=new Map<string,Pending>();
  private disposed=false;
  private readonly receive=(event:MessageEvent<unknown>)=>{
    const data=event.data;
    if(this.disposed||!record(data)||data.generation!==this.generation||data.sessionIncarnation!==this.incarnation||data.kind!=='result'||typeof data.requestId!=='string')return;
    const pending=this.pending.get(data.requestId);if(!pending)return;
    this.pending.delete(data.requestId);clearTimeout(pending.timer);
    const expected=['kind','version','requestId','type','generation','sessionIncarnation','ok',data.ok?'payload':'error'];
    if(data.version!==1||data.type!==pending.type||typeof data.ok!=='boolean'||Object.keys(data).length!==expected.length||expected.some(k=>!Object.hasOwn(data,k))){pending.reject(new Error('Invalid native document acknowledgement.'));return;}
    if(!data.ok){pending.reject(new Error('Document intent was not acknowledged by the native Session. Refreshing native state.'));return;}
    const payload=data.payload;
    if(!record(payload)||Object.keys(payload).length!==2||!Object.hasOwn(payload,'status')||!Object.hasOwn(payload,'token')
      ||typeof payload.status!=='string'||!['accepted','busy','stale-operation','unavailable'].includes(payload.status)
      ||!(payload.token===null||sessionUuid(payload.token))||payload.status==='accepted'&&!sessionUuid(payload.token)){
      pending.reject(new Error('Invalid native document acknowledgement.'));return;
    }
    pending.resolve(payload as unknown as DocumentAcknowledgement);
  };
  constructor(bridge:SessionTransport|undefined,generation:string,incarnation:string){
    if(!bridge||!sessionUuid(generation)||!sessionUuid(incarnation)||[generation,incarnation].includes('00000000-0000-0000-0000-000000000000'))throw new Error('The production document page context is unavailable.');
    this.bridge=bridge;this.generation=generation;this.incarnation=incarnation;bridge.addEventListener('message',this.receive);
    this.audioEffects=ProductionAudioEffects.retain(bridge,generation,incarnation);
  }
  begin(action:DocumentAction,expectedDocument?:string){return this.send(commandTypes[action],{},expectedDocument);}
  continue(token:string,choice:DocumentChoice,expectedDocument?:string){
    if(!sessionUuid(token)||!['save','discard','cancel'].includes(choice))return Promise.reject(new Error('Invalid document decision.'));
    return this.send('ContinueDocumentDecision',{token,choice},expectedDocument);
  }
  private async send(type:string,payload:Record<string,unknown>,expectedDocument?:string):Promise<DocumentAcknowledgement>{
    if(this.disposed)return Promise.reject(new Error('The document page has been disposed.'));
    const document=this.audioEffects.captureDocument(expectedDocument);
    await this.audioEffects.ready();
    this.audioEffects.captureDocument(document);
    if(this.disposed)throw new Error('The document page has been disposed.');
    return new Promise((resolve,reject)=>{
      const requestId=crypto.randomUUID();
      // This bounds delivery of the prompt acknowledgement, not native work.
      // An unknown delivery result never authorizes retry/cancel or fake failure.
      const timer=setTimeout(()=>{this.pending.delete(requestId);reject(new Error('Native document acknowledgement is unavailable; the operation outcome is unknown. Refreshing native state.'));},10000);
      this.pending.set(requestId,{type,resolve,reject,timer});
      try{this.bridge.postMessage({kind:'command',version:1,requestId,type,generation:this.generation,sessionIncarnation:this.incarnation,payload,audioEffects:{version:1,documentIncarnation:document}});}
      catch{this.pending.delete(requestId);clearTimeout(timer);reject(new Error('Unable to deliver the document intent. Refreshing native state.'));}
    });
  }
  dispose(){
    if(this.disposed)return;this.disposed=true;this.bridge.removeEventListener('message',this.receive);
    this.audioEffects.release();
    for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(new Error('The document page has been disposed.'));}this.pending.clear();
  }
}
