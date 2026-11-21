// The finite B3 editing boundary. These are intents and native outcomes; no
// frontend transition, revision owner or mutable authored cache lives here.
import {sessionUuid,uint64Revision} from './ProductionSessionIdentity.ts';
import type {SequenceMode} from './sessionPlaybackSequence.ts';
import {isAudioEffectReceipt,type AudioEffectReceipt} from './ProductionAudioEffects.ts';

export interface PresentationPatch {
  mode?:'native'|'auto-fill'|'custom';
  customAspect?:{numerator:number;denominator:number};
  scale?:number;
  positionX?:number;
  positionY?:number;
}
type PlaylistTarget={playlistId:string};
type ItemTarget=PlaylistTarget&{itemId:string};
export interface AuthoredPayloads {
  SelectPlaylist:PlaylistTarget;
  AddPlaylist:Record<string,never>;
  RenamePlaylist:PlaylistTarget&{alias:string;expectedAlias:string};
  RemovePlaylist:PlaylistTarget&{expectedAlias:string};
  MovePlaylist:PlaylistTarget&{direction:-1|1};
  SetPlaylistEnabled:PlaylistTarget&{enabled:boolean};
  SetProjectPolicy:{mode:SequenceMode};
  SetPlaylistPolicy:PlaylistTarget&{mode:SequenceMode};
  SetPlaylistMaster:PlaylistTarget&{value:number};
  SelectItem:ItemTarget;
  AddMedia:PlaylistTarget;
  RenameItem:ItemTarget&{alias:string;expectedAlias:string};
  RemoveItem:ItemTarget&{expectedAlias:string};
  MoveItem:ItemTarget&{direction:-1|1};
  SetItemEnabled:ItemTarget&{enabled:boolean};
  SetImageDuration:ItemTarget&{value:number};
  ApplyVideoRate:ItemTarget&{value:number};
  SetItemGain:ItemTarget&{value:number};
  SetItemPresentation:ItemTarget&{patch:PresentationPatch};
}
export type AuthoredCommand=keyof AuthoredPayloads;
export type AuthoredIntent={[K in AuthoredCommand]:{type:K;payload:AuthoredPayloads[K]}}[AuthoredCommand];
export type RuntimeOutcome='not-required'|'synchronized'|'unchanged'|'stopped'|'warning'|'failed'|'pending'|'cancelled'|'deferred-unavailable';
export interface AuthoredWarning {code:string;message:string}
export interface AuthoredResult {
  audioEffect?:AudioEffectReceipt;runtimeOnApplied?:RuntimeOutcome;
  status:'accepted'|'applied'|'no-change'|'not-found'|'wrong-parent'|'invalid-value'|'stale-value'|'stale-revision'|'busy'|'picker-cancelled'|'unavailable';
  playlistId:string|null;itemId:string|null;token:string|null;
  authored:'committed'|'unchanged'|'rejected'|'not-applicable';
  revision:string;dirty:boolean;runtime:RuntimeOutcome;
  positionRestore:'not-required'|'restored'|'rebound-at-start'|'failed';
  warnings:AuthoredWarning[];
  sources:{source:string;itemId:string|null;authored:'committed'|'unsupported'|'rejected';runtime:RuntimeOutcome;warnings:AuthoredWarning[]}[];
  playlistMasterVolume?:number;
}
export interface MediaImport {pending:null|{token:string;playlistId:string};last:AuthoredResult|null}
const record=(v:unknown):v is Record<string,unknown>=>typeof v==='object'&&v!==null&&!Array.isArray(v);
const exact=(v:Record<string,unknown>,names:string[])=>Object.keys(v).length===names.length&&names.every(k=>Object.hasOwn(v,k));
const one=(v:unknown,values:readonly unknown[])=>values.includes(v);
const text=(v:unknown):v is string=>typeof v==='string'&&v.length<=4*1024*1024;
const identity=(v:unknown)=>v===null||sessionUuid(v)&&v!=='00000000-0000-0000-0000-000000000000';
const runtime=(v:unknown)=>one(v,['not-required','synchronized','unchanged','stopped','warning','failed','pending','cancelled','deferred-unavailable']);
const warnings=(v:unknown)=>Array.isArray(v)&&v.every(w=>record(w)&&exact(w,['code','message'])&&text(w.code)&&text(w.message));
export function isAuthoredResult(value:unknown):value is AuthoredResult {
  if(!record(value)||!exact(value,['status','playlistId','itemId','token','authored','revision','dirty','runtime','positionRestore','warnings','sources',...(Object.hasOwn(value,'playlistMasterVolume')?['playlistMasterVolume']:[]),...(Object.hasOwn(value,'audioEffect')?['audioEffect']:[]),...(Object.hasOwn(value,'runtimeOnApplied')?['runtimeOnApplied']:[])]))return false;
  if(value.audioEffect!==undefined&&!isAudioEffectReceipt(value.audioEffect)||value.runtimeOnApplied!==undefined&&!runtime(value.runtimeOnApplied))return false;
  if(Object.hasOwn(value,'playlistMasterVolume')&&(!sessionUuid(value.playlistId)||typeof value.playlistMasterVolume!=='number'||!Number.isFinite(value.playlistMasterVolume)||value.playlistMasterVolume<0||value.playlistMasterVolume>1))return false;
  return one(value.status,['accepted','applied','no-change','not-found','wrong-parent','invalid-value','stale-value','stale-revision','busy','picker-cancelled','unavailable'])
    &&identity(value.playlistId)&&identity(value.itemId)&&identity(value.token)&&(value.itemId===null||value.playlistId!==null)
    &&(value.status!=='accepted'||sessionUuid(value.token))
    &&one(value.authored,['committed','unchanged','rejected','not-applicable'])&&uint64Revision(value.revision)&&typeof value.dirty==='boolean'
    &&runtime(value.runtime)&&one(value.positionRestore,['not-required','restored','rebound-at-start','failed'])&&warnings(value.warnings)
    &&Array.isArray(value.sources)&&value.sources.every(s=>record(s)&&exact(s,['source','itemId','authored','runtime','warnings'])
      &&text(s.source)&&identity(s.itemId)&&one(s.authored,['committed','unsupported','rejected'])&&runtime(s.runtime)&&warnings(s.warnings));
}
export function isMediaImport(value:unknown):value is MediaImport {
  return record(value)&&exact(value,['pending','last'])&&(value.pending===null||record(value.pending)&&exact(value.pending,['token','playlistId'])
    &&sessionUuid(value.pending.token)&&identity(value.pending.token)&&sessionUuid(value.pending.playlistId)&&identity(value.pending.playlistId))
    &&(value.last===null||isAuthoredResult(value.last));
}
