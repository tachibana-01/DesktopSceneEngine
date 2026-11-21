import {useEffect,useRef,useState} from 'react';
import {BackgroundMusicShell} from './BackgroundMusicShell';
import {ProductTitleBar} from './ProductTitleBar.tsx';
import {SessionLanguageProvider,translateSession} from './sessionLanguage';
import {BgmScalarReplies,ProductionBgmFacade,type BgmAuthoredResult,type BgmEdit,type BgmHeader,type BgmRuntimeState,type BgmScope,type BgmSnapshot,type BgmTarget} from './ProductionBgmFacade.ts';
import type {SessionTransport} from './ProductionSessionFacade.ts';
import {LatestUnsentAudioScalar,type ScalarDispatch} from './LatestUnsentAudioScalar.ts';
import {audioEffectPending} from './ProductionAudioEffects.ts';
import {useProductPresentation} from './productWindowHost.ts';

export function ProductionBackgroundMusicShell(){
  const [snapshot,setSnapshot]=useState<BgmSnapshot|null>(null),projection=useRef<BgmSnapshot|null>(null);
  const [failure,setFailure]=useState<string|null>(null),[notice,setNotice]=useState('');
  useProductPresentation(snapshot ? 'ready' : failure ? 'failed' : 'loading');
  const runtimeProblems=useRef<Record<BgmScope,string|null>>({playlist:null,item:null});
  const [runtimeFailures,setRuntimeFailures]=useState(runtimeProblems.current);
  const hydrationSerial=useRef({playlist:0,item:0});
  const [playlistRuntime,setPlaylistRuntime]=useState<BgmRuntimeState|null>(null),[itemRuntime,setItemRuntime]=useState<BgmRuntimeState|null>(null);
  const [playlistSelection,setPlaylistSelection]=useState<string|null>(null),[itemSelection,setItemSelection]=useState<string|null>(null);
  const wire=useRef<ProductionBgmFacade|null>(null),live=useRef(false),readSerial=useRef(0),hydratedRevision=useRef('0');
  const desiredItem=useRef<string|null>(null);
  const scalarDrivers=useRef(new Map<string,{scope:BgmScope;identity:string;edit:BgmEdit;driver:LatestUnsentAudioScalar}>());
  const scalarSender=useRef<(scope:BgmScope,edit:BgmEdit)=>Promise<ScalarDispatch>>(async()=> 'invalidated');
  const order=useRef(new BgmScalarReplies()),runtimeEpoch=useRef({playlist:0,item:0}),lastImportSeen=useRef<string|null>(null),header=useRef<BgmHeader|null>(null);
  const text=(value:string)=>translateSession(projection.current?.language??'en',value);
  function commit(value:BgmSnapshot){
    for(const [key,d]of scalarDrivers.current){
      const captured=JSON.parse(d.identity) as unknown[];
      const queue=d.scope==='playlist'?value.playlistQueue:value.itemQueue;
      const track=d.edit.type==='BgmSetTrackVolume'?d.edit.trackId:null;
      if(captured[0]!==value.permit||captured[1]!==value.documentIncarnation||captured[2]!==value.playlist.id
        ||d.scope==='item'&&captured[4]!==value.selectedItemId||!queue||track!==null&&!queue.tracks.some(t=>t.id===track)){
        d.driver.dispose();scalarDrivers.current.delete(key);
      }
    }
    projection.current=value;setSnapshot(value);
  }
  function target(value:BgmSnapshot,scope:BgmScope):BgmTarget|null{
    if(scope==='item'&&!value.selectedItemId)return null;
    return {permit:value.permit,scope,playlistId:value.playlist.id,...(scope==='item'?{itemId:value.selectedItemId!}:{})};
  }
  function runtimeCurrent(value:BgmRuntimeState){const p=projection.current;return p&&value.playlistId===p.playlist.id&&(value.scope==='playlist'?value.itemId===null:value.itemId===p.selectedItemId);}
  function runtime(value:BgmRuntimeState){if(!runtimeCurrent(value))return;if(value.scope==='playlist')setPlaylistRuntime(value);else setItemRuntime(value);}
  function runtimeProblem(scope:BgmScope,message:string|null){runtimeProblems.current={...runtimeProblems.current,[scope]:message};setRuntimeFailures(runtimeProblems.current);}
  async function hydrateRuntime(value:BgmSnapshot,scopes:readonly BgmScope[]=['playlist','item']){const f=wire.current;if(!f)return;
    await Promise.all(scopes.map(async scope=>{
      const owner=target(value,scope),serial=++hydrationSerial.current[scope];
      if(!owner){setItemRuntime(null);runtimeProblem(scope,null);return;}
      const version=runtimeEpoch.current[scope];
      const current=()=>live.current&&wire.current===f&&serial===hydrationSerial.current[scope]&&version===runtimeEpoch.current[scope]
        &&projection.current?.permit===owner.permit&&projection.current.playlist.id===owner.playlistId
        &&(scope==='playlist'||projection.current.selectedItemId===owner.itemId);
      const unavailable=(message:string)=>{if(!current())return;runtimeProblem(scope,message);if(scope==='playlist')setPlaylistRuntime(null);else setItemRuntime(null);};
      try{
        const result=await f.runtime(owner);if(!current())return;
        if(result.status!=='ready'||!result.state||!runtimeCurrent(result.state)){
          unavailable(text('Native BGM editor unavailable')+' · '+scope+' · '+result.reason);return;
        }
        runtime(result.state);runtimeProblem(scope,null);
      }catch(error){unavailable(error instanceof Error?error.message:text('Native BGM editor unavailable'));}
    }));
  }
  function resultNotice(result:BgmAuthoredResult){
    const messages=result.warnings.map(w=>w.message);
    if(result.bgmImport==='cancelled'||result.status==='picker-cancelled')messages.unshift(text('Picker cancelled'));
    else if(result.bgmSources?.some(s=>s.authored!=='committed'))messages.unshift(text('Some sources were not added'));
    else if(!['committed','unchanged'].includes(result.authored)&&result.status!=='accepted')messages.unshift(text('Native edit rejected')+' · '+result.status);
    if(result.runtime==='failed'&&!messages.length)messages.push(text('Authored change saved; runtime synchronization failed'));
    setNotice(messages.join(' · '));
  }
  async function read(itemId:string|null,initial=false,retryScopes:readonly BgmScope[]=[]){
    const f=wire.current;if(!f)return;const serial=++readSerial.current;
    try{
      const value=await f.read(itemId);if(!live.current||wire.current!==f||serial!==readSerial.current)return;
      const previous=projection.current;
      if(previous&&(previous.permit!==value.permit||previous.documentIncarnation!==value.documentIncarnation||previous.playlist.id!==value.playlist.id))throw new Error('The native BGM editor context changed unexpectedly.');
      // A queued read must not replace a more recent authoritative scalar ACK.
      if(previous&&BigInt(value.revision)<BigInt(previous.revision)){void read(projection.current?.selectedItemId??null);return;}
      commit(value);desiredItem.current=value.selectedItemId;hydratedRevision.current=value.revision;setFailure(null);
      header.current={revision:value.revision,dirty:value.dirty,language:value.language,playlistAlias:value.playlist.alias,busy:value.busy,pending:value.pending?.token??null,lastImportToken:value.lastImport?.token??null};
      setPlaylistSelection(selected=>value.playlistQueue.tracks.some(t=>t.id===selected)?selected:value.playlistQueue.tracks[0]?.id??null);
      setItemSelection(selected=>previous?.selectedItemId===value.selectedItemId&&value.itemQueue?.tracks.some(t=>t.id===selected)?selected:value.itemQueue?.tracks[0]?.id??null);
      if(value.lastImport?.token&&lastImportSeen.current!==value.lastImport.token){lastImportSeen.current=value.lastImport.token;resultNotice(value.lastImport);}
      if(initial||previous?.selectedItemId!==value.selectedItemId){setItemRuntime(null);await hydrateRuntime(value);}
      else if(retryScopes.length)await hydrateRuntime(value,retryScopes);
    }catch(error){if(live.current&&wire.current===f&&serial===readSerial.current)setFailure(error instanceof Error?error.message:text('Native BGM editor unavailable'));}
  }
  function invalidated(value:BgmHeader){const p=projection.current;if(!p){void read(null,true);return;}const prior=header.current;header.current=value;
    // A native busy→available transition is a bounded recovery opportunity,
    // including Cancel with no authored revision change. Host bindings may
    // have retired while native control was unavailable, even if the last DOM
    // facts still looked healthy. Re-arm both current scopes once per edge.
    const recovered=prior?.busy===true&&!value.busy;
    const retries=recovered?(['playlist','item'] as const):[];
    if(BigInt(value.revision)>BigInt(p.revision)||value.pending!==prior?.pending||value.lastImportToken!==prior?.lastImportToken||recovered){void read(desiredItem.current,false,retries);return;}
    // Header facts are native values. They do not allocate or reconstruct queues.
    if(BigInt(value.revision)===BigInt(p.revision))commit({...p,dirty:value.dirty,language:value.language,busy:value.busy,playlist:{...p.playlist,alias:value.playlistAlias}});
  }
  useEffect(()=>{
    live.current=true;let f:ProductionBgmFacade|undefined,unsubscribeHeader:(()=>void)|undefined,unsubscribeRuntime:(()=>void)|undefined,unsubscribeEffects:(()=>void)|undefined;
    try{const q=new URLSearchParams(window.location.search),bridge=(window as Window&{chrome?:{webview?:SessionTransport}}).chrome?.webview;
      if(q.get('mode')!=='production'||q.get('view')!=='background-music')throw new Error('Native BGM editor context unavailable.');
      f=new ProductionBgmFacade(bridge,q.get('generation')??'',q.get('session')??'');wire.current=f;
      unsubscribeEffects=f.audioEffects.subscribe(value=>{
        if(value.closing||value.effects.some(e=>e.scope==='structural'&&audioEffectPending(e))){for(const d of scalarDrivers.current.values())d.driver.setIdentity(null);return;}
        const frontier=JSON.stringify(value);
        for(const d of scalarDrivers.current.values())if(!value.effects.some(e=>e.scope===(d.scope==='playlist'?'playlist-bgm-gain':'item-bgm-gain')&&audioEffectPending(e)))d.driver.capacityAvailable(d.identity,frontier);
      });
      unsubscribeHeader=f.subscribeHeader(invalidated);unsubscribeRuntime=f.subscribeRuntime(value=>{if(runtimeCurrent(value)){++runtimeEpoch.current[value.scope];runtime(value);runtimeProblem(value.scope,null);}});
      void read(null,true);
    }catch(error){setFailure(error instanceof Error?error.message:'Native BGM editor unavailable');}
    return()=>{live.current=false;++readSerial.current;unsubscribeHeader?.();unsubscribeRuntime?.();unsubscribeEffects?.();for(const d of scalarDrivers.current.values())d.driver.dispose();scalarDrivers.current.clear();f?.dispose();wire.current=null;};
  },[]);
  useEffect(()=>{if(snapshot)document.title=text('Background Music')+' — '+snapshot.playlist.alias;},[snapshot?.language,snapshot?.playlist.alias]);

  async function performEdit(scope:BgmScope,edit:BgmEdit):Promise<ScalarDispatch>{
    const p=projection.current,f=wire.current;if(!p||!f)return 'invalidated';const owner=target(p,scope);if(!owner)return 'invalidated';
    const scalar=edit.type==='BgmSetQueueVolume'||edit.type==='BgmSetTrackVolume';
    const key=scope+':'+(owner.itemId??'')+':'+(edit.type==='BgmSetTrackVolume'?edit.trackId:'queue');
    const request=scalar?order.current.next(key):0;
    try{
      const result=await f.edit(owner,edit);if(!live.current||wire.current!==f)return 'invalidated';
      const current=projection.current;if(!current||current.permit!==owner.permit||current.playlist.id!==owner.playlistId)return 'invalidated';
      if(result.bgmTarget&&(result.bgmTarget.scope!==scope||result.bgmTarget.playlistId!==owner.playlistId||result.bgmTarget.itemId!==owner.itemId))throw new Error('Native BGM result target mismatch.');
      resultNotice(result);
      if(scalar&&result.bgmReadback&&order.current.accepts(key,request,result.revision,hydratedRevision.current)){
        const oldRevision=BigInt(current.revision),newRevision=BigInt(result.revision);
        if(newRevision>oldRevision+1n){await read(current.selectedItemId);return 'terminal';}
        if(scope==='item'&&current.selectedItemId!==owner.itemId)return 'invalidated';
        const queue=scope==='playlist'?current.playlistQueue:current.itemQueue;if(!queue)return 'invalidated';
        const rb=result.bgmReadback;
        const next=edit.type==='BgmSetQueueVolume'&&rb.queueVolume!==undefined?{...queue,volume:rb.queueVolume}
          :edit.type==='BgmSetTrackVolume'&&rb.trackVolume!==undefined?{...queue,tracks:queue.tracks.map(t=>t.id===edit.trackId?{...t,volume:rb.trackVolume!}:t)}:queue;
        commit({...current,revision:newRevision>=oldRevision?result.revision:current.revision,dirty:newRevision>=oldRevision?result.dirty:current.dirty,
          ...(scope==='playlist'?{playlistQueue:next}:{itemQueue:next})});
      }else if(!scalar||!result.bgmReadback)await read(current.selectedItemId);
      return result.status==='busy'?'busy':['committed','unchanged'].includes(result.authored)?'terminal':'invalidated';
    }catch(error){if(live.current&&wire.current===f){setNotice(error instanceof Error?error.message:text('Native edit rejected'));await read(projection.current?.selectedItemId??null);}return 'unknown';}
  }
  scalarSender.current=performEdit;
  async function edit(scope:BgmScope,edit:BgmEdit):Promise<void>{
    if(edit.type!=='BgmSetQueueVolume'&&edit.type!=='BgmSetTrackVolume'){
      for(const d of scalarDrivers.current.values())d.driver.setIdentity(null);await performEdit(scope,edit);return;
    }
    const p=projection.current;if(!p)return;const owner=target(p,scope);if(!owner)return;
    const identity=JSON.stringify([p.permit,p.documentIncarnation,p.playlist.id,scope,owner.itemId??null,edit.type,edit.type==='BgmSetTrackVolume'?edit.trackId:null]);
    let entry=scalarDrivers.current.get(identity);
    if(!entry){entry={scope,identity,edit,driver:new LatestUnsentAudioScalar(async(key,value)=>{
      const current=scalarDrivers.current.get(identity);if(!current||current.identity!==key)return 'invalidated';
      const captured=current.edit;if(captured.type!=='BgmSetQueueVolume'&&captured.type!=='BgmSetTrackVolume')return 'invalidated';
      return scalarSender.current(scope,{...captured,value});
    })};scalarDrivers.current.set(identity,entry);}
    entry.identity=identity;entry.edit=edit;entry.driver.setIdentity(identity);await entry.driver.submit(edit.value);
  }
  async function use(scope:BgmScope,id:string){const p=projection.current,f=wire.current;if(!p||!f)return;const owner=target(p,scope);if(!owner)return;const version=runtimeEpoch.current[scope];
    try{const result=await f.use(owner,id);if(!live.current||wire.current!==f)return;if(result.state&&version===runtimeEpoch.current[scope])runtime(result.state);setNotice(result.status==='rejected'?text('Native Use rejected')+' · '+result.reason:'');}
    catch(error){if(live.current&&wire.current===f)setNotice(error instanceof Error?error.message:text('Native Use rejected'));}
  }
  if(!snapshot)return <div className="bgm-entry-shell" data-bootstrap-state={failure?'failure':'loading'}>
    <ProductTitleBar text={text} beforeDrag={<span className="bgm-chrome-title">{text('Background Music')}</span>} />
    <main className="bgm-entry-status"><h1>{failure?'Native BGM editor unavailable':'Connecting to the native BGM editor…'}</h1>{failure&&<p role="alert">{failure}</p>}</main>
  </div>;
  return <SessionLanguageProvider language={snapshot.language}><BackgroundMusicShell snapshot={snapshot} playlistRuntime={playlistRuntime} itemRuntime={itemRuntime}
    playlistSelection={playlistSelection} itemSelection={itemSelection} onSelect={(scope,id)=>scope==='playlist'?setPlaylistSelection(id):setItemSelection(id)}
    onItem={id=>{for(const [key,d]of scalarDrivers.current)if(d.scope==='item'){d.driver.dispose();scalarDrivers.current.delete(key);}desiredItem.current=id;setItemRuntime(null);setItemSelection(null);void read(id,true);}} onEdit={edit} onUse={use} notice={notice} unavailable={failure!==null||runtimeFailures.playlist!==null||runtimeFailures.item!==null}/>
    {(runtimeFailures.playlist||runtimeFailures.item)&&<p className="bgm-shell-feedback" role="alert" data-bgm-runtime-unavailable>{runtimeFailures.playlist??runtimeFailures.item}</p>}
  </SessionLanguageProvider>;
}
