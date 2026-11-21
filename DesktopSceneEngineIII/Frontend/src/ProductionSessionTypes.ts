import type { SessionProject } from './sessionTypes.ts';
import {isAudioEffectsState,type AudioEffectsState} from './ProductionAudioEffects.ts';
import {isMediaImport,type MediaImport} from './ProductionAuthoredTypes.ts';
import {sessionUuid,uint64Revision} from './ProductionSessionIdentity.ts';
export {sessionUuid,uint64Revision} from './ProductionSessionIdentity.ts';

export type DocumentAction='new'|'open'|'save'|'save-as'|'close';
export type DocumentChoice='save'|'discard'|'cancel';
export interface DocumentOperation {
  pending:null|{token:string;action:DocumentAction;stage:'queued'|'open-picker'|'save-picker'|'decision'|'executing'};
  last:null|{token:string;action:DocumentAction;outcome:'completed'|'saved'|'cancelled'|'failed'|'candidate-invalid'|'save-cancelled'|'save-failed';message:string};
}
export interface SessionNotice { id:string; level:'information'|'warning'|'error'; title:string; message:string }
export interface MainWindowSnapshot {
  audioEffects:AudioEffectsState;
  session:{incarnation:string;boundDisplayId:string};
  document:{path:string|null;displayName:string;authoredRevision:string;cleanRevision:string;dirty:boolean};
  project:SessionProject;
  selection:{playlistId:string|null;itemId:string|null};
  active:{playlistId:string|null;itemId:string|null};
  display:{status:'success'|'api-failure'|'incomplete-path-metadata'|'duplicate-identity';descriptor:null|{
    id:string;displayNumber:number;friendlyName:string;sourceDeviceName:string;desktopX:number;desktopY:number;
    pixelWidth:number;pixelHeight:number;primary:boolean;refreshHz:number|null;
  }};
  desktop:{desired:boolean;targetRegistered:boolean;hostOwned:boolean;hostHealthy:boolean;busy:boolean;canDeploy:boolean;canUndeploy:boolean};
  preferences:{readStatus:'Success'|'Unavailable';highlight:boolean;background:'cover'|'reveal';backgroundReadStatus:'Success'|'Failure';persistedBackground:'cover'|'reveal'|null};
  language:{readStatus:'Success'|'Failure';committed:'en'|'zh_CN'|null;effective:'en'|'zh_CN'};
  host:{ready:boolean;sessionRoleReady:boolean;closing:boolean;ending:boolean;visible:boolean;minimized:boolean;maximized:boolean;hideEligible:boolean};
  documentOperation:DocumentOperation;
  mediaImport:MediaImport;
  sessionNotice?:SessionNotice;
}

export type MainWindowFailure = 'BridgeUnavailable'|'InvalidPageContext'|'SnapshotUnavailable'|'SnapshotValidationFailure'|'NativeOwnerFailure'|'TransportFailure';
const record=(v:unknown):v is Record<string,unknown>=>typeof v==='object'&&v!==null&&!Array.isArray(v);
const keys=(v:Record<string,unknown>,names:string[])=>names.length===Object.keys(v).length&&names.every(k=>Object.hasOwn(v,k));
const text=(v:unknown,max=4*1024*1024):v is string=>typeof v==='string'&&v.length<=max;
const finite=(v:unknown,min=-Infinity,max=Infinity):v is number=>typeof v==='number'&&Number.isFinite(v)&&v>=min&&v<=max;
const boolean=(v:unknown)=>typeof v==='boolean';
const one=(v:unknown,choices:readonly unknown[])=>choices.includes(v);
const nullableId=(v:unknown)=>v===null||sessionUuid(v);
const mode=(v:unknown)=>one(v,['list-loop','random','repeat-current']);
export function isDocumentOperation(value:unknown):value is DocumentOperation {
  if(!record(value)||!keys(value,['pending','last']))return false;
  const action=(v:unknown)=>one(v,['new','open','save','save-as','close']);
  const p=value.pending,l=value.last;
  return (p===null||record(p)&&keys(p,['token','action','stage'])&&sessionUuid(p.token)&&action(p.action)&&one(p.stage,['queued','open-picker','save-picker','decision','executing']))
    &&(l===null||record(l)&&keys(l,['token','action','outcome','message'])&&sessionUuid(l.token)&&action(l.action)&&one(l.outcome,['completed','saved','cancelled','failed','candidate-invalid','save-cancelled','save-failed'])&&text(l.message));
}
function presentation(v:unknown):boolean {
  if(!record(v)||!keys(v,['crop','layout','mediaPresentation']))return false;
  const {crop:c,layout:l,mediaPresentation:m}=v;
  return record(c)&&keys(c,['x','y','width','height'])&&Object.values(c).every(n=>finite(n))
    &&record(l)&&keys(l,['positionX','positionY','scaleX','scaleY'])&&Object.values(l).every(n=>finite(n))
    &&record(m)&&keys(m,['mode','customAspect'])&&one(m.mode,['native','auto-fill','custom'])&&record(m.customAspect)
    &&keys(m.customAspect,['numerator','denominator'])&&Object.values(m.customAspect).every(n=>finite(n,1)&&Number.isInteger(n));
}
export function isMainWindowSnapshot(value:unknown,incarnation:string):value is MainWindowSnapshot {
  if(!record(value))return false;
  const fields=['session','document','project','selection','active','display','desktop','preferences','language','host','documentOperation','mediaImport','audioEffects'];
  if(Object.hasOwn(value,'sessionNotice')){
    const notice=value.sessionNotice;
    if(!record(notice)||!keys(notice,['id','level','title','message'])||!sessionUuid(notice.id)||notice.id==='00000000-0000-0000-0000-000000000000'
      ||!one(notice.level,['information','warning','error'])||!text(notice.title)||!text(notice.message))return false;
    fields.push('sessionNotice');
  }
  if(!keys(value,fields))return false;
  if(!isAudioEffectsState(value.audioEffects))return false;
  if(!isDocumentOperation(value.documentOperation)||!isMediaImport(value.mediaImport))return false;
  const {session:s,document:d,project:p,selection:sel,active:a,display:dis,desktop:desk,preferences:pref,language:lang,host:h}=value;
  if(!record(s)||!keys(s,['incarnation','boundDisplayId'])||s.incarnation!==incarnation||!text(s.boundDisplayId)||!s.boundDisplayId)return false;
  if(!record(d)||!keys(d,['path','displayName','authoredRevision','cleanRevision','dirty'])||(d.path!==null&&!text(d.path))||!text(d.displayName)
    ||!uint64Revision(d.authoredRevision)||!uint64Revision(d.cleanRevision)||d.dirty!==(d.authoredRevision!==d.cleanRevision))return false;
  if(!record(p)||!keys(p,['displayOwner','canvasAspect','playlistMode','playlists'])||p.displayOwner!==s.boundDisplayId||!mode(p.playlistMode)
    ||!Array.isArray(p.canvasAspect)||p.canvasAspect.length!==2||!p.canvasAspect.every(n=>finite(n,1)&&Number.isInteger(n))||!Array.isArray(p.playlists))return false;
  const playlistIds=new Set<string>(),itemIds=new Set<string>(),itemOwner=new Map<string,string>();
  for(const pl of p.playlists){
    if(!record(pl)||!keys(pl,['id','alias','enabled','masterVolume','itemMode','items'])||!sessionUuid(pl.id)||playlistIds.has(pl.id)||!text(pl.alias)||!boolean(pl.enabled)||!finite(pl.masterVolume,0,1)||!mode(pl.itemMode)||!Array.isArray(pl.items))return false;
    playlistIds.add(pl.id);
    for(const item of pl.items){
      if(!record(item)||!keys(item,['id','alias','source','kind','enabled','duration','rate','mediaGain','presentation'])||!sessionUuid(item.id)||itemIds.has(item.id)||!text(item.alias)||!text(item.source)||!one(item.kind,['image','video'])||!boolean(item.enabled)||!finite(item.duration,0)||!finite(item.rate,Number.MIN_VALUE)||!finite(item.mediaGain,0,1)||!presentation(item.presentation))return false;
      itemIds.add(item.id);itemOwner.set(item.id,pl.id);
    }
  }
  for(const binding of [sel,a]){
    if(!record(binding)||!keys(binding,['playlistId','itemId'])||!nullableId(binding.playlistId)||!nullableId(binding.itemId)
      ||(binding.playlistId!==null&&!playlistIds.has(binding.playlistId as string))||(binding.itemId!==null&&itemOwner.get(binding.itemId as string)!==binding.playlistId))return false;
  }
  if(!record(dis)||!keys(dis,['status','descriptor'])||!one(dis.status,['success','api-failure','incomplete-path-metadata','duplicate-identity']))return false;
  const dd=dis.descriptor;
  if(dd!==null&&(!record(dd)||!keys(dd,['id','displayNumber','friendlyName','sourceDeviceName','desktopX','desktopY','pixelWidth','pixelHeight','primary','refreshHz'])||dd.id!==s.boundDisplayId||!finite(dd.displayNumber,1)||!Number.isInteger(dd.displayNumber)||!text(dd.friendlyName)||!text(dd.sourceDeviceName)||!finite(dd.desktopX)||!finite(dd.desktopY)||!finite(dd.pixelWidth,1)||!finite(dd.pixelHeight,1)||!boolean(dd.primary)||(dd.refreshHz!==null&&!finite(dd.refreshHz,Number.MIN_VALUE))))return false;
  if(dis.status!=='success'&&dd!==null)return false;
  if(!record(desk)||!keys(desk,['desired','targetRegistered','hostOwned','hostHealthy','busy','canDeploy','canUndeploy'])||!Object.values(desk).every(boolean))return false;
  if(!record(pref)||!keys(pref,['readStatus','highlight','background','backgroundReadStatus','persistedBackground'])||!one(pref.readStatus,['Success','Unavailable'])||!boolean(pref.highlight)||!one(pref.background,['cover','reveal'])||!one(pref.backgroundReadStatus,['Success','Failure'])||!one(pref.persistedBackground,['cover','reveal',null]))return false;
  if(!record(lang)||!keys(lang,['readStatus','committed','effective'])||!one(lang.readStatus,['Success','Failure'])||!one(lang.committed,['en','zh_CN',null])||!one(lang.effective,['en','zh_CN']))return false;
  return record(h)&&keys(h,['ready','sessionRoleReady','closing','ending','visible','minimized','maximized','hideEligible'])&&Object.values(h).every(boolean)&&h.ready===true&&h.sessionRoleReady===true;
}
