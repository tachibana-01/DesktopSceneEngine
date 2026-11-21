import {useState} from 'react';
import {SessionModal} from './SessionModal';
import {useSessionText} from './sessionLanguage';
import type {AuthoredIntent} from './ProductionAuthoredTypes.ts';
import type {SessionReadState} from './sessionReadProjection.ts';
import {findItem,findPlaylist} from './sessionReadProjection.ts';

// A menu/dialog captures canonical IDs and the alias it presented. Native
// revalidates existence, membership and expectedAlias at actual submission.
export interface ProductionEntityTarget {
  entity:'playlist'|'item';playlistId:string;itemId?:string;
  alias:string;x:number;y:number;
}
export function ProductionEntityCommands({target,state,disabled,onIntent,onClose,playbackAvailable=false,onPlay,onConfigureBgm}:{
  target:ProductionEntityTarget;state:SessionReadState;disabled:boolean;
  onIntent:(intent:AuthoredIntent)=>Promise<void>;onClose:()=>void;
  playbackAvailable?:boolean;onPlay?:()=>void;onConfigureBgm?:()=>void;
}){
  const t=useSessionText();
  const [dialog,setDialog]=useState<'menu'|'rename'|'delete'>('menu'),[alias,setAlias]=useState(target.alias);
  const playlist=findPlaylist(state,target.playlistId),item=target.itemId?findItem(state,target.itemId)?.item:undefined;
  const entity=target.entity==='playlist'?playlist:item;
  const playable=playlist?.enabled&&(target.entity==='playlist'?playlist.items.some(entry=>entry.enabled):item?.enabled);
  const siblings=target.entity==='playlist'?state.project.playlists:playlist?.items??[];
  const index=siblings.findIndex(value=>value.id===(target.itemId??target.playlistId));
  function send(intent:AuthoredIntent){if(disabled)return;onClose();void onIntent(intent);}
  function rename(){
    send(target.entity==='playlist'?{type:'RenamePlaylist',payload:{playlistId:target.playlistId,alias,expectedAlias:target.alias}}
      :{type:'RenameItem',payload:{playlistId:target.playlistId,itemId:target.itemId!,alias,expectedAlias:target.alias}});
  }
  if(dialog==='rename')return <SessionModal key="rename" title={t(target.entity==='playlist'?'Rename Playlist':'Rename Item')} onClose={onClose} initialFocus="[data-rename-value]">
    <form onSubmit={event=>{event.preventDefault();rename();}}>
      <label className="session-dialog-label" htmlFor="production-entity-alias">{t('Name')}</label>
      <input id="production-entity-alias" type="text" data-rename-value value={alias} disabled={disabled} onChange={event=>setAlias(event.target.value)}/>
      <div className="session-dialog-actions"><button type="button" data-dialog-cancel onClick={onClose}>{t('Cancel')}</button><button type="submit" className="session-dialog-primary" data-dialog-confirm disabled={disabled}>{t('OK')}</button></div>
    </form>
  </SessionModal>;
  if(dialog==='delete')return <SessionModal key="delete" title={t(target.entity==='playlist'?'Delete Playlist':'Remove Item')} onClose={onClose} initialFocus="[data-dialog-cancel]">
    <p>{t(target.entity==='playlist'?'Delete playlist “%1”?':'Remove “%1” from the playlist?').replace('%1',()=>target.alias)}</p>
    {target.entity==='item'&&<p className="session-dialog-note">{t('The source file will not be deleted.')}</p>}
    <div className="session-dialog-actions"><button type="button" data-dialog-confirm disabled={disabled} onClick={()=>send(target.entity==='playlist'
      ?{type:'RemovePlaylist',payload:{playlistId:target.playlistId,expectedAlias:target.alias}}
      :{type:'RemoveItem',payload:{playlistId:target.playlistId,itemId:target.itemId!,expectedAlias:target.alias}})}>{t('Yes')}</button><button type="button" data-dialog-cancel onClick={onClose}>{t('Cancel')}</button></div>
  </SessionModal>;
  return <SessionModal key="menu" title={target.alias+' '+t('commands')} className="session-command-menu" position={target} onClose={onClose}>
    <div role="menu" aria-label={t(target.entity+' commands')}>
      <button role="menuitem" type="button" data-command="play" disabled={!playbackAvailable||!playable}
        onClick={()=>{onClose();onPlay?.();}}>{t('Play')}</button><hr/>
      <button role="menuitem" type="button" data-command="availability" disabled={disabled||!entity||target.entity==='item'&&!playlist?.enabled} onClick={()=>entity&&send(target.entity==='playlist'
        ?{type:'SetPlaylistEnabled',payload:{playlistId:target.playlistId,enabled:!entity.enabled}}
        :{type:'SetItemEnabled',payload:{playlistId:target.playlistId,itemId:target.itemId!,enabled:!entity.enabled}})}>{t(entity?.enabled?'Disable':'Enable')}</button>
      {([-1,1] as const).map(direction=><button key={direction} role="menuitem" type="button" data-command={direction<0?'move-up':'move-down'} disabled={disabled||index<0||index+direction<0||index+direction>=siblings.length}
        onClick={()=>send(target.entity==='playlist'?{type:'MovePlaylist',payload:{playlistId:target.playlistId,direction}}
          :{type:'MoveItem',payload:{playlistId:target.playlistId,itemId:target.itemId!,direction}})}>{t(direction<0?'Move Up':'Move Down')}</button>)}
      <hr/><button role="menuitem" type="button" data-command="rename" disabled={disabled||!entity} onClick={()=>setDialog('rename')}>{t('Rename')}</button>
      {target.entity==='playlist'&&<button role="menuitem" type="button" data-command="background-music" disabled={disabled||!playlist||!onConfigureBgm} onClick={()=>{onClose();onConfigureBgm?.();}}>{t('Configure Background Music…')}</button>}
      <button role="menuitem" type="button" data-command="delete" disabled={disabled||!entity} onClick={()=>setDialog('delete')}>{t('Delete')}</button>
    </div>
  </SessionModal>;
}
