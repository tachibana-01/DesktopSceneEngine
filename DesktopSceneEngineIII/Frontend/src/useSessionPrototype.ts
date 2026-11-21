import { useEffect,useRef, useState } from 'react';
import {type HideAction} from './sessionHideModel.ts';
import {createSessionRole,roleTransition} from './sessionLifecycleModel.ts';
import {createSessionLanguage,languageTransition,type LanguageAction} from './sessionLanguageModel.ts';
import { entityTransition, findItem, findPlaylist, itemPlayable, playlistPlayable, type EntityAction } from './sessionEntities.ts';
import { inspectorTransition, type InspectorAction } from './sessionInspectorModel.ts';
import { afterEntityPlayback, playbackTransition, type PlaybackAction } from './sessionPlayback.ts';
import { controllerClear } from './sessionPlaybackSequence.ts';
import { applyTimelineSeek, timelineReleaseIntent, type TimelineDrag } from './sessionTimelineModel.ts';
import { presentationTransition, type PresentationAction } from './sessionPresentationModel.ts';
import { continueDocumentIntent, isDirty, loadOpenCandidate, saveToMockPath } from './sessionDocumentMock.ts';
import { createSessionFixture, readSessionScenario, type SessionScenario } from './sessionFixtures';
import type { PendingDocumentIntent, SessionFeedback, SessionPresentation, SessionProjection } from './sessionTypes';

export type SessionDialogState =
  | { kind: 'decision'; pending: PendingDocumentIntent }
  | { kind: 'open' }
  | { kind: 'save-as'; pending: PendingDocumentIntent | null }
  | { kind: 'rename'; entity: 'playlist' | 'item'; id: string }
  | { kind: 'delete'; entity: 'playlist' | 'item'; id: string }
  | { kind: 'add-media' }
  | { kind: 'configure-background-music'; playlistId: string }
  | { kind: 'background-warning' }
  | { kind: 'message'; feedback: SessionFeedback };

// MainWindow::refreshLibrary rebuilds the selected Inspector after these
// commands, even when its owner ID stays the same. Inline edits do not do this.
function refreshesInspector(state: SessionProjection, action: EntityAction, next: SessionProjection): boolean {
  switch (action.type) {
    case 'select-playlist': case 'select-item': case 'toggle-pause': return false;
    case 'add-playlist': return true;
    case 'move-playlist': case 'move-item': return next !== state;
    case 'add-media': return action.sources.length > 0;
    case 'play-playlist': return playlistPlayable(findPlaylist(state, action.id));
    case 'play-item': {
      const target = findItem(state, action.id);
      return Boolean(target && target.playlist.id === state.selectedPlaylistId && itemPlayable(target.playlist, target.item));
    }
    case 'rename-playlist': case 'rename-item':
      return Boolean(action.type === 'rename-playlist' ? findPlaylist(state, action.id) : findItem(state, action.id)) && next.feedback?.title !== 'Invalid Name';
    case 'delete-playlist': case 'set-playlist-enabled': return Boolean(findPlaylist(state, action.id));
    case 'delete-item': return Boolean(findItem(state, action.id));
    case 'set-item-enabled': return Boolean(findItem(state, action.id)?.playlist.enabled);
  }
}

// UI orchestration only. Player transitions plus approved UI-PIC-001 define the
// mock target; future integration must replace it with authoritative facts.
export function useSessionPrototype() {
  const initialScenario = readSessionScenario(new URLSearchParams(window.location.search).get('sessionScenario'));
  const [scenario, setScenario] = useState<SessionScenario>(initialScenario);
  const [inspectorEpoch, setInspectorEpoch] = useState(0);
  const [timelineEpoch, setTimelineEpoch] = useState(0);
  const [timelineDragEpoch, setTimelineDragEpoch] = useState(0);
  const [state, setState] = useState(() => createSessionFixture(initialScenario));
  const current = useRef(state);
  // UI-PIC-001: transient per-ID restore memory, outside Project/file snapshots.
  const lastNonZeroMasterVolume = useRef(new Map<string, number>());
  // Role presentation owns one lifecycle. Hide and deployment consume it;
  // neither keeps a synchronized copy of closePending/ending.
  const [role,setRole]=useState(()=>state.hideMock?.initial??createSessionRole());
  const currentRole=useRef(role);
  const hide=role.hide, lifecycle=role.lifecycle;
  function hideAction(action:HideAction){const next=roleTransition(currentRole.current,action);currentRole.current=next;setRole(next);return next.hide;}
  const languageFor=(value:SessionProjection)=>{
    let result=createSessionLanguage();
    if(value.languageMock)result=languageTransition(result,{type:'faults',value:{catalogAvailable:value.languageMock.catalogAvailable??true,readFails:value.languageMock.readFails??false}});
    result=languageTransition(result,{type:'external-write',value:value.languageMock?.committed??'en'});
    return languageTransition(languageTransition(result,{type:'invalidate'}),{type:'flush'});
  };
  const [language,setLanguage]=useState(()=>languageFor(state));
  function languageEvent(action:LanguageAction){setLanguage(before=>languageTransition(before,action));}
  useEffect(()=>{
    if(!import.meta.env.DEV)return;
    const receive=(event:Event)=>languageEvent((event as CustomEvent<LanguageAction>).detail);
    window.addEventListener('dse-mock-language',receive);return()=>window.removeEventListener('dse-mock-language',receive);
  },[]);
  const [dialog, setDialog] = useState<SessionDialogState | null>(()=>initialScenario==='hide-modal'?{kind:'delete',entity:'playlist',id:'playlist-a'}:null);
  function commit(next: SessionProjection) {
    for (const id of lastNonZeroMasterVolume.current.keys()) if (!findPlaylist(next, id)) lastNonZeroMasterVolume.current.delete(id);
    current.current = next;
    setState(next);
    if(next.closed&&!currentRole.current.lifecycle.ending)hideAction({type:'teardown'});
    if (next.view?.warning) setDialog({ kind: 'background-warning' });
    else if (next.feedback) setDialog({ kind: 'message', feedback: next.feedbackQueue?.[0]??next.feedback });
  }
  function entity(action: EntityAction) {
    if (current.current.closed) return;
    const before = { ...current.current, feedback: null, feedbackQueue:undefined };
    const next = afterEntityPlayback(before, entityTransition(before, action), action);
    if ((action.type === 'play-playlist' || action.type === 'play-item') && refreshesInspector(before, action, next)) {
      setTimelineDragEpoch(epoch => epoch + 1);
    }
    const activeCount = (value: SessionProjection) => findPlaylist(value, before.activePlaylistId)?.items.length;
    const rebuilt = action.type === 'add-media' && before.selectedPlaylistId === before.activePlaylistId && activeCount(next) !== activeCount(before)
      || action.type === 'delete-item' && findItem(before, action.id)?.playlist.id === before.activePlaylistId && !findItem(next, action.id);
    if (before.runtime && rebuilt && before.faults.playback !== 'rebuild-prepare') setTimelineDragEpoch(epoch => epoch + 1);
    if (refreshesInspector(before, action, next)) setInspectorEpoch(epoch => epoch + 1);
    commit(next);
  }
  function inspector(action: InspectorAction) {
    const before = current.current;
    const next = inspectorTransition(before, action);
    if (action.type === 'playlist-master') {
      const volume = findPlaylist(next, action.id)?.masterVolume;
      // A first direct-to-zero row/Inspector edit must retain the loaded value,
      // even when no earlier UI edit has populated transient restore memory.
      const previous = findPlaylist(before, action.id)?.masterVolume;
      const remembered = volume === 0 ? previous : volume;
      if (!before.closed && remembered !== undefined && remembered > 0) lastNonZeroMasterVolume.current.set(action.id, remembered);
    }
    if (action.type === 'presentation' && next !== before && before.runtime
      && before.activeItemId === action.id && before.runtime.items.some(item => item.id === action.id)
      && before.faults.playback !== 'rebuild-prepare') setTimelineDragEpoch(epoch => epoch + 1);
    if (before.runtime && !next.runtime) controllerClear(next.sequence);
    commit(next);
    return next;
  }
  function setPlaylistMasterPercent(id: string, percent: number) {
    if (!Number.isFinite(percent)) return;
    inspector({ type: 'playlist-master', id, value: Math.max(0, Math.min(100, Math.round(percent))) / 100 });
  }
  function togglePlaylistMaster(id: string) {
    const playlist = findPlaylist(current.current, id);
    if (!playlist || current.current.closed) return;
    if (playlist.masterVolume > 0) lastNonZeroMasterVolume.current.set(id, playlist.masterVolume);
    inspector({ type: 'playlist-master', id, value: playlist.masterVolume > 0 ? 0 : lastNonZeroMasterVolume.current.get(id) ?? 1 });
  }
  function continueIntent(before: SessionProjection, pending: PendingDocumentIntent, decision: 'discard' | 'saved') {
    const next = continueDocumentIntent(before, pending, decision);
    if (next !== before && !next.feedback) lastNonZeroMasterVolume.current.clear();
    return next;
  }
  function playback(action: PlaybackAction) {
    const before = current.current;
    const next = playbackTransition({ ...before, feedback: null }, action);
    // Explicit navigateScene cancels a drag even when Previous finds no target.
    // Natural completion uses Item/duration validation plus the model's explicit
    // cross-Playlist start cancellation fact, including failed preparation.
    if (before.runtime && (action.type === 'previous' || action.type === 'next')) setTimelineDragEpoch(epoch => epoch + 1);
    // Cross-Playlist start/rollback calls refreshLibrary; in-Playlist transport
    // only refreshes lists/status and preserves the selected Inspector draft.
    if (before.runtime && (next.runtime && before.activePlaylistId !== next.activePlaylistId
      || next.feedback?.title === 'Unable to play media' && next.feedback.message === 'Media file could not be opened.')) {
      setInspectorEpoch(epoch => epoch + 1);
    }
    commit(next);
  }
  function timelineRelease(drag: TimelineDrag) {
    const intent = timelineReleaseIntent(current.current, drag);
    if (intent.type === 'next') playback({ type: 'next' });
    else if (intent.type === 'seek') commit(applyTimelineSeek(current.current, intent.seconds));
    return intent.type;
  }
  function injectTimelineDuration() {
    if (!import.meta.env.DEV || !current.current.runtime) return;
    const next = structuredClone(current.current);
    const item = next.runtime!.items.find(item => item.id === next.activeItemId);
    if (item) { item.duration += 1; commit(next); }
  }
  function present(id: string, patch: { layout?: Partial<SessionPresentation['layout']>; mediaPresentation?: Partial<SessionPresentation['mediaPresentation']> }) {
    const item = findItem(current.current, id)?.item;
    if (!item) return current.current;
    return inspector({ type: 'presentation', id, value: { ...item.presentation,
      layout: { ...item.presentation.layout, ...patch.layout },
      mediaPresentation: { ...item.presentation.mediaPresentation, ...patch.mediaPresentation } } });
  }
  function presentation(action: PresentationAction) { commit(presentationTransition(current.current, action, currentRole.current.lifecycle)); }
  useEffect(() => {
    if (!import.meta.env.DEV) return;
    // Delivered DEV intents exercise the same fresh gate as the visible control;
    // they do not expose a production bridge or mutate authority from the DOM.
    const deliverPresentation = (event: Event) => presentation((event as CustomEvent<PresentationAction>).detail);
    const deliverRole = (event: Event) => hideAction((event as CustomEvent<HideAction>).detail);
    window.addEventListener('dse-mock-presentation', deliverPresentation);
    window.addEventListener('dse-mock-session-role', deliverRole);
    return () => {
      window.removeEventListener('dse-mock-presentation', deliverPresentation);
      window.removeEventListener('dse-mock-session-role', deliverRole);
    };
  }, []);
  function configureBackgroundMusic(playlistId: string) {
    if (current.current.closed || !findPlaylist(current.current, playlistId)) return;
    // MainWindow::openBackgroundMusicWindow validates the captured ID. Alias
    // remains presentation only; no BGM implementation is opened by this mock.
    setDialog({ kind: 'configure-background-music', playlistId });
  }
  function acknowledgeBackground() {
    if (dialog?.kind !== 'background-warning') return;
    commit(presentationTransition(current.current, { scope: 'view', action: { type: 'dismiss-warning' } }, currentRole.current.lifecycle));
    setDialog(null);
  }
  function select(entityKind: 'playlist' | 'item', id: string) {
    if (entityKind === 'playlist' && current.current.selectedPlaylistId === id) return;
    entity({ type: entityKind === 'playlist' ? 'select-playlist' : 'select-item', id });
  }
  function reset(next: SessionScenario) {
    lastNonZeroMasterVolume.current.clear();
    const fixture=createSessionFixture(next);
    currentRole.current=fixture.hideMock?.initial??createSessionRole();setRole(currentRole.current);setLanguage(languageFor(fixture));
    setDialog(next==='hide-modal'?{kind:'delete',entity:'playlist',id:'playlist-a'}:null); setScenario(next); setInspectorEpoch(epoch => epoch + 1); setTimelineEpoch(epoch => epoch + 1); setTimelineDragEpoch(epoch => epoch + 1); commit(fixture);
  }
  function request(pending: PendingDocumentIntent) {
    if (current.current.closed) return;
    if(pending.kind==='close'){hideAction({type:'close-begin'});hideAction({type:'close-interaction'});}
    if (isDirty(current.current)) setDialog({ kind: 'decision', pending });
    else commit(continueIntent({ ...current.current, feedback: null }, pending, 'discard'));
  }
  function open(path: string) {
    const result = loadOpenCandidate(current.current, path);
    setDialog(null);
    if (result.error) commit({ ...current.current, feedback: result.error });
    else if (result.candidate) request({ kind: 'open', candidate: result.candidate });
  }
  function save(pending: PendingDocumentIntent | null = null) {
    if (current.current.document.path === null) { setDialog({ kind: 'save-as', pending }); return; }
    const result = saveToMockPath({ ...current.current, feedback: null }, current.current.document.path, false);
    setDialog(null);
    commit(result.result === 'saved' && pending ? continueIntent(result.state, pending, 'saved') : result.state);
    if(pending?.kind==='close'&&result.result!=='saved')hideAction({type:'close-abort'});
  }
  function saveAs(path: string, pending: PendingDocumentIntent | null) {
    const result = saveToMockPath({ ...current.current, feedback: null }, path, true);
    setDialog(null);
    commit(result.result === 'saved' && pending ? continueIntent(result.state, pending, 'saved') : result.state);
    if(pending?.kind==='close'&&result.result!=='saved')hideAction({type:'close-abort'});
  }
  function decide(choice: 'save' | 'discard' | 'cancel', pending: PendingDocumentIntent) {
    setDialog(null);
    if (choice === 'save') save(pending);
    else if (choice === 'discard') commit(continueIntent({ ...current.current, feedback: null }, pending, 'discard'));
    else if(pending.kind==='close')hideAction({type:'close-abort'});
  }
  function dismiss() {
    if (dialog?.kind === 'background-warning') return;
    if(dialog?.kind==='message'&&(current.current.feedbackQueue?.length??0)>1){
      const queue=current.current.feedbackQueue!.slice(1);current.current={...current.current,feedbackQueue:queue};setState(current.current);setDialog({kind:'message',feedback:queue[0]});return;
    }
    if(currentRole.current.lifecycle.closePending)hideAction({type:'close-abort'});
    setDialog(null);
    if (current.current.feedback) commit({ ...current.current, feedback: null,feedbackQueue:undefined });
  }
  return { state, hide,lifecycle,hideAction,language,languageEvent, scenario, inspectorEpoch, timelineEpoch, timelineDragEpoch, timelineRelease, injectTimelineDuration, dialog, setDialog, entity, inspector, setPlaylistMasterPercent, togglePlaylistMaster, present, presentation, playback, select, reset, request, open, save, saveAs, decide, dismiss, configureBackgroundMusic, acknowledgeBackground };
}
