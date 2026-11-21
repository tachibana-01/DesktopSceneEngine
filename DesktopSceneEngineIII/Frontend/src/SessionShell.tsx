import { useEffect, useState } from 'react';
import { TitleBar } from './ShellTitleBar';
import { documentName, isDirty } from './sessionDocumentMock.ts';
import { findItem, findPlaylist, itemPlayable, playlistPlayable } from './sessionEntities.ts';
import { readSessionScenario, sessionScenarios } from './sessionFixtures';
import { SessionLibrary, type EntityMenuTarget } from './SessionLibrary';
import { projectionAttributes, SessionDecisionDialog, SessionModal } from './SessionDialogs';
import { useSessionPrototype } from './useSessionPrototype';
import { SessionInspector } from './SessionInspector';
import { SessionPlaybackRail } from './SessionPlaybackRail';
import { SessionTimeline, timelineDevAction, type TimelineDevAction } from './SessionTimeline';
import { SessionPreview } from './SessionPreview';
import { sessionDisplay, sessionView } from './sessionPresentationModel.ts';
import { displayFacts, createSessionDisplay } from './sessionDisplayModel.ts';
import {hideFacts} from './sessionHideModel.ts';
import {useSessionLayout} from './SessionSplitters';
import {SessionLanguageProvider,translateSession,displayText} from './sessionLanguage';
import {documentShortcut,type ProjectCommand} from './sessionShortcuts.ts';
import { SessionSettings } from './SessionSettings';
import './sessionRemaining.css';
import './session.css';
import './sessionPhaseA.css';

export function SessionShell() {
  const mock = useSessionPrototype();
  const { state } = mock;
  const view = sessionView(state);
  const t=(text:string)=>translateSession(mock.language.effective,text);
  const layout=useSessionLayout(mock.timelineEpoch,mock.scenario==='custom-layout-left-wide'?'left-wide':mock.scenario==='custom-layout-inspector-wide'?'inspector-wide':undefined,mock.language.effective);
  const boundDisplay = displayFacts(sessionDisplay(state), mock.lifecycle);
  const [menu, setMenu] = useState<EntityMenuTarget | { entity: 'project'; x: number; y: number } | null>(null);
  const [devOpen, setDevOpen] = useState(false);
  const [notice, setNotice] = useState<{ message: string } | null>(null);
  const dirty = isDirty(state);
  const hide=hideFacts(mock.hide,Boolean(menu||devOpen||mock.dialog||state.view?.warning||mock.hide.modalBlocked),mock.lifecycle);
  const hideReason=hide.reason==='modal-blocked'?'Modal interaction active':hide.reason==='close-pending'?'Close pending':hide.reason==='ending'?'Session ending':hide.reason==='window-unavailable'?'Window unavailable':'No restore eligibility';
  useEffect(()=>{if(!mock.language.pending)return;const timer=window.setTimeout(()=>mock.languageEvent({type:'flush'}),0);return()=>window.clearTimeout(timer);},[mock.language.pending]);
  useEffect(() => {
    if (!notice) return;
    const timer = window.setTimeout(() => setNotice(null), 2400);
    return () => window.clearTimeout(timer);
  }, [notice]);
  useEffect(() => {
    function shortcut(event: globalThis.KeyboardEvent) {
      const command=documentShortcut(event);if(!command)return;
      event.preventDefault();
      if(state.closed||document.querySelector('dialog[open]')||mock.hide.hidden)return;
      runProject(command);
    }
    window.addEventListener('keydown', shortcut);
    return () => window.removeEventListener('keydown', shortcut);
  });
  function select(entity: 'playlist' | 'item', id: string) {
    // Qt uses currentItemChanged: clicking the already-current Playlist does
    // not call the model's same-ID selectPlaylist (which would clear its Item).
    mock.select(entity, id);
  }
  function openMenu(target: EntityMenuTarget) { setMenu(target); }
  function runProject(command: ProjectCommand) {
    setMenu(null);
    if (command === 'new' || command === 'close') mock.request({ kind: command });
    else if (command === 'open') mock.setDialog({ kind: 'open' });
    else if (command === 'save-as') mock.setDialog({ kind: 'save-as', pending: null });
    else mock.save();
  }
  function requestHide(){
    const blocked=Boolean(document.querySelector('dialog[open]')||state.view?.warning)||mock.hide.modalBlocked;
    let issued=mock.hideAction({type:'request',modalBlocked:blocked});
    if(!issued.pending)return;
    const request=issued.pending.request,race=state.hideMock?.race;
    if(race==='show')issued=mock.hideAction({type:'restore'});
    if(race==='recovery')issued=mock.hideAction({type:'facts',recovery:{recoveryQualified:false}});
    if(race==='expired')issued=mock.hideAction({type:'advance',milliseconds:3000});
    if(race==='modal')issued=mock.hideAction({type:'facts',facts:{modalBlocked:true}});
    const applied=mock.hideAction({type:'permit',request,allowed:race!=='denied',modalBlocked:blocked||issued.modalBlocked});
    if(applied.hidden)setNotice({message:t('Hide to tray')+' — visual prototype only'});
  }
  function entityMenu() {
    if (!menu || menu.entity === 'project') return null;
    const targetMenu = menu;
    const playlist = menu.entity === 'playlist' ? findPlaylist(state, menu.id) : findItem(state, menu.id)?.playlist;
    const item = menu.entity === 'item' ? findItem(state, menu.id)?.item : undefined;
    const target = item ?? playlist;
    if (!target) return null;
    const playable = menu.entity === 'playlist' ? playlistPlayable(playlist) : itemPlayable(playlist, item);
    const siblings = menu.entity === 'playlist' ? state.project.playlists : playlist?.items ?? [];
    const targetIndex = siblings.findIndex(entry => entry.id === targetMenu.id);
    return <SessionModal title={target.alias + ' '+t('commands')} className="session-command-menu" position={menu} onClose={() => setMenu(null)}>
      <div role="menu" aria-label={t(menu.entity + ' commands')}>
        <button role="menuitem" type="button" data-command="play" disabled={!playable} onClick={() => {
          setMenu(null); mock.entity({ type: targetMenu.entity === 'playlist' ? 'play-playlist' : 'play-item', id: targetMenu.id });
        }}>{t('Play')}</button>
        <hr />
        <button role="menuitem" type="button" data-command="availability" disabled={menu.entity === 'item' && !playlist?.enabled} onClick={() => {
          setMenu(null); mock.entity({ type: targetMenu.entity === 'playlist' ? 'set-playlist-enabled' : 'set-item-enabled', id: targetMenu.id, enabled: !target.enabled });
        }}>{t(target.enabled ? 'Disable' : 'Enable')}</button>
        {([-1, 1] as const).map(direction => <button key={direction} role="menuitem" type="button" data-command={direction < 0 ? 'move-up' : 'move-down'}
          disabled={targetIndex < 0 || targetIndex + direction < 0 || targetIndex + direction >= siblings.length} onClick={() => {
            setMenu(null); mock.entity({type: targetMenu.entity === 'playlist' ? 'move-playlist' : 'move-item', id: targetMenu.id, direction});
          }}>{t(direction < 0 ? 'Move Up' : 'Move Down')}</button>)}
        <hr />
        <button role="menuitem" type="button" data-command="rename" onClick={() => { setMenu(null); mock.setDialog({ kind: 'rename', entity: targetMenu.entity, id: targetMenu.id }); }}>{t('Rename')}</button>
        {menu.entity === 'playlist' && <button role="menuitem" type="button" data-command="background-music" onClick={() => {
          setMenu(null); mock.configureBackgroundMusic(targetMenu.id);
        }}>{t('Configure Background Music…')}</button>}
        <button role="menuitem" type="button" data-command="delete" onClick={() => { setMenu(null); mock.setDialog({ kind: 'delete', entity: targetMenu.entity, id: targetMenu.id }); }}>{t('Delete')}</button>
      </div>
    </SessionModal>;
  }
  return <SessionLanguageProvider language={mock.language.effective}><div className={'session-shell session-phase-a session-phase-d'+(mock.hide.hidden?' session-mock-hidden':'')} data-language={mock.language.effective} data-language-desired={mock.language.desired} data-close-pending={mock.lifecycle.closePending} data-ending={mock.lifecycle.ending} data-dirty={dirty} {...projectionAttributes(state)}>
    <TitleBar text={t} hideState={mock.hide} hideEligible={hide.eligible} hideReason={hideReason} onHide={requestHide} onFeedback={message => setNotice({ message })} onClose={() => runProject('close')}
      leftActions={<><SessionSettings onResetLayout={layout.reset} resetAvailable={!state.closed}
        highlighted={view.highlighted} highlightAvailable={!state.closed}
        onHighlight={value => mock.presentation({scope:'view',action:{type:'highlight',value}})} />
        {mock.hide.hidden&&<span className="session-hidden-mock" role="status"><span>{t('Hidden Session · mock')}</span><button type="button" data-session-restore onClick={()=>mock.hideAction({type:'restore'})}>{t('Restore Session · mock')}</button></span>}</>} context={<>
      <button type="button" className="session-project-trigger" aria-label={t('Project commands')} aria-haspopup="menu"
        disabled={state.closed} title={state.document.path ?? t('Untitled')} onClick={event => {
          const r = event.currentTarget.getBoundingClientRect(); setMenu({ entity: 'project', x: r.left, y: r.bottom + 6 });
        }}>
        <span className="session-project-name">{state.document.path?documentName(state):t('Untitled')}</span>
        {dirty && <span className="project-dirty" role="img" aria-label={t('Unsaved changes')} />}
        <svg width="10" height="10" viewBox="0 0 12 12" fill="none" aria-hidden="true"><path d="m3 4.5 3 3 3-3" /></svg>
      </button>
      <span className="session-context-dot">·</span><span className="session-title-display" title={displayText(mock.language.effective,boundDisplay.label)}>{t('DISPLAY')} {boundDisplay.descriptor?.displayNumber ?? '—'}</span>
    </>} />
    {state.closed ? <main className="session-workspace session-closed"><div><h1>{t('Session closed.')}</h1><p>{t('Mock close result. This browser remains open.')}</p></div></main>
      : <main className="session-workspace" ref={layout.workspaceRef} style={layout.workspaceStyle} {...layout.attributes}>
        <SessionLibrary state={state} onSelect={select} onPlay={(entity, id) => mock.entity({ type: entity === 'playlist' ? 'play-playlist' : 'play-item', id })}
          onMenu={openMenu} onAddPlaylist={() => mock.entity({ type: 'add-playlist' })} onAddMedia={() => mock.setDialog({ kind: 'add-media' })}
          onProjectMode={mode => mock.playback({ type: 'project-mode', mode })} onItemMode={(id, mode) => mock.playback({ type: 'playlist-item-mode', id, mode })}
          onMasterValue={mock.setPlaylistMasterPercent} onMasterToggle={mock.togglePlaylistMaster} />
        <SessionPreview mock={mock} onResetLayout={layout.reset}>
          <SessionTimeline mock={mock} />
          <SessionPlaybackRail mock={mock} notice={notice?.message ?? ''} />
        </SessionPreview>
        <SessionInspector mock={mock} onMenu={openMenu} />{layout.separators}
      </main>}
    <button type="button" className="session-dev-trigger" aria-label="Dev Session scenarios" onClick={() => setDevOpen(true)}>DEV STATE</button>
    {menu?.entity === 'project' && <SessionModal title={t('Project commands')} className="session-command-menu" position={menu} onClose={() => setMenu(null)}>
      <div role="menu" aria-label={t('Project commands')}>
        {([['new', 'New', 'Ctrl+N'], ['open', 'Open…', 'Ctrl+O'], ['save', 'Save', 'Ctrl+S'], ['save-as', 'Save As…', 'Ctrl+Shift+S'], ['close', 'Close Session', '']] as const)
          .map(([command, label, shortcut]) => <button key={command} type="button" role="menuitem" data-command={command} onClick={() => runProject(command)}>
            <span>{t(label)}</span><span className="session-menu-shortcut">{shortcut}</span>
          </button>)}
      </div>
    </SessionModal>}
    {entityMenu()}
    {mock.dialog && <SessionDecisionDialog key={mock.dialog.kind + ('id' in mock.dialog ? mock.dialog.id : '')} mock={mock} />}
    {devOpen && <SessionModal title="DEV STATE · UI prototype" onClose={() => setDevOpen(false)} initialFocus="select">
      <label className="session-dialog-label" htmlFor="session-scenario">Mock scenario</label>
      <select id="session-scenario" aria-label="Dev Session scenario" value={mock.scenario} onChange={event => mock.reset(readSessionScenario(event.target.value))}>
        {sessionScenarios.map(([value, label]) => <option key={value} value={value}>{label}</option>)}
      </select>
      <p className="session-dialog-note">All state is a UI mock projection of current Player behavior. No real files or Session are changed.</p>
      <p className="session-dialog-note">Real native Preview/deployment, tray/Coordinator and BGM internals remain outside this semantic mock.</p>
      <div className="playback-dev-facts" aria-label="Runtime fact simulation"><button type="button" data-runtime-fact="complete" disabled={!state.runtime} onClick={() => { setDevOpen(false); mock.playback({ type: 'natural-completion' }); }}>Complete active Item</button><button type="button" data-runtime-fact="advance" disabled={!state.runtime} onClick={() => { setDevOpen(false); mock.playback({ type: 'advance-time', seconds: 5 }); }}>Advance 5 s</button></div>
      <details className="timeline-dev-lab"><summary>Timeline lab · deterministic facts</summary>
        <p className="session-dialog-note">Hover uses a 75 ms single-shot timer and mock frames. Loading holds the result; out-of-order uses 600/120 ms. Stale-drag fixtures inject a runtime change 500 ms after Press. Manual clock: add &amp;timelineClock=manual to the URL.</p>
        <div className="playback-dev-facts">{([['9999','Commit 9999'],['10000','Commit 10000'],['advance74','+74 ms'],['advance1','+1 ms'],['advance75','+75 ms'],['resolve-latest','Resolve latest'],['resolve-oldest','Resolve oldest'],['invalidate','Invalidate generation'],['hide','Hide hover'],['external-next','Runtime Next'],['duration','Change runtime duration'],['complete','Runtime completion']] as [TimelineDevAction,string][]).map(([action,label]) => <button type="button" key={action} data-timeline-dev={action} onClick={() => { timelineDevAction(action); if (action === '9999' || action === '10000') setDevOpen(false); }}>{label}</button>)}</div>
      </details>
      <details className="timeline-dev-lab"><summary>Display / View lab · mock native facts</summary>
        <p className="session-dialog-note">Bound identity stays fixed. These inject delivered platform facts, not a Session Display selector or Refresh command.</p>
        <div className="playback-dev-facts">{(['disappear','return','enumeration-failure','host-lost','allow-cleanup','continuation','external-background'] as const).map(action => <button type="button" key={action} data-presentation-dev={action} onClick={() => {
          const currentDisplay = sessionDisplay(state);
          if (action === 'external-background') mock.presentation({scope:'view',action:{type:'external-background',id:currentDisplay.boundId,mode:'cover'}});
          else if (action === 'allow-cleanup') mock.presentation({scope:'display',action:{type:'faults',faults:{detach:false,destroy:false,attach:false,pacing:false}}});
          else if (action === 'host-lost' || action === 'continuation') mock.presentation({scope:'display',action:{type:action === 'host-lost' ? 'host-lost' : 'continue-reconciliation'}});
          else mock.presentation({scope:'display',action:{type:'native-event',result:action === 'return' ? state.previewMock?.returnEnumeration ?? createSessionDisplay(currentDisplay.boundId).enumeration : action === 'enumeration-failure' ? {status:'api-failure',displays:[]} : {status:'success',displays:currentDisplay.enumeration.displays.filter(display=>display.id!==currentDisplay.boundId)}}});
        }}>{action === 'disappear' ? 'Bound Display disappears' : action === 'return' ? 'Exact Display returns' : action === 'enumeration-failure' ? 'Enumeration fails' : action === 'host-lost' ? 'Owned host lost' : action === 'allow-cleanup' ? 'Allow cleanup retry' : action === 'continuation' ? 'Deliver finite continuation' : 'External background write: Cover'}</button>)}</div>
        <p className="session-dialog-note" data-presentation-diagnostics>{JSON.stringify({boundId:sessionDisplay(state).boundId,identify:sessionDisplay(state).identifyStatus,deployment:sessionDisplay(state).lastResult,desired:sessionDisplay(state).desired,target:sessionDisplay(state).targetRegistered,host:sessionDisplay(state).hostOwned})}</p>
      </details>
      <details className="timeline-dev-lab" open><summary>Committed Language lab · delivered global facts</summary><p className="session-dialog-note">Draft is not applied. Commit writes mock truth then queues a latest-truth reload; no MainWindow language preference or real backend.</p>
        <div className="playback-dev-facts">{(['draft-zh','commit-zh','commit-en','invalid','read-failure','catalog-failure'] as const).map(action=><button type="button" key={action} data-language-dev={action} onClick={()=>{
          if(action==='draft-zh')mock.languageEvent({type:'draft',value:'zh_CN'});
          else {mock.languageEvent({type:'faults',value:{readFails:action==='read-failure',catalogAvailable:action!=='catalog-failure'}});mock.languageEvent({type:'external-write',value:action==='commit-en'?'en':action==='invalid'?'unsupported':'zh_CN'});mock.languageEvent({type:'invalidate'});}
        }}>{action}</button>)}</div><p className="session-dialog-note">{JSON.stringify(mock.language)}</p></details>
      <details className="timeline-dev-lab"><summary>Hide lab · delivered authority facts</summary><div className="playback-dev-facts">
        <button type="button" data-hide-dev="revoke" onClick={()=>mock.hideAction({type:'revoke'})}>Revoke restore qualification</button>
        <button type="button" data-hide-dev="restore" onClick={()=>mock.hideAction({type:'restore'})}>Deliver Show</button>
        <button type="button" data-hide-dev="qualified" onClick={()=>mock.hideAction({type:'facts',recovery:{recoveryQualified:true,qualificationAgeMs:0}})}>Restore qualification</button>
        <button type="button" data-hide-dev="abort-close" onClick={()=>mock.hideAction({type:'close-abort'})}>Abort mock Close Pending</button>
      </div><p className="session-dialog-note">{JSON.stringify({lifecycle:mock.lifecycle,hide:mock.hide})}</p></details>
      <div className="session-dialog-actions"><button type="button" onClick={() => mock.reset(mock.scenario)}>Reset scenario</button><button type="button" data-phase-f-done onClick={() => setDevOpen(false)}>Done</button></div>
    </SessionModal>}
  </div></SessionLanguageProvider>;
}
