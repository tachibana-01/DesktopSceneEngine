// Test-only wire facts. Never imported by the production entry.
export const generation='24d93f2f-13d5-454b-b7bb-aa3402aa2311',incarnation='ae0581a3-1a5c-4dbf-b3d4-e76ac3563ef2';
export const ids={a:'10000000-0000-0000-0000-000000000001',b:'10000000-0000-0000-0000-000000000002',image:'20000000-0000-0000-0000-000000000001',video:'20000000-0000-0000-0000-000000000002'};
export function fixtureAudioEffects() {
  return {version:1,documentIncarnation:'30000000-0000-0000-0000-000000000001',execution:'1',capacityRevision:'1',
    closing:false,stopped:false,desiredMuted:true,appliedMuted:true,muteKnown:true,deviceHealth:'healthy',effects:[]};
}
const item=(id,kind,alias)=>({id,kind,alias,source:'C:\\Media\\'+alias+(kind==='image'?'.png':'.mp4'),enabled:true,duration:12,rate:1.25,mediaGain:.5,
  presentation:{crop:{x:0,y:0,width:1,height:1},layout:{positionX:0,positionY:0,scaleX:1,scaleY:1},mediaPresentation:{mode:'native',customAspect:{numerator:16,denominator:9}}}});
export function snapshot(){return {
  session:{incarnation,boundDisplayId:'opaque native display'},
  document:{path:'C:\\Projects\\native.dse',displayName:'native.dse',authoredRevision:'18446744073709551614',cleanRevision:'18446744073709551613',dirty:true},
  project:{displayOwner:'opaque native display',canvasAspect:[16,9],playlistMode:'list-loop',playlists:[
    {id:ids.a,alias:'Native active playlist',enabled:true,masterVolume:.7,itemMode:'list-loop',items:[item(ids.image,'image','Native image')]},
    {id:ids.b,alias:'Native selected playlist',enabled:true,masterVolume:.4,itemMode:'repeat-current',items:[item(ids.video,'video','Native video')]}]},
  selection:{playlistId:ids.b,itemId:ids.video},active:{playlistId:ids.a,itemId:ids.image},
  display:{status:'success',descriptor:{id:'opaque native display',displayNumber:1,friendlyName:'Native panel',sourceDeviceName:'NATIVE_SOURCE',desktopX:0,desktopY:0,pixelWidth:1920,pixelHeight:1080,primary:true,refreshHz:60}},
  desktop:{desired:false,targetRegistered:false,hostOwned:false,hostHealthy:false,busy:false,canDeploy:true,canUndeploy:false},
  preferences:{readStatus:'Unavailable',highlight:true,background:'reveal',backgroundReadStatus:'Success',persistedBackground:'reveal'},
  language:{readStatus:'Success',committed:'en',effective:'en'},
  host:{ready:true,sessionRoleReady:true,closing:false,ending:false,visible:true,minimized:false,maximized:false,hideEligible:true},
  documentOperation:{pending:null,last:null},
  mediaImport:{pending:null,last:null},
  audioEffects:fixtureAudioEffects(),
};}

export function productionSessionUrl(origin,page=generation,session=incarnation) {
  return origin+'/index.html?'+new URLSearchParams({mode:'production',view:'session',generation:page,session,presentation:'model-c-production'});
}

// This controlled peer emits a dedicated runtime observation for the selected
// active fixture item. Playback and mute are absent from its Main snapshot.
export function fixtureRuntimeSample(value) {
  const items=value.project.playlists.find(playlist=>playlist.id===value.active.playlistId)?.items??[];
  const item=items.find(item=>item.id===value.active.itemId),hasMedia=Boolean(item);
  return {
    bindingToken:'fixture-binding',asOfEffectSequence:'0',asOfTimeNs:'0',active:value.active,
    hasMedia,state:hasMedia?'paused':'unavailable',
    position:hasMedia?3:0,duration:hasMedia?12:0,actualRate:hasMedia?1.1:1,outputMuted:true,
    canPlayPause:hasMedia,canPrevious:false,canNext:hasMedia,canSeek:hasMedia,canMute:true,
    sceneIndex:hasMedia?items.indexOf(item):-1,sceneCount:hasMedia?items.length:0,
    mediaKind:item?.kind??null,
  };
}

export function fixturePreviewReply() {
  let accepted=null,revision=0;
  return command=>{
    const {rect,visible,rasterizationScale}=command.payload;
    const fitted=rect.width>0&&rect.height>0&&rect.x>=0&&rect.y>=0;
    const desiredAccepted=visible&&fitted;
    if(!visible)accepted=null;
    else if(fitted)accepted={
      revision:String(++revision),page:command.generation,binding:'1',requestId:command.requestId,
      rect:Object.fromEntries(Object.entries(rect).map(([key,value])=>[key,Math.round(value*rasterizationScale)])),
      contentGeneration:'1',deviceGeneration:'1',
    };
    const requestOutcome=desiredAccepted?'accepted':visible?'invalid-request':'lifecycle-retired';
    return {status:accepted?'visible':'suspended',desiredAccepted,desiredRequestId:command.requestId,requestOutcome,
      reason:requestOutcome,accepted,lifecycleValid:Boolean(accepted),apertureOwner:'host-composition-clip'};
  };
}
