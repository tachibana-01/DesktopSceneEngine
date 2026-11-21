import type { SessionTransport } from './ProductionSessionFacade.ts';
import { sessionUuid } from './ProductionSessionTypes.ts';

export type PreviewRectIntent = {
  rect:{x:number;y:number;width:number;height:number};
  viewport:{width:number;height:number};
  // Observation for native validation. Native controller scale is authoritative.
  rasterizationScale:number;
  visible:boolean;
  occlusion?:{modalActive:boolean;rects:{x:number;y:number;width:number;height:number}[]};
};
export type AcceptedPresentation = {
  revision:string;page:string;binding:string;requestId:string;
  rect:{x:number;y:number;width:number;height:number};
  contentGeneration:string;deviceGeneration:string;
};
export type CurrentPresentation = {
  acceptedThrough:string;accepted:AcceptedPresentation|null;lifecycleValid:boolean;
  reason:string;apertureOwner:'host-composition-clip';
};
export type PreviewPresentationProjection = {
  status:'visible'|'suspended';desiredAccepted:boolean;desiredRequestId:string;
  requestOutcome:'accepted'|'transient-layout-pending'|'stale-request'|'invalid-request'|'lifecycle-retired';
  reason:string;accepted:AcceptedPresentation|null;lifecycleValid:boolean;apertureOwner:'host-composition-clip';
};
export type NativePreviewState = ({status:'initializing'|'visible'|'suspended'}|{status:'failure';message:string})&{
  currentPresentation?:CurrentPresentation;
  desired?:{pending:boolean;requestId:string;outcome:string;reason:string};
};
type Pending = {resolve:(status:PreviewPresentationProjection)=>void;reject:(error:Error)=>void;timer:ReturnType<typeof setTimeout>};
const record=(value:unknown):value is Record<string,unknown>=>value!==null&&typeof value==='object'&&!Array.isArray(value);
const exact=(value:Record<string,unknown>,keys:string[])=>Object.keys(value).length===keys.length&&keys.every(key=>Object.hasOwn(value,key));
const identity=(value:unknown):value is string=>typeof value==='string'&&/^[1-9][0-9]{0,19}$/.test(value)&&BigInt(value)<=18446744073709551615n;
const serial=(value:unknown):value is string=>value==='0'||identity(value);
const uuid=(value:unknown):value is string=>sessionUuid(value)&&value!=='00000000-0000-0000-0000-000000000000';
function acceptedPresentation(value:unknown,page:string):value is AcceptedPresentation {
  if(!(record(value)&&exact(value,['revision','page','binding','requestId','rect','contentGeneration','deviceGeneration'])
    &&identity(value.revision)&&value.page===page&&identity(value.binding)&&uuid(value.requestId)
    &&identity(value.contentGeneration)&&identity(value.deviceGeneration)&&record(value.rect)&&exact(value.rect,['x','y','width','height'])))return false;
  const rect=value.rect;
  return ['x','y','width','height'].every(key=>Number.isSafeInteger(rect[key])&&Number(rect[key])>=(key==='width'||key==='height'?1:0));
}
function currentPresentation(value:unknown,page:string):value is CurrentPresentation {
  return record(value)&&exact(value,['acceptedThrough','accepted','lifecycleValid','reason','apertureOwner'])
    &&serial(value.acceptedThrough)&&typeof value.lifecycleValid==='boolean'&&typeof value.reason==='string'
    &&value.apertureOwner==='host-composition-clip'
    &&(value.lifecycleValid?acceptedPresentation(value.accepted,page)&&value.accepted.revision===value.acceptedThrough:value.accepted===null);
}
function presentationProjection(value:unknown,page:string,requestId:string):value is PreviewPresentationProjection {
  return record(value)&&exact(value,['status','desiredAccepted','desiredRequestId','requestOutcome','reason','accepted','lifecycleValid','apertureOwner'])
    &&['visible','suspended'].includes(String(value.status))&&typeof value.desiredAccepted==='boolean'&&value.desiredRequestId===requestId
    &&['accepted','transient-layout-pending','stale-request','invalid-request','lifecycle-retired'].includes(String(value.requestOutcome))
    &&typeof value.reason==='string'&&typeof value.lifecycleValid==='boolean'&&value.apertureOwner==='host-composition-clip'
    &&(value.status==='visible')===value.lifecycleValid&&value.desiredAccepted===(value.requestOutcome==='accepted')
    &&(value.lifecycleValid?acceptedPresentation(value.accepted,page):value.accepted===null)&&(!value.desiredAccepted||value.lifecycleValid);
}
const copyAccepted=(value:AcceptedPresentation):AcceptedPresentation=>({...value,rect:{...value.rect}});
const copyCurrent=(value:CurrentPresentation):CurrentPresentation=>({...value,accepted:value.accepted?copyAccepted(value.accepted):null});
const sameAccepted=(left:AcceptedPresentation,right:AcceptedPresentation)=>
  (['revision','page','binding','requestId','contentGeneration','deviceGeneration'] as const).every(key=>left[key]===right[key])
  &&(['x','y','width','height'] as const).every(key=>left.rect[key]===right.rect[key]);

// Presentation-only capability, separate from the native document/runtime read
// facade. This never sends a playback, selection or authored-state command.
export class ProductionPreviewFacade {
  private readonly bridge:SessionTransport;
  private readonly generation:string;
  private readonly incarnation:string;
  private disposed=false;
  private readonly pending=new Map<string,Pending>();
  private readonly invalidations=new Set<()=>void>();
  private readonly presentationListeners=new Set<(value:CurrentPresentation|undefined)=>void>();
  private current:CurrentPresentation|undefined;
  private latestAccepted:AcceptedPresentation|undefined;
  private acceptedThrough=0n;
  private retiredThrough=-1n;
  private publishCurrent(value:CurrentPresentation|undefined){
    if(JSON.stringify(value)===JSON.stringify(this.current))return;
    this.current=value?copyCurrent(value):undefined;
    for(const listener of this.presentationListeners)listener(this.current?copyCurrent(this.current):undefined);
  }
  private unavailable(){this.publishCurrent(undefined);}
  private projectCurrent(value:CurrentPresentation){
    const through=BigInt(value.acceptedThrough),next=value.accepted,old=this.latestAccepted;
    if(through<this.acceptedThrough)return;
    if(value.lifecycleValid){
      if(!next||through<=this.retiredThrough)return;
      if(old&&(BigInt(next.contentGeneration)<BigInt(old.contentGeneration)||BigInt(next.deviceGeneration)<BigInt(old.deviceGeneration)
        ||next.revision===old.revision&&!sameAccepted(next,old)))return;
      this.latestAccepted=copyAccepted(next);
    }else this.retiredThrough=through;
    this.acceptedThrough=through;this.publishCurrent(value);
  }
  private readonly receive=(event:MessageEvent<unknown>)=>{
    const data=event.data;
    if(this.disposed||!record(data)||data.generation!==this.generation||data.sessionIncarnation!==this.incarnation)return;
    if(data.kind==='event'){
      if(data.version===1&&data.type==='PreviewPresentationState'
        &&exact(data,['kind','version','type','generation','sessionIncarnation','payload'])
        &&currentPresentation(data.payload,this.generation))this.projectCurrent(data.payload);
      if(data.version===1&&data.type==='PreviewGeometryInvalidated'&&record(data.payload)&&exact(data.payload,[])
        &&exact(data,['kind','version','type','generation','sessionIncarnation','payload']))for(const listener of this.invalidations)listener();
      return;
    }
    if(data.kind!=='result'||typeof data.requestId!=='string')return;
    const pending=this.pending.get(data.requestId);if(!pending)return;
    this.pending.delete(data.requestId);clearTimeout(pending.timer);
    if(data.version!==1||data.type!=='SetPreviewRect'||typeof data.ok!=='boolean'
      ||!exact(data,['kind','version','requestId','type','generation','sessionIncarnation','ok',data.ok?'payload':'error'])){
      this.unavailable();pending.reject(new Error('invalid-preview-result'));return;
    }
    if(!data.ok){
      // A well-formed desired-command rejection does not revoke native current
      // presentation. Only its independent lifecycle projection can do that.
      if(typeof data.error!=='string')this.unavailable();
      pending.reject(new Error(typeof data.error==='string'?data.error:'invalid-preview-error'));return;
    }
    if(!presentationProjection(data.payload,this.generation,data.requestId)){this.unavailable();pending.reject(new Error('invalid-preview-presentation'));return;}
    const value=data.payload;
    if(value.lifecycleValid&&value.accepted)this.projectCurrent({acceptedThrough:value.accepted.revision,accepted:value.accepted,
      lifecycleValid:true,reason:value.reason,apertureOwner:value.apertureOwner});
    // A result without acceptedThrough cannot retire a newer native fact.
    // Independent PreviewPresentationState events carry lifecycle authority.
    pending.resolve({...value,accepted:value.accepted?copyAccepted(value.accepted):null});
  };
  constructor(bridge:SessionTransport,generation:string,incarnation:string){
    if(!sessionUuid(generation)||!sessionUuid(incarnation)||[generation,incarnation].includes('00000000-0000-0000-0000-000000000000'))throw new Error('invalid-preview-page-context');
    this.bridge=bridge;this.generation=generation;this.incarnation=incarnation;
    bridge.addEventListener('message',this.receive);
  }
  setRect(intent:PreviewRectIntent,onSubmitted?:(requestId:string)=>void):Promise<PreviewPresentationProjection>{
    if(this.disposed)return Promise.reject(new Error('preview-page-disposed'));
    return new Promise((resolve,reject)=>{
      const requestId=crypto.randomUUID();
      const timer=setTimeout(()=>{this.pending.delete(requestId);this.unavailable();reject(new Error('preview-geometry-request-timeout'));},10000);
      this.pending.set(requestId,{resolve,reject,timer});
      try{onSubmitted?.(requestId);this.bridge.postMessage({kind:'command',version:1,requestId,type:'SetPreviewRect',generation:this.generation,sessionIncarnation:this.incarnation,payload:intent});}
      catch{this.pending.delete(requestId);clearTimeout(timer);this.unavailable();reject(new Error('preview-geometry-transport-failure'));}
    });
  }
  subscribeInvalidation(listener:()=>void){this.invalidations.add(listener);return()=>{this.invalidations.delete(listener);};}
  subscribePresentation(listener:(value:CurrentPresentation|undefined)=>void){
    if(this.disposed)return()=>{};
    this.presentationListeners.add(listener);if(this.current)listener(copyCurrent(this.current));
    return()=>{this.presentationListeners.delete(listener);};
  }
  dispose(){
    if(this.disposed)return;this.disposed=true;this.bridge.removeEventListener('message',this.receive);this.invalidations.clear();this.presentationListeners.clear();this.current=undefined;
    for(const pending of this.pending.values()){clearTimeout(pending.timer);pending.reject(new Error('preview-page-disposed'));}this.pending.clear();
  }
}

// Event-driven layout observation only. rAF coalesces DOM layout reads; it is
// never a render loop or a clock for Engine, playback or media progression.
export function observeNativePreviewRect(element:HTMLDivElement,facade:ProductionPreviewFacade,onState:(state:NativePreviewState)=>void){
  let disposed=false,frame=0,inFlight=false,force=false,lastSent='';
  let current:NativePreviewState={status:'initializing'};
  const publish=()=>{if(!disposed)onState(current);};
  const unsubscribePresentation=facade.subscribePresentation(value=>{
    current=value?{...current,status:value.lifecycleValid?'visible':'suspended',currentPresentation:value}
      :{...current,status:'failure',message:'preview-presentation-unavailable',currentPresentation:undefined};publish();
  });
  let queued:{intent:PreviewRectIntent;key:string}|undefined;
  const overlayElements=new Set<HTMLElement>();
  const overlayMarker='[data-native-preview-overlay]';
  // One synchronous layout sample owns both Preview and overlay observations.
  // Native alone decides occlusion after its current geometry/lifecycle checks.
  const sampleOcclusion=():NonNullable<PreviewRectIntent['occlusion']>=>{
    const current=new Set(document.querySelectorAll<HTMLElement>(overlayMarker));
    for(const overlay of overlayElements)if(!current.has(overlay)){observer.unobserve(overlay);overlayElements.delete(overlay);}
    let modalActive=false;
    const rects:NonNullable<PreviewRectIntent['occlusion']>['rects']=[];
    for(const overlay of current){
      if(!overlayElements.has(overlay)){overlayElements.add(overlay);observer.observe(overlay);}
      if(!overlay.isConnected||overlay.hidden||overlay.tagName==='DIALOG'&&!overlay.hasAttribute('open'))continue;
      const style=getComputedStyle(overlay),bounds=overlay.getBoundingClientRect();
      if(style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||Number(style.opacity)===0||bounds.width<=0||bounds.height<=0)continue;
      if(overlay.dataset.nativePreviewOverlay==='modal')modalActive=true;
      else if(overlay.dataset.nativePreviewOverlay==='floating')rects.push({x:bounds.x,y:bounds.y,width:bounds.width,height:bounds.height});
    }
    // Tell the native hover owner to block conservatively if there are too
    // many floating surfaces to describe within the bounded wire payload.
    return {modalActive:modalActive||rects.length>32,rects:rects.slice(0,32)};
  };
  const submit=({intent,key}:{intent:PreviewRectIntent;key:string})=>{
    if(disposed)return;
    if(inFlight){queued={intent,key};return;}
    inFlight=true;lastSent=key;
    void facade.setRect(intent,requestId=>{
      current={...current,desired:{pending:true,requestId,outcome:'pending',reason:''}};publish();
    }).then(result=>{
      if(disposed)return;
      current={...current,desired:{pending:Boolean(queued),requestId:result.desiredRequestId,outcome:result.requestOutcome,reason:result.reason}};publish();
    },error=>{
      if(disposed)return;
      const message=error instanceof Error?error.message:'preview-geometry-failure';
      current={...current,status:current.currentPresentation?.lifecycleValid?'visible':'failure',message,
        desired:{pending:Boolean(queued),requestId:current.desired?.requestId??'',outcome:'failed',reason:message}};publish();
    }).finally(()=>{
      inFlight=false;if(disposed)return;
      if(queued){
        // A queued close observation is not a restoration permit. Overlay DOM,
        // Preview geometry or visibility may have changed while native replied.
        queued=undefined;if(frame){cancelAnimationFrame(frame);frame=0;}force=true;measure();
      }
    });
  };
  const measure=()=>{
    frame=0;if(disposed)return;
    const bounds=element.getBoundingClientRect();
    const occlusion=sampleOcclusion();
    const intent:PreviewRectIntent={rect:{x:bounds.x,y:bounds.y,width:bounds.width,height:bounds.height},
      viewport:{width:window.innerWidth,height:window.innerHeight},rasterizationScale:window.devicePixelRatio,
      // Model C keeps page/connection visibility separate from desired extent.
      // A temporary zero layout must be rejected as geometry, not retire media.
      visible:!document.hidden&&element.isConnected,occlusion};
    if(intent.visible){
      // Resize can update the viewport before React commits the surface fitted
      // to it. The existing Preview view stamps the inputs it actually fitted;
      // this observer reads that DOM truth, it does not calculate another fit.
      const viewport=element.closest<HTMLElement>('.preview-viewport')?.getBoundingClientRect();
      const fitted=viewport&&element.dataset.previewFitWidth===String(Math.floor(viewport.width))
        &&element.dataset.previewFitHeight===String(Math.floor(viewport.height));
      // Overlay facts still reach native hover blocking during a fit handoff.
      // A normal DOM overlay never invalidates current Model C presentation.
      if(!fitted&&!occlusion.modalActive&&!occlusion.rects.length)return;
    }
    const key=JSON.stringify(intent),resend=force;force=false;
    if(key!==(queued?.key??lastSent)||resend)submit({intent,key});
  };
  const schedule=()=>{if(!disposed&&!frame)frame=requestAnimationFrame(measure);};
  const revalidate=()=>{force=true;schedule();};
  const visibility=()=>{if(frame){cancelAnimationFrame(frame);frame=0;}force=true;measure();};
  const observer=new ResizeObserver(schedule);observer.observe(element);
  const overlayMutation=new MutationObserver(records=>{
    if(records.some(record=>record.type==='childList'||record.target instanceof Element
      &&(record.target.matches(overlayMarker)||record.target.closest(overlayMarker))))schedule();
  });
  overlayMutation.observe(document.body,{childList:true,subtree:true,attributes:true,attributeFilter:['data-native-preview-overlay','open','hidden','style','class']});
  window.addEventListener('resize',schedule);window.addEventListener('scroll',schedule,true);
  document.addEventListener('visibilitychange',visibility);
  const unsubscribe=facade.subscribeInvalidation(revalidate);schedule();
  const dispose=()=>{
    disposed=true;queued=undefined;if(frame)cancelAnimationFrame(frame);observer.disconnect();overlayMutation?.disconnect();overlayElements.clear();unsubscribe();unsubscribePresentation();
    window.removeEventListener('resize',schedule);window.removeEventListener('scroll',schedule,true);document.removeEventListener('visibilitychange',visibility);
  };
  return {dispose,remeasure:schedule};
}
