import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { ProductionSessionController, ProductionSessionFacade, SessionReadFailure, type SessionReadState, type SessionTransport } from './ProductionSessionFacade.ts';
import { snapshotDisplayFacts } from './ProductionSessionProjection.ts';
import { ProductSessionBar } from './ShellTitleBar';
import { ProductTitleBar } from './ProductTitleBar';
import { SessionLibrary } from './SessionLibrary';
import { SessionInspector } from './SessionInspector';
import { SessionPreviewView } from './SessionPreviewView';
import { SessionPlaybackRailView } from './SessionPlaybackRailView';
import {ProductionRuntimeFacade,type RuntimePlaybackSample,type RuntimeIntent,type RuntimeResult} from './ProductionRuntimeFacade.ts';
import {runtimeSessionProjection} from './ProductionRuntimeProjection.ts';
import {ProductionRuntimeTimeline} from './ProductionRuntimeTimeline';
import { SessionSettings } from './SessionSettings';
import { useSessionLayout } from './SessionSplitters';
import { SessionLanguageProvider, translateSession, displayText } from './sessionLanguage';
import type { MainWindowSnapshot, DocumentAction, DocumentChoice } from './ProductionSessionTypes.ts';
import { ProductionPreviewFacade, observeNativePreviewRect, type NativePreviewState } from './ProductionPreviewGeometry.ts';
import { isModelCSessionPage } from './sessionComposition.ts';
import { ProductionDocumentFacade } from './ProductionDocumentFacade.ts';
import { ProductionViewFacade, type ViewIntent, type ViewStatus } from './ProductionViewFacade.ts';
import {NativePreviewOverlayScope} from './NativePreviewOverlayScope';
import {ProductionAuthoredFacade} from './ProductionAuthoredFacade.ts';
import type {AuthoredIntent,AuthoredResult} from './ProductionAuthoredTypes.ts';
import {authoredOutcomeMessages} from './ProductionAuthoredFeedback.ts';
import {ProductionEntityCommands,type ProductionEntityTarget} from './ProductionEntityCommands';
import {ProductionBgmFacade} from './ProductionBgmFacade.ts';
import {findItem,findPlaylist} from './sessionReadProjection.ts';
import type {SequenceMode} from './sessionPlaybackSequence.ts';
import type {EntityMenuTarget} from './SessionLibrary';
import { SessionDocumentDecision } from './SessionDocumentDecision';
import { SessionModal } from './SessionModal';
import { documentShortcut } from './sessionShortcuts.ts';
import {LatestUnsentAudioScalar,UnsentAudioMuteParity,type ScalarDispatch} from './LatestUnsentAudioScalar.ts';
import {audioEffectPending} from './ProductionAudioEffects.ts';
import {useProductPresentation,useSessionHide} from './productWindowHost.ts';
import './session.css';
import './sessionPhaseA.css';
import './sessionRemaining.css';
import './productionSession.css';

const unavailable=()=>{};
type DocumentIntent={action:DocumentAction}|{token:string;choice:DocumentChoice};
type SeekTarget=Extract<RuntimeIntent,{type:'Seek'}>['payload'];
type ViewPreference=Extract<ViewIntent,{type:'SetCanvasBoundaryHighlight'|'SetDesktopBackground'}>;
type ScalarIntent=Extract<AuthoredIntent,{type:'SetPlaylistMaster'|'SetItemGain'}>;
type ScalarDriver={identity:string;boundary:string;intent:ScalarIntent;driver:LatestUnsentAudioScalar};
const needsScalarSettlement=(type:AuthoredIntent['type'])=>
  type==='AddMedia'||type==='RemoveItem'||type==='RemovePlaylist'||type==='SetItemEnabled'
  ||type==='SetPlaylistEnabled'||type==='ApplyVideoRate'||type==='SetItemPresentation';
const masterWriteKey=(document:string,playlistId:string)=>document+':'+playlistId;
function sameViewPreference(first:ViewPreference,second:ViewPreference){
  return first.type==='SetCanvasBoundaryHighlight'
    ?second.type===first.type&&first.payload.highlighted===second.payload.highlighted
    :second.type===first.type&&first.payload.mode===second.payload.mode;
}
export function ProductionSessionShell(){
  const [state,setState]=useState<SessionReadState>({status:'loading'});
  // Observe native Hide eligibility before the ready handshake asks the Host to publish it.
  const onHide=useSessionHide(state.status==='ready'?state.snapshot.session.incarnation:null);
  useProductPresentation(state.status === 'failure' ? 'failed' : state.status);
  const [runtimeSample,setRuntimeSample]=useState<RuntimePlaybackSample|null>(null),[runtimeFailure,setRuntimeFailure]=useState<string|null>(null);
  const native=useRef<{controller:ProductionSessionController;documents:ProductionDocumentFacade;authored:ProductionAuthoredFacade;runtime:ProductionRuntimeFacade;view:ProductionViewFacade}|null>(null);
  useEffect(()=>{
    let controller:ProductionSessionController|undefined,unsubscribe:(()=>void)|undefined,unsubscribeRuntime:(()=>void)|undefined,active=true;
    try{
      if(!isModelCSessionPage(window.location.search))throw new SessionReadFailure('InvalidPageContext','The production Session page context is invalid.');
      const query=new URLSearchParams(window.location.search);
      const bridge=(window as Window&{chrome?:{webview?:SessionTransport}}).chrome?.webview;
      const generation=query.get('generation')??'',incarnation=query.get('session')??'';
      if(!bridge)throw new SessionReadFailure('BridgeUnavailable','The production Session native bridge is unavailable.');
      controller=new ProductionSessionController(new ProductionSessionFacade(bridge,generation,incarnation));
      const runtime=new ProductionRuntimeFacade(bridge,generation,incarnation);
      native.current={controller,documents:new ProductionDocumentFacade(bridge,generation,incarnation),
        authored:new ProductionAuthoredFacade(bridge,generation,incarnation),runtime,view:new ProductionViewFacade(bridge,generation,incarnation)};
      unsubscribe=controller.subscribe(()=>{if(active)setState(controller!.getSnapshot());});
      let previous:RuntimePlaybackSample|null=null;
      unsubscribeRuntime=runtime.subscribe(sample=>{
        if(!active)return;
        setRuntimeSample(sample);setRuntimeFailure(null);
        const editor=controller!.getSnapshot();
        const discontinuity=previous?previous.bindingToken!==sample.bindingToken||previous.active.playlistId!==sample.active.playlistId||previous.active.itemId!==sample.active.itemId
          :editor.status==='ready'&&(editor.snapshot.active.playlistId!==sample.active.playlistId||editor.snapshot.active.itemId!==sample.active.itemId);
        previous=sample;
        // Runtime cadence never reads the editor. A binding discontinuity can
        // reflect native selection consequences and needs one coherent refresh.
        if(discontinuity)void controller!.refresh();
      });
      void controller.refresh().then(()=>{
        if(active&&controller!.getSnapshot().status==='ready')void runtime.start().catch(error=>{if(active)setRuntimeFailure(error instanceof Error?error.message:'Native runtime unavailable.');});
      });
    }catch(error){setState({status:'failure',code:error instanceof SessionReadFailure?error.code:'TransportFailure',message:error instanceof Error?error.message:'Session startup failed.'});}
    return()=>{active=false;unsubscribe?.();unsubscribeRuntime?.();native.current?.documents.dispose();native.current?.authored.dispose();native.current?.runtime.dispose();native.current?.view.dispose();native.current=null;controller?.dispose();};
  },[]);
  const authoredIntent=useCallback(async(intent:AuthoredIntent,document?:string)=>{
    const owner=native.current;if(!owner)throw new Error('Native authored commands are unavailable.');
    try{
      const result=await owner.authored.send(intent,document);
      if(native.current===owner&&(intent.type!=='SetPlaylistMaster'||!await owner.controller.projectPlaylistMaster(result)))await owner.controller.refresh();
      return result;
    }catch(error){if(native.current===owner)await owner.controller.refresh();throw error;}
  },[]);
  const documentIntent=useCallback(async(intent:DocumentIntent,document?:string)=>{
    const owner=native.current;if(!owner)throw new Error('Native document commands are unavailable.');
    try{return await ('action'in intent?owner.documents.begin(intent.action,document):owner.documents.continue(intent.token,intent.choice,document));}
    finally{if(native.current===owner)await owner.controller.refresh();}
  },[]);
  const runtimeIntent=useCallback(async(intent:RuntimeIntent,document?:string)=>{
    const owner=native.current;if(!owner)throw new Error('Native runtime commands are unavailable.');
    return owner.runtime.send(intent,document);
  },[]);
  const viewIntent=useCallback(async(intent:ViewIntent)=>{
    const owner=native.current;
    if(!owner)throw new Error('Native View controls are unavailable.');
    try{return await owner.view.send(intent);}
    finally{if(native.current===owner)await owner.controller.refresh();}
  },[]);
  if(state.status!=='ready'){
    const language = typeof window !== 'undefined' && new URLSearchParams(window.location.search).get('language') === 'zh_CN' ? 'zh_CN' : 'en';
    const text = (message:string)=>translateSession(language,message);
    return <div className="production-session session-entry-shell" data-bootstrap-state={state.status} data-failure-code={state.status==='failure'?state.code:undefined}>
      <ProductTitleBar text={text} beforeDrag={<span className="session-entry-title">DesktopSceneEngine</span>} />
      <main className="session-entry-status">
        <h1>{state.status==='loading'?'Connecting to the native Session…':'Production Session unavailable'}</h1>
        {state.status==='failure'&&<p role="alert">{state.code}: {state.message}</p>}
      </main>
    </div>;
  }
  return <ProductionSessionView snapshot={state.snapshot} runtimeSample={runtimeSample} runtimeFacade={native.current!.runtime} runtimeFailure={runtimeFailure}
    onRuntimeIntent={runtimeIntent} onDocumentIntent={documentIntent} onAuthoredIntent={authoredIntent} onViewIntent={viewIntent} onHide={onHide}/>;
}

function ProductionSessionView({snapshot,runtimeSample,runtimeFacade,runtimeFailure,onRuntimeIntent,onDocumentIntent,onAuthoredIntent,onViewIntent,onHide}:{snapshot:MainWindowSnapshot;
  runtimeSample:RuntimePlaybackSample|null;runtimeFacade:ProductionRuntimeFacade;runtimeFailure:string|null;onRuntimeIntent:(intent:RuntimeIntent,document?:string)=>Promise<RuntimeResult>;
  onDocumentIntent:(intent:DocumentIntent,document?:string)=>Promise<{status:string}>;onAuthoredIntent:(intent:AuthoredIntent,document?:string)=>Promise<AuthoredResult>;
  onViewIntent:(intent:ViewIntent)=>Promise<ViewStatus>;onHide?:()=>void}){
  const state=runtimeSessionProjection(snapshot,runtimeSample),facts=snapshotDisplayFacts(snapshot),language=snapshot.language.effective;
  const t=(text:string)=>translateSession(language,text);
  const layout=useSessionLayout(snapshot.session.incarnation,undefined,language);
  const operation=snapshot.documentOperation,pending=operation.pending;
  const [menu,setMenu]=useState<{x:number;y:number}|null>(null);
  const [deliveryBusy,setDeliveryBusy]=useState(false),deliveryLock=useRef(false),live=useRef(true);
  const structuralLock=useRef(false);
  const [entityMenu,setEntityMenu]=useState<ProductionEntityTarget|null>(null);
  const selectionIntent=useRef<{document:string;playlistId:string;itemId:string|null}|null>(null);
  const pendingModes=useRef(new Map<string|null,{document:string;mode:SequenceMode}>());
  const [authoredFeedback,setAuthoredFeedback]=useState<{title:string;messages:string[]}|null>(null);
  const [dismissedImport,setDismissedImport]=useState<string|null>(null);
  const masterRestore=useRef(new Map<string,number>());
  const [masterWrites,setMasterWrites]=useState<ReadonlyMap<string,number>>(new Map());
  const scalarDrivers=useRef(new Map<string,ScalarDriver>());
  const scalarSender=useRef<(intent:ScalarIntent,document:string)=>Promise<ScalarDispatch>>(async()=> 'invalidated');
  const muteSender=useRef<(admitted:(execution:string,sequence:string)=>void)=>Promise<ScalarDispatch>>(async()=> 'invalidated');
  const muteDriver=useRef<UnsentAudioMuteParity|null>(null);
  if(!muteDriver.current)muteDriver.current=new UnsentAudioMuteParity((_,admitted)=>muteSender.current(admitted));
  const [deliveryError,setDeliveryError]=useState<string|null>(null);
  const [dismissedOutcome,setDismissedOutcome]=useState<string|null>(null);
  const [runtimeFeedback,setRuntimeFeedback]=useState('');
  const noticeAcknowledgement=useRef<string|null>(null);
  const [noticeAcknowledging,setNoticeAcknowledging]=useState<string|null>(null);
  const [noticeFailure,setNoticeFailure]=useState<string|null>(null);
  const preferenceRequests=useRef(new Map<ViewPreference['type'],{current:ViewPreference;next?:ViewPreference}>());
  const deploymentLock=useRef(false);
  const identifyLock=useRef(false);
  const [viewFeedback,setViewFeedback]=useState<string>();
  const [deploymentRequest,setDeploymentRequest]=useState<'deploy'|'undeploy'>();
  const runtimePending=useRef(new Set<string>()),[runtimePendingTypes,setRuntimePendingTypes]=useState<ReadonlySet<string>>(new Set());
  const mediaPending=snapshot.mediaImport.pending;
  const structuralPending=snapshot.audioEffects.effects.some(e=>e.scope==='structural'&&audioEffectPending(e));
  const documentAllowed=!pending&&!mediaPending&&!deliveryBusy&&!snapshot.host.closing&&!snapshot.host.ending;
  const authoredAllowed=documentAllowed&&!structuralPending;
  const runtimeAllowed=!pending&&!mediaPending&&!snapshot.host.closing&&!snapshot.host.ending;
  const viewAllowed=!snapshot.host.closing&&!snapshot.host.ending;
  const viewStillAllowed=useRef(viewAllowed);
  viewStillAllowed.current=viewAllowed;
  const deploymentPending=deploymentRequest??(snapshot.desktop.busy?(snapshot.desktop.desired?'deploy':'undeploy'):undefined);
  const documentIncarnation=snapshot.audioEffects.documentIncarnation;
  const model={state,readOnly:!authoredAllowed,runtimeRateAvailable:Boolean(runtimeSample?.hasMedia&&runtimeSample.mediaKind==='video'),
    inspectorEpoch:snapshot.session.incarnation+':'+documentIncarnation+':'+(snapshot.document.path??''),language:{effective:language,retranslation:language},authoredIntent:requestAuthored};
  useEffect(()=>()=>{live.current=false;for(const d of scalarDrivers.current.values())d.driver.dispose();muteDriver.current?.dispose();},[]);
  const interactionBoundary=snapshot.session.incarnation+':'+snapshot.audioEffects.documentIncarnation+':'+(runtimeSample?.bindingToken??'');
  useEffect(()=>{
    for(const d of scalarDrivers.current.values())d.driver.setIdentity(null);
    scalarDrivers.current.clear();muteDriver.current?.setIdentity(null);
  },[interactionBoundary,pending?.token,mediaPending?.token,structuralPending,snapshot.host.closing,snapshot.host.ending]);
  useEffect(()=>{
    if(pending||mediaPending||structuralPending||snapshot.host.closing||snapshot.host.ending)return;
    const effects=snapshot.audioEffects.effects;
    const frontier=JSON.stringify(snapshot.audioEffects);
    const muteEffect=effects.find(e=>e.scope==='output-mute');
    if(muteEffect)muteDriver.current?.observeNative(interactionBoundary,muteEffect.execution,muteEffect.sequence,muteEffect.phase);
    if(!effects.some(e=>e.scope==='embedded-gain'&&audioEffectPending(e)))for(const d of scalarDrivers.current.values())d.driver.capacityAvailable(d.identity,frontier);
    if(!effects.some(e=>e.scope==='output-mute'&&audioEffectPending(e)))muteDriver.current?.capacityAvailable(interactionBoundary,frontier);
  },[snapshot.audioEffects,pending?.token,mediaPending?.token,structuralPending,snapshot.host.closing,snapshot.host.ending,interactionBoundary]);
  useEffect(()=>{document.title='DesktopSceneEngine — '+(snapshot.document.path?snapshot.document.displayName:t('Untitled'));},[snapshot.document.path,snapshot.document.displayName,language]);
  useEffect(()=>{if(!documentAllowed)setMenu(null);},[documentAllowed]);
  useEffect(()=>{if(pending||mediaPending||snapshot.host.closing||snapshot.host.ending)setEntityMenu(null);},[pending?.token,mediaPending?.token,snapshot.host.closing,snapshot.host.ending]);
  useEffect(()=>{if(operation?.last?.outcome==='completed'&&['new','open'].includes(operation.last.action))masterRestore.current.clear();},[operation?.last?.token]);
  useEffect(()=>{
    const ids=new Set(snapshot.project.playlists.map(playlist=>playlist.id));
    for(const id of masterRestore.current.keys())if(!ids.has(id))masterRestore.current.delete(id);
    for(const playlist of snapshot.project.playlists)if(playlist.masterVolume>0)masterRestore.current.set(playlist.id,playlist.masterVolume);
  },[snapshot.project]);
  useEffect(()=>{if(operation?.pending||operation?.last)setDeliveryError(null);},[operation?.pending?.token,operation?.last?.token]);
  async function settleScalarEdits(){
    // New scalar UI submissions are held at ingress while an exclusive action
    // waits. Existing drivers are free to send their latest unsent value.
    const current=Array.from(scalarDrivers.current.values()).filter(entry=>entry.boundary===interactionBoundary);
    const results=await Promise.all(current.map(entry=>entry.driver.settle()));
    return results.every(Boolean);
  }
  const busyMessage=t('Another operation is already in progress.');
  const volumePendingMessage=t('The volume change could not be confirmed. Try again.');
  async function requestDocument(intent:DocumentIntent){
    if(deliveryLock.current||snapshot.host.ending)return;
    if('action'in intent&&!documentAllowed)return;
    if('token'in intent&&(pending?.stage!=='decision'||pending.token!==intent.token))return;
    if('action'in intent&&structuralLock.current){setDeliveryError(busyMessage);return;}
    deliveryLock.current=true;setDeliveryBusy(true);setMenu(null);setDeliveryError(null);
    try{
      if('action'in intent&&!await settleScalarEdits()){
        if(live.current)setDeliveryError(volumePendingMessage);
        return;
      }
      if(!live.current)return;
      const ack=await onDocumentIntent(intent,snapshot.audioEffects.documentIncarnation);
      if(live.current&&ack.status==='unavailable')setDeliveryError(t('Native document commands are unavailable.'));
      if(live.current&&ack.status==='busy')setDeliveryError(busyMessage);
      // Accepted is delivery/ownership only. Completion, path and dirty always
      // come from the next native snapshot, including after page recreation.
    }catch(error){if(live.current)setDeliveryError(error instanceof Error?error.message:'Native document acknowledgement is unavailable; the operation outcome is unknown.');}
    finally{deliveryLock.current=false;if(live.current)setDeliveryBusy(false);}
  }
  async function requestAuthored(intent:AuthoredIntent):Promise<void>{
    if(intent.type==='SetPlaylistMaster'||intent.type==='SetItemGain'){await requestScalar(intent);return;}
    if(deliveryLock.current||!authoredAllowed)return;
    if(!needsScalarSettlement(intent.type)){await deliverAuthored(intent);return;}
    if(structuralLock.current){setAuthoredFeedback({title:t('Authored command'),messages:[busyMessage]});return;}
    structuralLock.current=true;
    try{
      if(!await settleScalarEdits()){
        if(live.current)setAuthoredFeedback({title:t('Authored command'),messages:[volumePendingMessage]});
        return;
      }
      if(live.current)await deliverAuthored(intent);
    }finally{structuralLock.current=false;}
  }
  async function deliverAuthored(intent:AuthoredIntent):Promise<boolean>{
    try{
      const result=await onAuthoredIntent(intent,snapshot.audioEffects.documentIncarnation);
      if(!live.current)return false;
      const messages=authoredOutcomeMessages(result,t);if(messages.length)setAuthoredFeedback({title:t('Authored command'),messages});
      return ['committed','unchanged'].includes(result.authored);
    }catch(error){if(live.current)setAuthoredFeedback({title:t('Authored command'),messages:[error instanceof Error?error.message:t('Native authored result is unavailable; the outcome is unknown.')]});}
    return false;
  }
  async function requestTimelineSeek(payload:SeekTarget,document:string):Promise<RuntimeResult|null>{
    if(!runtimeAllowed||document!==documentIncarnation)return null;
    const result=await onRuntimeIntent({type:'Seek',payload},document);
    // A failed media preparation releases the native Seek slot without ever
    // reserving an audio effect, so it need not publish a capacity revision.
    if(live.current&&!result.audioEffect&&['bind-failed','failed','stale','cancelled'].includes(result.status)){
      for(const entry of scalarDrivers.current.values())if(entry.boundary===interactionBoundary)entry.driver.capacityAvailable(entry.identity);
    }
    return result;
  }
  async function requestRuntime(intent:Exclude<RuntimeIntent,{type:'Seek'}>):Promise<void>{
    if(intent.type==='ToggleOutputMute'){
      if(!runtimeAllowed)return;muteDriver.current!.setIdentity(interactionBoundary);muteDriver.current!.click();return;
    }
    if(!runtimeAllowed||runtimePending.current.has(intent.type))return;
    runtimePending.current.add(intent.type);setRuntimePendingTypes(new Set(runtimePending.current));setRuntimeFeedback('');
    try{
      const result=await onRuntimeIntent(intent,snapshot.audioEffects.documentIncarnation);
      if(live.current&&result.message)setRuntimeFeedback(result.message);
    }catch(error){if(live.current)setRuntimeFeedback(error instanceof Error?error.message:'Native runtime result is unavailable.');}
    finally{runtimePending.current.delete(intent.type);if(live.current)setRuntimePendingTypes(new Set(runtimePending.current));}
  }
  function play(entity:'playlist'|'item',id:string){
    if(entity==='playlist')void requestRuntime({type:'PlayPlaylist',payload:{playlistId:id}});
    else{const target=findItem(state,id);if(target)void requestRuntime({type:'PlayItem',payload:{playlistId:target.playlist.id,itemId:id}});}
  }
  function select(entity:'playlist'|'item',id:string){
    if(!authoredAllowed||deliveryLock.current)return;
    const pending=selectionIntent.current?.document===documentIncarnation?selectionIntent.current:null;
    const playlistId=pending?pending.playlistId:state.selectedPlaylistId;
    const itemId=pending?pending.itemId:state.selectedItemId;
    function request(intent:Extract<AuthoredIntent,{type:'SelectPlaylist'|'SelectItem'}>){
      const next={document:documentIncarnation,playlistId:intent.payload.playlistId,
        itemId:intent.type==='SelectItem'?intent.payload.itemId:null};
      selectionIntent.current=next;
      void requestAuthored(intent).finally(()=>{if(selectionIntent.current===next)selectionIntent.current=null;});
    }
    if(entity==='playlist'){
      // Qt currentItemChanged never clears Item selection on a same-row click.
      if(id!==playlistId)request({type:'SelectPlaylist',payload:{playlistId:id}});
    }else{
      const target=findItem(state,id);if(target&&id!==itemId)request({type:'SelectItem',payload:{playlistId:target.playlist.id,itemId:id}});
    }
  }
  function openEntityMenu(target:EntityMenuTarget){
    const found=target.entity==='item'?findItem(state,target.id):undefined;
    const playlist=target.entity==='playlist'?findPlaylist(state,target.id):found?.playlist;
    const entity=found?.item??playlist;if(!entity||!playlist)return;
    setEntityMenu({entity:target.entity,playlistId:playlist.id,itemId:found?.item.id,alias:entity.alias,x:target.x,y:target.y});
  }
  function pendingMode(playlistId:string|null){
    const pending=pendingModes.current.get(playlistId);
    return pending?.document===documentIncarnation?pending.mode:undefined;
  }
  function requestMode(playlistId:string|null,mode:SequenceMode){
    if(!authoredAllowed||deliveryLock.current)return;
    const next={document:documentIncarnation,mode};
    pendingModes.current.set(playlistId,next);
    const intent:AuthoredIntent=playlistId?{type:'SetPlaylistPolicy',payload:{playlistId,mode}}:{type:'SetProjectPolicy',payload:{mode}};
    void requestAuthored(intent).finally(()=>{if(pendingModes.current.get(playlistId)===next)pendingModes.current.delete(playlistId);});
  }
  async function configureBgm(playlistId:string){
    if(!authoredAllowed)return;
    let peer:ProductionBgmFacade|undefined;
    try{
      const query=new URLSearchParams(window.location.search),bridge=(window as Window&{chrome?:{webview?:SessionTransport}}).chrome?.webview;
      peer=new ProductionBgmFacade(bridge,query.get('generation')??'',query.get('session')??'');
      const result=await peer.open(playlistId);
      if(live.current&&result.status!=='opened')setAuthoredFeedback({title:t('Configure Background Music…'),messages:[result.message||t('Native BGM editor unavailable')]});
    }catch(error){if(live.current)setAuthoredFeedback({title:t('Configure Background Music…'),messages:[error instanceof Error?error.message:t('Native BGM editor unavailable')]});}
    finally{peer?.dispose();}
  }
  async function requestMasterValue(id:string,value:number):Promise<boolean>{
    return requestScalar({type:'SetPlaylistMaster',payload:{playlistId:id,value}});
  }
  function trackMasterWrite(key:string,delta:number){
    setMasterWrites(previous=>{
      const writes=new Map(previous),count=(writes.get(key)??0)+delta;
      if(count>0)writes.set(key,count);else writes.delete(key);
      return writes;
    });
  }
  scalarSender.current=async (intent,document)=>{
    if(document!==documentIncarnation)return 'invalidated';
    const master=intent.type==='SetPlaylistMaster'?masterWriteKey(document,intent.payload.playlistId):null;
    if(master)trackMasterWrite(master,1);
    try{
      const result=await onAuthoredIntent(intent,document);if(!live.current)return 'invalidated';
      if(result.status==='busy')return 'busy';
      const messages=authoredOutcomeMessages(result,t);if(messages.length)setAuthoredFeedback({title:t('Authored command'),messages});
      return ['committed','unchanged'].includes(result.authored)?'terminal':'invalidated';
    }catch(error){if(live.current)setAuthoredFeedback({title:t('Authored command'),messages:[error instanceof Error?error.message:t('Native authored result is unavailable; the outcome is unknown.')]});return 'unknown';}
    finally{if(master&&live.current)trackMasterWrite(master,-1);}
  };
  function requestScalar(intent:ScalarIntent):Promise<boolean>{
    if(!authoredAllowed||structuralLock.current||deliveryLock.current)return Promise.resolve(false);
    const kind=intent.type,identity=JSON.stringify([interactionBoundary,kind,intent.payload.playlistId,'itemId'in intent.payload?intent.payload.itemId:null]);
    let entry=scalarDrivers.current.get(identity);
    if(!entry){entry={identity,boundary:interactionBoundary,intent,driver:new LatestUnsentAudioScalar(async(key,value)=>{
      const current=scalarDrivers.current.get(identity);if(!current||current.identity!==key)return 'invalidated';
      const captured=current.intent;
      return scalarSender.current(captured.type==='SetPlaylistMaster'
        ?{type:'SetPlaylistMaster',payload:{...captured.payload,value}}
        :{type:'SetItemGain',payload:{...captured.payload,value}},documentIncarnation);
    })};scalarDrivers.current.set(identity,entry);}
    entry.intent=intent;
    entry.driver.setIdentity(identity);
    return entry.driver.submit(intent.payload.value);
  }
  muteSender.current=async admitted=>{
    try{const result=await runtimeFacade.send({type:'ToggleOutputMute',payload:{}},snapshot.audioEffects.documentIncarnation,receipt=>admitted(receipt.execution,receipt.sequence));if(!live.current)return 'invalidated';if(result.message)setRuntimeFeedback(result.message);return result.status==='busy'?'busy':['stale','unavailable','rejected'].includes(result.status)?'invalidated':'terminal';}
    catch(error){if(live.current)setRuntimeFeedback(error instanceof Error?error.message:'Native mute outcome is unknown.');return 'unknown';}
  };
  function setMaster(id:string,percent:number){return Number.isFinite(percent)?requestMasterValue(id,Math.max(0,Math.min(100,Math.round(percent)))/100):Promise.resolve(false);}
  function toggleMaster(id:string){
    if(masterWrites.has(masterWriteKey(documentIncarnation,id)))return;
    const playlist=findPlaylist(state,id);if(!playlist)return;
    void requestMasterValue(id,playlist.masterVolume>0?0:masterRestore.current.get(id)??1);
  }
  async function viewFailure(intent:ViewIntent){
    const failure=intent.type==='DeployDesktop'?'The desktop could not be deployed.'
      :intent.type==='UndeployDesktop'?'The desktop could not be undeployed.'
      :intent.type==='SetCanvasBoundaryHighlight'?'The canvas highlight could not be changed.':'The desktop background could not be changed.';
    try{
      const status=await onViewIntent(intent);
      if(['rejected','failed','unavailable'].includes(status))return failure;
    }catch{
      return failure;
    }
  }
  async function requestViewPreference(intent:ViewPreference,sameConfirmedValue:boolean){
    if(!viewAllowed)return;
    const active=preferenceRequests.current.get(intent.type);
    if(active){
      // Accept an explicit reversal while the older native value is still
      // displayed. At most the latest unsent choice is retained.
      active.next=sameViewPreference(active.current,intent)?undefined:intent;
      return;
    }
    if(sameConfirmedValue)return;
    const request:{current:ViewPreference;next?:ViewPreference}={current:intent};
    preferenceRequests.current.set(intent.type,request);
    setViewFeedback(undefined);
    try{
      for(;;){
        const failure=await viewFailure(request.current);
        if(!live.current||!viewStillAllowed.current)return;
        const next=request.next;
        if(!next){
          if(failure)setViewFeedback(failure);
          return;
        }
        request.current=next;
        request.next=undefined;
      }
    }finally{preferenceRequests.current.delete(intent.type);}
  }
  const highlight=(highlighted:boolean)=>void requestViewPreference({type:'SetCanvasBoundaryHighlight',payload:{highlighted}},highlighted===snapshot.preferences.highlight);
  const desktopBackground=(mode:'cover'|'reveal')=>void requestViewPreference({type:'SetDesktopBackground',payload:{mode}},mode===snapshot.preferences.background);
  async function identifyDisplay(){
    if(!viewAllowed||!facts.canIdentify||identifyLock.current)return;
    identifyLock.current=true;
    try{await onViewIntent({type:'IdentifyBoundDisplay',payload:{}});}
    catch{/* The native marker is the only success indication. */}
    finally{identifyLock.current=false;}
  }
  async function toggleDeployment(){
    if(!viewAllowed||deploymentLock.current||!facts.deploymentEnabled)return;
    const retained=facts.retained;
    deploymentLock.current=true;
    setDeploymentRequest(retained?'undeploy':'deploy');
    setViewFeedback(undefined);
    try{
      const failure=await viewFailure({type:retained?'UndeployDesktop':'DeployDesktop',payload:{}});
      if(failure&&live.current)setViewFeedback(failure);
    }finally{
      deploymentLock.current=false;
      if(live.current)setDeploymentRequest(undefined);
    }
  }
  const importResult=snapshot.mediaImport.last;
  const importMessages=importResult&&importResult.token!==dismissedImport?authoredOutcomeMessages(importResult,t):[];
  useEffect(()=>{
    const shortcut=(event:KeyboardEvent)=>{
      const command=documentShortcut(event);if(!command)return;event.preventDefault();
      if(!documentAllowed||document.querySelector('dialog[open]'))return;
      void requestDocument({action:command});
    };
    window.addEventListener('keydown',shortcut);return()=>window.removeEventListener('keydown',shortcut);
  });
  // Tray Save errors are acknowledged in the Session notice so the native
  // role can restore its prior hidden presentation after the feedback.
  const isTraySaveNotice=operation?.last?.action==='close'&&operation.last.outcome==='save-failed'&&operation.last.message==='';
  const failedOutcome=operation?.last&&!isTraySaveNotice&&['failed','candidate-invalid','save-failed'].includes(operation.last.outcome)&&operation.last.token!==dismissedOutcome&&!pending?operation.last:null;
  const saveFailed=failedOutcome?.outcome==='save-failed'||failedOutcome?.action==='save'||failedOutcome?.action==='save-as';
  const failureTitle=t(saveFailed?'Save Project Failed':failedOutcome?.action==='open'?'Open Project Failed':'Project commands');
  const failureMessage=failedOutcome?.message||(saveFailed||failedOutcome?.action==='open'?failureTitle:language==='zh_CN'?'文档操作失败。':'Document operation failed.');
  const previewSurface=useRef<HTMLDivElement>(null);
  const visibleNotice=!pending&&!mediaPending&&!menu&&!entityMenu&&!authoredFeedback&&!importMessages.length
    &&!failedOutcome&&!deliveryError&&!snapshot.host.closing&&!snapshot.host.ending?snapshot.sessionNotice:undefined;
  async function acknowledgeNotice(id:string){
    if(noticeAcknowledgement.current)return;
    noticeAcknowledgement.current=id;setNoticeAcknowledging(id);setNoticeFailure(null);
    try{await onViewIntent({type:'AcknowledgeSessionNotice',payload:{noticeId:id}});}
    catch{if(live.current)setNoticeFailure(id);}
    finally{noticeAcknowledgement.current=null;if(live.current)setNoticeAcknowledging(null);}
  }
  const previewObserver=useRef<ReturnType<typeof observeNativePreviewRect>|null>(null);
  const notifyPreviewLayout=useCallback(()=>{previewObserver.current?.remeasure();},[]);
  const [previewState,setPreviewState]=useState<NativePreviewState>({status:'initializing'});
  useLayoutEffect(()=>{
    let facade:ProductionPreviewFacade|undefined;
    try{
      const query=new URLSearchParams(window.location.search),bridge=(window as Window&{chrome?:{webview?:SessionTransport}}).chrome?.webview;
      if(!bridge||!previewSurface.current)throw new Error('preview-presentation-unavailable');
      facade=new ProductionPreviewFacade(bridge,query.get('generation')??'',snapshot.session.incarnation);
      previewObserver.current=observeNativePreviewRect(previewSurface.current,facade,setPreviewState);
    }catch(error){setPreviewState({status:'failure',message:error instanceof Error?error.message:'preview-presentation-unavailable'});}
    return()=>{previewObserver.current?.dispose();previewObserver.current=null;facade?.dispose();};
  },[snapshot.session.incarnation]);
  return <NativePreviewOverlayScope.Provider value={true}><SessionLanguageProvider language={language}><div className="production-session session-shell session-phase-a session-phase-d"
    data-bootstrap-state="ready" data-session-incarnation={snapshot.session.incarnation} data-bound-display-id={snapshot.session.boundDisplayId}
    data-authored-revision={snapshot.document.authoredRevision} data-clean-revision={snapshot.document.cleanRevision} data-dirty={snapshot.document.dirty}
    data-selected-playlist-id={state.selectedPlaylistId??''} data-selected-item-id={state.selectedItemId??''}
    data-active-playlist-id={state.activePlaylistId??''} data-active-item-id={state.activeItemId??''} data-output-muted={state.outputMuted}
    data-language={language} data-close-pending={snapshot.host.closing} data-ending={snapshot.host.ending} data-authority="native-session"
    data-runtime-binding-token={runtimeSample?.bindingToken??''}
    data-authored-busy={structuralPending} data-media-import-token={mediaPending?.token??''}
    data-document-operation-token={pending?.token??''} data-document-operation-stage={pending?.stage??'idle'}>
    <ProductSessionBar text={t} onHide={snapshot.host.closing || snapshot.host.ending ? undefined : onHide}
      leftActions={<SessionSettings native onResetLayout={layout.reset} highlighted={snapshot.preferences.highlight} highlightAvailable={viewAllowed}
        onHighlight={highlight} background={snapshot.preferences.background} onBackground={viewAllowed?desktopBackground:undefined}/>}
      context={<><button type="button" className="session-project-trigger" disabled={!documentAllowed} title={snapshot.document.path??t('Untitled')} aria-label={t('Project commands')} aria-haspopup="menu"
        onClick={event=>{const r=event.currentTarget.getBoundingClientRect();setMenu({x:r.left,y:r.bottom+6});}}>
        <span className="session-project-name">{snapshot.document.path?snapshot.document.displayName:t('Untitled')}</span>
        {snapshot.document.dirty&&<span className="project-dirty" role="img" aria-label={t('Unsaved changes')}/>}
        <svg width="10" height="10" viewBox="0 0 12 12" fill="none" aria-hidden="true"><path d="m3 4.5 3 3 3-3"/></svg>
      </button><span className="session-context-dot">·</span><span className="session-title-display" title={displayText(language,facts.label)}>{t('DISPLAY')} {facts.descriptor?.displayNumber??'—'}</span></>}/>
    <main className="session-workspace" ref={layout.workspaceRef} style={layout.workspaceStyle} {...layout.attributes}>
      <SessionLibrary state={state} readOnly={!authoredAllowed} playbackAvailable={runtimeAllowed} masterEpoch={documentIncarnation} masterToggleDisabled={id=>masterWrites.has(masterWriteKey(documentIncarnation,id))} onSelect={select} onPlay={play} onMenu={openEntityMenu}
        onAddPlaylist={()=>void requestAuthored({type:'AddPlaylist',payload:{}})} onAddMedia={()=>{if(state.selectedPlaylistId)void requestAuthored({type:'AddMedia',payload:{playlistId:state.selectedPlaylistId}});}}
        onProjectMode={mode=>requestMode(null,mode)} onItemMode={requestMode} pendingMode={pendingMode} onMasterValue={setMaster} onMasterToggle={toggleMaster}/>
      <SessionPreviewView state={state} facts={facts} highlighted={snapshot.preferences.highlight} background={snapshot.preferences.background} language={language}
        resetEpoch={snapshot.session.incarnation} readOnly={!viewAllowed} onResetLayout={layout.reset} onHighlight={highlight}
        onBackground={desktopBackground} onBackgroundReselect={desktopBackground}
        onIdentify={()=>void identifyDisplay()} onDeploy={toggleDeployment} deploymentPending={deploymentPending} actionFeedback={viewFeedback}
        nativePreviewSurfaceRef={previewSurface} nativePreviewState={previewState} onNativePreviewLayout={notifyPreviewLayout}
        diagnostics={{'data-host-owned':snapshot.desktop.hostOwned,'data-target-registered':snapshot.desktop.targetRegistered,'data-desired':snapshot.desktop.desired,
          'data-background-persisted':snapshot.preferences.persistedBackground??'unavailable','data-preference-read-status':snapshot.preferences.readStatus}}>
        <ProductionRuntimeTimeline sample={runtimeSample} facade={runtimeFacade} allowed={runtimeAllowed}
          documentIncarnation={documentIncarnation} onSeek={requestTimelineSeek} onSeekFailure={setRuntimeFeedback} aspect={state.project.canvasAspect}/>
        <SessionPlaybackRailView state={state} notice={runtimeFailure??runtimeFeedback} display={facts} readOnly={!runtimeAllowed}
          eligibility={{canPlayPause:Boolean(runtimeSample?.canPlayPause)&&!runtimePendingTypes.has('PlayPause'),canPrevious:Boolean(runtimeSample?.canPrevious)&&!runtimePendingTypes.has('Previous'),canNext:Boolean(runtimeSample?.canNext)&&!runtimePendingTypes.has('Next'),canMute:Boolean(runtimeSample?.canMute)&&!runtimePendingTypes.has('ToggleOutputMute')}}
          onPlayback={action=>void requestRuntime({type:action.type==='previous'?'Previous':action.type==='next'?'Next':action.type==='toggle-pause'?'PlayPause':'ToggleOutputMute',payload:{}})}/>
      </SessionPreviewView>
      <SessionInspector mock={model} onMenu={openEntityMenu}/>{layout.separators}
    </main>
    {menu&&documentAllowed&&<SessionModal title={t('Project commands')} className="session-command-menu" position={menu} onClose={()=>setMenu(null)}>
      <div role="menu" aria-label={t('Project commands')}>
        {([['new','New','Ctrl+N'],['open','Open…','Ctrl+O'],['save','Save','Ctrl+S'],['save-as','Save As…','Ctrl+Shift+S'],['close','Close Session','']] as const)
          .map(([action,label,shortcut])=><button key={action} type="button" role="menuitem" data-command={action} onClick={()=>void requestDocument({action})}>
            <span>{t(label)}</span><span className="session-menu-shortcut">{shortcut}</span>
          </button>)}
      </div>
    </SessionModal>}
    {entityMenu&&<ProductionEntityCommands key={entityMenu.entity+':'+(entityMenu.itemId??entityMenu.playlistId)} target={entityMenu} state={state} disabled={!authoredAllowed}
      playbackAvailable={runtimeAllowed} onPlay={()=>play(entityMenu.entity,entityMenu.itemId??entityMenu.playlistId)} onConfigureBgm={()=>void configureBgm(entityMenu.playlistId)} onIntent={requestAuthored} onClose={()=>setEntityMenu(null)}/>}
    {!pending&&!mediaPending&&(authoredFeedback||importMessages.length>0)&&<SessionModal title={authoredFeedback?.title??t('Add Media')} initialFocus="[data-message-ok]" onClose={()=>{setAuthoredFeedback(null);if(importResult)setDismissedImport(importResult.token);}}>
      <div className="session-message" data-level="warning" data-authored-feedback>{(authoredFeedback?.messages??importMessages).map((message,index)=><p key={index}>{message}</p>)}</div>
      <div className="session-dialog-actions"><button type="button" data-message-ok className="session-dialog-primary" onClick={()=>{setAuthoredFeedback(null);if(importResult)setDismissedImport(importResult.token);}}>{t('OK')}</button></div>
    </SessionModal>}
    {pending?.stage==='decision'&&<SessionDocumentDecision key={pending.token} name={snapshot.document.path?snapshot.document.displayName:t('Untitled')}
      disabled={deliveryBusy} onDecision={choice=>void requestDocument({token:pending.token,choice})}/>}
    {!pending&&(failedOutcome||deliveryError)&&<SessionModal title={failureTitle}
      onClose={()=>{if(failedOutcome)setDismissedOutcome(failedOutcome.token);setDeliveryError(null);}} initialFocus="[data-message-ok]">
      <p className="session-message" data-level="error" data-document-feedback>{deliveryError??t(failureMessage)}</p>
      <div className="session-dialog-actions"><button type="button" data-message-ok className="session-dialog-primary" onClick={()=>{if(failedOutcome)setDismissedOutcome(failedOutcome.token);setDeliveryError(null);}}>{t('OK')}</button></div>
    </SessionModal>}
    {visibleNotice&&<SessionModal key={visibleNotice.id} title={visibleNotice.title} initialFocus="[data-notice-ok]"
      onClose={unavailable} dismissible={false}>
      <div data-session-notice>
        <p className="session-message" data-level={visibleNotice.level}>{visibleNotice.message}</p>
        {noticeFailure===visibleNotice.id&&<p className="session-message" data-level="error" role="alert">{t('The notice could not be acknowledged. Try again.')}</p>}
      </div>
      <div className="session-dialog-actions"><button type="button" data-notice-ok className="session-dialog-primary"
        disabled={noticeAcknowledging===visibleNotice.id} onClick={()=>void acknowledgeNotice(visibleNotice.id)}>{t('OK')}</button></div>
    </SessionModal>}
  </div></SessionLanguageProvider></NativePreviewOverlayScope.Provider>;
}
