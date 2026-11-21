import {useCallback,useEffect,useId,useLayoutEffect,useRef,useState,type KeyboardEvent} from 'react';
import {createPortal} from 'react-dom';
import {useSessionText} from './sessionLanguage.tsx';
import {ProductTitleBar} from './ProductTitleBar.tsx';
import {useNativeRangeDrag} from './useNativeRangeDrag.ts';
import type {BgmEdit,BgmQueue,BgmRuntimeState,BgmScope,BgmSnapshot} from './ProductionBgmFacade.ts';
import './backgroundMusic.css';

export function BgmText({text,className=''}:{text:string;className?:string}){
  const anchor=useRef<HTMLSpanElement>(null),id=useId();
  const [point,setPoint]=useState<{left:number;top:number}|null>(null);
  function show(){const r=anchor.current?.getBoundingClientRect();if(r)setPoint({left:Math.max(12,Math.min(r.left,window.innerWidth-412)),top:Math.max(8,Math.min(r.bottom+6,window.innerHeight-90))});}
  useEffect(()=>{if(!point)return;const hide=()=>setPoint(null),key=(event:globalThis.KeyboardEvent)=>{if(event.key==='Escape')hide();};window.addEventListener('resize',hide);window.addEventListener('scroll',hide,true);window.addEventListener('keydown',key);return()=>{window.removeEventListener('resize',hide);window.removeEventListener('scroll',hide,true);window.removeEventListener('keydown',key);};},[point]);
  return <><span ref={anchor} className={'bgm-track-name '+className} tabIndex={0} aria-describedby={point?id:undefined} onMouseEnter={show} onMouseLeave={()=>setPoint(null)} onFocus={show} onBlur={()=>setPoint(null)}>{text}</span>
    {point&&createPortal(<span id={id} role="tooltip" className="bgm-filename-tooltip" style={point}>{text}</span>,document.body)}</>;
}

// The thumb owns a gesture draft, never authored truth. Native scalar readback
// can update behind a held pointer without replacing the input or ending drag.
export function BgmVolume({label,value,disabled,onChange,kind,trackId}:{label:string;value:number;disabled:boolean;onChange:(value:number)=>Promise<void>;kind:'queue'|'track';trackId?:string}){
  const [draft,setDraft]=useState<number|null>(null),pending=useRef(0),settled=useRef(0),mounted=useRef(true);
  const capture=useNativeRangeDrag();
  useEffect(()=>()=>{mounted.current=false;},[]);
  const shown=draft??Math.round(value*100);
  function change(next:number){
    if(disabled||capture.ignoresInput())return;
    setDraft(next);const request=++pending.current;
    void onChange(next/100).finally(()=>{if(request===pending.current){settled.current=request;if(mounted.current&&!capture.active())setDraft(null);}});
  }
  const cancel=useCallback(()=>{
    if(capture.cancel()&&settled.current===pending.current)setDraft(null);
  },[capture]);
  useLayoutEffect(()=>{if(disabled)cancel();},[disabled,cancel]);
  useLayoutEffect(()=>{
    const visibility=()=>{if(document.hidden)cancel();};
    window.addEventListener('blur',cancel);window.addEventListener('pagehide',cancel);
    document.addEventListener('visibilitychange',visibility);
    return ()=>{
      window.removeEventListener('blur',cancel);window.removeEventListener('pagehide',cancel);
      document.removeEventListener('visibilitychange',visibility);
    };
  },[cancel]);
  return <label className={'bgm-volume bgm-volume--'+kind}><span>{label}</span><output>{shown}%</output>
    <input type="range" data-bgm-control={kind+'-volume'} data-track-id={trackId} aria-label={label} aria-valuetext={shown+'%'} min="0" max="100" step="1" disabled={disabled} value={shown}
      onPointerDown={event=>{capture.start(event,false);}}
      onPointerMove={event=>{if(event.pointerType==='mouse'&&!(event.buttons&1)&&capture.owns(event))cancel();}}
      onPointerUp={event=>{if(capture.owns(event)){capture.end();if(settled.current===pending.current)setDraft(null);}}}
      onPointerCancel={event=>{if(capture.owns(event))cancel();}} onLostPointerCapture={event=>{if(capture.owns(event))cancel();}}
      onBlur={cancel} onKeyDown={capture.onKeyDown}
      onChange={event=>change(Number(event.currentTarget.value))}/></label>;
}

export function bgmStatus(queue:BgmQueue|null,runtime:BgmRuntimeState|null){
  if(!queue)return 'No Item selected';
  if(!queue.enabled)return 'Disabled';
  if(!runtime)return 'Loading runtime…';
  if(!runtime.active)return 'Not running';
  if(runtime.phase==='no-playable-track')return 'No playable track';
  if(runtime.phase==='pending-start')return 'Starting';
  if(runtime.phase==='waiting-for-executor')return 'Waiting';
  if(runtime.playbackState==='paused'&&runtime.currentTrackId)return 'Paused';
  if(runtime.phase==='playing')return 'Playing';
  return queue.tracks.length?'No Current track':'Empty queue';
}
export function BgmScopeColumn({scope,context,queue,runtime,selected,onSelect,onEdit,onUse,disabled,pending}:{
  scope:BgmScope;context:string;queue:BgmQueue|null;runtime:BgmRuntimeState|null;selected:string|null;
  onSelect:(id:string)=>void;onEdit:(edit:BgmEdit)=>Promise<void>;onUse:(id:string)=>Promise<void>;disabled:boolean;pending:boolean;
}){
  const t=useSessionText(),heading=useId(),rows=useRef(new Map<string,HTMLButtonElement>());
  const title=t(scope==='playlist'?'Playlist Global BGM':'Item BGM'),tracks=queue?.tracks??[];
  const current=tracks.find(track=>track.id===runtime?.currentTrackId),chosen=tracks.find(track=>track.id===selected);
  const selectedIndex=tracks.findIndex(track=>track.id===selected),tabTrack=chosen?.id??tracks[0]?.id;
  const canUse=Boolean(queue?.enabled&&runtime?.active&&runtime.exists&&runtime.switchEligible&&runtime.currentTrackId&&runtime.phase!=='no-playable-track'&&chosen&&chosen.id!==runtime.currentTrackId);
  function rowKey(event:KeyboardEvent<HTMLButtonElement>,index:number){let next=index;if(event.key==='ArrowDown')next=Math.min(index+1,tracks.length-1);else if(event.key==='ArrowUp')next=Math.max(index-1,0);else if(event.key==='Home')next=0;else if(event.key==='End')next=tracks.length-1;else return;event.preventDefault();onSelect(tracks[next].id);rows.current.get(tracks[next].id)?.focus();rows.current.get(tracks[next].id)?.scrollIntoView({block:'nearest'});}
  return <section className="bgm-scope" data-scope={scope} data-current-track-id={runtime?.currentTrackId??''} data-phase={runtime?.phase??''} aria-labelledby={heading}>
    <header className="bgm-scope-header"><h2 id={heading}>{title}</h2><p className="bgm-scope-context" title={context}>{context}</p></header>
    <section className="bgm-current" aria-label={title+' '+t('Current')}>
      <h3>{t('CURRENT')}</h3><p className={'bgm-current-name'+(!current?' is-empty':'')}>{current?<BgmText text={current.alias||current.source}/>:t('No Current track')}</p>
      <p className="bgm-current-duration" data-bgm-runtime-status>{t(bgmStatus(queue,runtime))}</p>
    </section>
    <div className="bgm-config"><label><input type="checkbox" data-bgm-control="enabled" checked={queue?.enabled??false} disabled={disabled||!queue} onChange={event=>void onEdit({type:'BgmSetEnabled',enabled:event.currentTarget.checked})}/>{t('Enabled')}</label>
      <label className="bgm-mode"><span className="sr-only">{title+' '+t('Queue Mode')}</span><select aria-label={title+' '+t('Queue Mode')} data-bgm-control="mode" value={queue?.mode??'list-loop'} disabled={disabled||!queue} onChange={event=>void onEdit({type:'BgmSetQueueMode',mode:event.currentTarget.value as BgmQueue['mode']})}>
        <option value="current-single-loop">{t('Current Single Loop')}</option><option value="list-loop">{t('List Loop')}</option><option value="shuffle">{t('Shuffle')}</option>
      </select></label></div>
    <BgmVolume label={title+' '+t('Queue Volume')} kind="queue" value={queue?.volume??0} disabled={disabled||!queue} onChange={value=>onEdit({type:'BgmSetQueueVolume',value})}/>
    <div className="bgm-list-heading"><h3>{t('MUSIC')}</h3><span>{String(tracks.length).padStart(2,'0')}</span></div>
    <ul className="bgm-track-list" aria-label={title+' '+t('tracks')}>
      {!queue?<li className="bgm-empty">{t('No Item selected')}</li>:!tracks.length?<li className="bgm-empty">{t('No music added')}</li>:tracks.map((track,index)=><li key={track.id} className="bgm-track-entry" data-track-id={track.id} data-selected={selected===track.id} data-current={runtime?.currentTrackId===track.id} data-failed={runtime?.lastFailedAttempt?.trackId===track.id}>
        <button ref={node=>{if(node)rows.current.set(track.id,node);else rows.current.delete(track.id);}} type="button" className="bgm-track-row" data-track-id={track.id} tabIndex={tabTrack===track.id?0:-1} aria-pressed={selected===track.id} aria-current={runtime?.currentTrackId===track.id?'true':undefined} aria-label={track.alias||track.source}
          onClick={()=>onSelect(track.id)} onKeyDown={event=>rowKey(event,index)}><span className="bgm-track-name" title={track.alias}>{track.alias||track.source}</span>
          <span className="bgm-current-marker">{runtime?.currentTrackId===track.id?t('CURRENT'):runtime?.lastFailedAttempt?.trackId===track.id?t('Failed attempt'):''}</span></button>
        <div className="bgm-track-details"><BgmText text={track.source} className="bgm-track-source"/>
          <BgmVolume label={t('Track Volume')+' · '+(track.alias||track.source)} kind="track" trackId={track.id} value={track.volume} disabled={disabled} onChange={value=>onEdit({type:'BgmSetTrackVolume',trackId:track.id,value})}/></div>
      </li>)}
    </ul>
    <div className="bgm-selection"><p>{chosen?<BgmText text={chosen.alias||chosen.source}/>:t('No track selected')}</p>
      <span>{runtime?.lastFailedAttempt?<span data-bgm-failure>{t('Failed attempt')+': '+(tracks.find(track=>track.id===runtime.lastFailedAttempt?.trackId)?.alias||runtime.lastFailedAttempt.trackId)+' · '+runtime.lastFailedAttempt.code}</span>:t(chosen?.id===current?.id&&chosen?'Current track':'Selection does not start playback')}</span></div>
    <div className="bgm-actions"><button className="bgm-use" type="button" data-bgm-control="use" disabled={disabled||!canUse} onClick={()=>chosen&&void onUse(chosen.id)}>{t('Use now')}<span aria-hidden="true">⟶</span></button>
      <button type="button" className="bgm-move" data-bgm-control="move-up" disabled={disabled||selectedIndex<=0} onClick={()=>chosen&&void onEdit({type:'BgmMoveTrack',trackId:chosen.id,direction:-1})} aria-label={title+' '+t('Move Up')}>↑</button>
      <button type="button" className="bgm-move" data-bgm-control="move-down" disabled={disabled||selectedIndex<0||selectedIndex>=tracks.length-1} onClick={()=>chosen&&void onEdit({type:'BgmMoveTrack',trackId:chosen.id,direction:1})} aria-label={title+' '+t('Move Down')}>↓</button>
      <button className="bgm-remove" type="button" data-bgm-control="remove" disabled={disabled||!chosen} onClick={()=>chosen&&void onEdit({type:'BgmRemoveTrack',trackId:chosen.id})}>{t('Remove')}</button></div>
    <button className="bgm-add" type="button" data-bgm-control="add" disabled={disabled||!queue||pending} onClick={()=>void onEdit({type:'BgmAddTracks'})}><span aria-hidden="true">＋</span>{t(pending?'Native picker pending…':'Add Music')}</button>
  </section>;
}

// Presentation over detached native values; the historical fixture adapter is
// separate and reachable only through the explicit DEV PrototypeApp import.
export function BackgroundMusicShell({snapshot,playlistRuntime,itemRuntime,playlistSelection,itemSelection,onSelect,onItem,onEdit,onUse,notice,unavailable}:{
  snapshot:BgmSnapshot;playlistRuntime:BgmRuntimeState|null;itemRuntime:BgmRuntimeState|null;playlistSelection:string|null;itemSelection:string|null;
  onSelect:(scope:BgmScope,id:string)=>void;onItem:(id:string|null)=>void;onEdit:(scope:BgmScope,edit:BgmEdit)=>Promise<void>;onUse:(scope:BgmScope,id:string)=>Promise<void>;notice:string;unavailable:boolean;
}){
  const t=useSessionText();
  return <div className="background-music-shell bgm-production" data-bootstrap-state="ready" data-playlist-id={snapshot.playlist.id} data-document-incarnation={snapshot.documentIncarnation} data-revision={snapshot.revision} data-dirty={snapshot.dirty} data-selected-item-id={snapshot.selectedItemId??''} data-language={snapshot.language}>
    <ProductTitleBar text={t} beforeDrag={<span className="bgm-chrome-title">{t('Background Music')} — {snapshot.playlist.alias}</span>} />
    <main className="bgm-scopes" aria-label={t('Background music configuration')}>
      <BgmScopeColumn scope="playlist" context={snapshot.playlist.alias} queue={snapshot.playlistQueue} runtime={playlistRuntime} selected={playlistSelection} onSelect={id=>onSelect('playlist',id)} onEdit={edit=>onEdit('playlist',edit)} onUse={id=>onUse('playlist',id)} disabled={snapshot.busy||unavailable} pending={snapshot.pending?.scope==='playlist'}/>
      <div className="bgm-item-pane"><label className="bgm-item-chooser">{t('Edit Item BGM')}<select data-bgm-control="item-chooser" aria-label={t('Edit Item BGM')} value={snapshot.selectedItemId??''} disabled={!snapshot.items.length||unavailable} onChange={event=>onItem(event.currentTarget.value||null)}>
        {!snapshot.items.length&&<option value="">{t('No Item selected')}</option>}{snapshot.items.map(item=><option key={item.id} value={item.id}>{item.alias}</option>)}</select></label>
        <BgmScopeColumn key={snapshot.selectedItemId} scope="item" context={snapshot.items.find(item=>item.id===snapshot.selectedItemId)?.alias??t('No Item selected')} queue={snapshot.itemQueue} runtime={itemRuntime} selected={itemSelection} onSelect={id=>onSelect('item',id)} onEdit={edit=>onEdit('item',edit)} onUse={id=>onUse('item',id)} disabled={snapshot.busy||unavailable} pending={snapshot.pending?.scope==='item'}/></div>
    </main>
    <footer className="bgm-footer"><span className="bgm-footer-left">{t(snapshot.dirty?'Unsaved changes':'Saved')}<span>{t('Queue Volume × Track Volume')}</span></span><span role="status" aria-live="polite" data-bgm-notice>{unavailable?t('Native BGM editor unavailable'):notice}</span></footer>
  </div>;
}
