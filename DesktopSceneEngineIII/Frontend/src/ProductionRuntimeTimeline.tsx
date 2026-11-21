import {useEffect,useLayoutEffect,useRef,useState,type PointerEvent as ReactPointerEvent} from 'react';
import {SessionProgressRow} from './SessionProgressRow';
import {useTimelineHoverOverlay} from './useTimelineHoverOverlay.ts';
import {usePointerDragCapture} from './usePointerDragCapture.ts';
import {useSessionText} from './sessionLanguage';
import {formatRuntimeTime} from './sessionTime.ts';
import {TIMELINE_MAXIMUM,timeForValue,timelineValueForTime,valueForGroovePosition} from './sessionTimelineGeometry.ts';
import {compareRuntimeSamples,type NativeTimelineHoverRequest,type ProductionRuntimeFacade,type RuntimePlaybackSample,type RuntimeResult,type RuntimeSeekTarget} from './ProductionRuntimeFacade.ts';

type Seek={target:RuntimeSeekTarget;document:string};
type ReleasedSeek={seek:Seek;confirmation?:RuntimePlaybackSample};
type Hover=NativeTimelineHoverRequest&{status:'loading'|'ready'|'unavailable'|'decode-failed'};
function seekable(sample:RuntimePlaybackSample|null){return Boolean(sample?.hasMedia&&sample.canSeek&&sample.bindingToken&&sample.active.itemId&&Number.isFinite(sample.duration)&&sample.duration>0);}
function currentTarget(sample:RuntimePlaybackSample|null,target:RuntimeSeekTarget){return seekable(sample)&&sample!.bindingToken===target.bindingToken&&sample!.active.itemId===target.itemId&&sample!.duration===target.duration;}

// One released target may wait behind the single native request. The thumb is
// only presentation; the native owner still decides whether and where to seek.
export function ProductionRuntimeTimeline({sample,facade,allowed,documentIncarnation,onSeek,onSeekFailure,aspect}:{
  sample:RuntimePlaybackSample|null;facade:ProductionRuntimeFacade;allowed:boolean;documentIncarnation:string;
  onSeek:(target:RuntimeSeekTarget,document:string)=>Promise<RuntimeResult|null>;
  onSeekFailure:(message:string)=>void;aspect:readonly[number,number];
}){
  const t=useSessionText();
  const control=useRef<HTMLDivElement>(null),groove=useRef<HTMLSpanElement>(null);
  const capture=usePointerDragCapture<HTMLDivElement>();
  const latest=useRef({sample,allowed,documentIncarnation,facade,onSeek,onSeekFailure});
  latest.current={sample,allowed,documentIncarnation,facade,onSeek,onSeekFailure};
  const drag=useRef<Seek|null>(null),[dragValue,setDragValue]=useState<Seek|null>(null),[keyboardValue,setKeyboardValue]=useState<number|null>(null);
  const queuedSeek=useRef<Seek|null>(null),inFlightSeek=useRef<Seek|null>(null);
  const [releasedSeek,setReleasedSeek]=useState<ReleasedSeek|null>(null);
  const [hover,setHover]=useState<Hover|null>(null),hoverRef=useRef<Hover|null>(null),serial=useRef(facade.currentHoverSerial),timer=useRef<ReturnType<typeof setTimeout>|undefined>(undefined);
  const requestFrame=useRef<number|undefined>(undefined),lastDispatch=useRef<{serial:number;at:number}|null>(null);
  const [hoverX,setHoverX]=useState(0);
  const [presentation,setPresentation]=useState<'idle'|'pending'|'shown'|'unavailable'>('idle');
  const presentationDelivery=useRef(0),presentedSerial=useRef<number|null>(null);
  const enabled=allowed&&seekable(sample),duration=sample?.duration??0,time=sample?.position??0;
  function cancelDrag(){drag.current=null;capture.end();setDragValue(null);}
  function seekContextIsCurrent(seek:Seek){
    const context=latest.current;
    return context.allowed&&seek.document===context.documentIncarnation&&currentTarget(context.facade.sample??context.sample,seek.target);
  }
  function pumpSeek(){
    const seek=queuedSeek.current;
    if(!seek||inFlightSeek.current)return;
    queuedSeek.current=null;
    if(!seekContextIsCurrent(seek)){
      setReleasedSeek(shown=>shown?.seek===seek?null:shown);
      return;
    }
    inFlightSeek.current=seek;
    const finish=(result:RuntimeResult|null,message:string)=>{
      if(inFlightSeek.current!==seek)return;
      inFlightSeek.current=null;
      if(!queuedSeek.current){
        const current=seekContextIsCurrent(seek);
        if(current&&message)latest.current.onSeekFailure(message);
        const confirmation=current&&result?.status==='applied'?result.sample:null;
        setReleasedSeek(shown=>shown?.seek!==seek?shown:confirmation?{seek,confirmation}:null);
      }
      pumpSeek();
    };
    void latest.current.onSeek(seek.target,seek.document).then(
      result=>finish(result,result?.message??''),
      error=>finish(null,error instanceof Error?error.message:'Native runtime result is unavailable.'),
    );
  }
  function hideHover(){
    const hadHover=hoverRef.current!==null;
    clearTimeout(timer.current);timer.current=undefined;hoverRef.current=null;setHover(null);
    if(requestFrame.current!==undefined)cancelAnimationFrame(requestFrame.current);requestFrame.current=undefined;
    ++presentationDelivery.current;presentedSerial.current=null;setPresentation('idle');
    if(hadHover){const next=facade.nextHoverSerial();if(next!==null){serial.current=next;void facade.cancelHover(next).catch(()=>{});}}
  }
  useLayoutEffect(()=>{
    if(drag.current&&!seekContextIsCurrent(drag.current))cancelDrag();
    if(queuedSeek.current&&!seekContextIsCurrent(queuedSeek.current))queuedSeek.current=null;
    setReleasedSeek(shown=>shown&&!seekContextIsCurrent(shown.seek)?null:shown);
    setKeyboardValue(null);hideHover();
  },[allowed,documentIncarnation,sample?.bindingToken,sample?.active.itemId,sample?.duration,sample?.canSeek,sample?.hasMedia]);
  useLayoutEffect(()=>()=>{
    queuedSeek.current=null;
    inFlightSeek.current=null;
  },[facade]);
  useEffect(()=>{
    let live=true;
    const unsubscribe=facade.subscribeHover(result=>{
      const target=hoverRef.current,current=latest.current.sample;
      if(!live||!target||!latest.current.allowed||result.serial!==target.serial||result.bindingToken!==target.bindingToken||result.itemId!==target.itemId
        ||!current||current.bindingToken!==result.bindingToken||current.active.itemId!==result.itemId)return;
      if(result.status==='cancelled'||result.status==='stale'){hideHover();return;}
      const next={...target,status:result.status};hoverRef.current=next;setHover(next);
    });
    const hide=()=>{hideHover();cancelDrag();setKeyboardValue(null);};
    const visibility=()=>{if(document.hidden)hide();};
    window.addEventListener('blur',hide);window.addEventListener('pagehide',hide);document.addEventListener('visibilitychange',visibility);
    return()=>{live=false;unsubscribe();window.removeEventListener('blur',hide);window.removeEventListener('pagehide',hide);document.removeEventListener('visibilitychange',visibility);hideHover();cancelDrag();};
  },[facade]);
  function logicalValue(clientX:number){const rect=groove.current!.getBoundingClientRect();return valueForGroovePosition(clientX,rect.left,rect.width);}
  function currentHoverRequest(){
    const target=hoverRef.current,current=latest.current.sample;
    return target&&latest.current.allowed&&seekable(current)&&current!.bindingToken===target.bindingToken&&current!.active.itemId===target.itemId
      &&presentedSerial.current===target.serial&&lastDispatch.current?.serial!==target.serial?target:null;
  }
  function scheduleHoverRequest(){
    if(timer.current!==undefined||requestFrame.current!==undefined||!currentHoverRequest())return;
    // Idle targets wait only for a frame. Keep the existing75ms as an inter-
    // dispatch budget; movement updates one latest target without restarting it.
    // The timestamp survives hide, while all scheduled work is cancelled there.
    const remaining=lastDispatch.current?75-(performance.now()-lastDispatch.current.at):0;
    if(remaining>0){timer.current=setTimeout(()=>{timer.current=undefined;scheduleHoverRequest();},remaining);return;}
    requestFrame.current=requestAnimationFrame(()=>{
      requestFrame.current=undefined;const target=currentHoverRequest();if(!target)return;
      lastDispatch.current={serial:target.serial,at:performance.now()};
      const {bindingToken,itemId,serial:requestSerial,time:requestTime}=target;
      void facade.requestHover({bindingToken,itemId,serial:requestSerial,time:requestTime}).then(ack=>{
        if(hoverRef.current?.serial!==requestSerial||ack.serial!==requestSerial||ack.status==='pending')return;
        if(ack.status==='stale'||ack.status==='cancelled'){hideHover();return;}
        const unavailable={...target,status:'unavailable' as const};hoverRef.current=unavailable;setHover(unavailable);
      }).catch(()=>{
        if(hoverRef.current?.serial!==requestSerial)return;
        const unavailable={...target,status:'unavailable' as const};hoverRef.current=unavailable;setHover(unavailable);
      });
    });
  }
  function press(event:ReactPointerEvent<HTMLDivElement>){
    const current=latest.current.sample;
    if(!latest.current.allowed||!seekable(current)||!capture.start(event))return;
    event.preventDefault();event.currentTarget.focus();hideHover();setKeyboardValue(null);
    drag.current={document:latest.current.documentIncarnation,target:{bindingToken:current!.bindingToken,itemId:current!.active.itemId!,duration:current!.duration,value:logicalValue(event.clientX)}};
    setDragValue(drag.current);
  }
  function move(event:ReactPointerEvent<HTMLDivElement>){
    const current=latest.current.sample;
    if(drag.current){
      if(!capture.owns(event))return;
      if(event.pointerType==='mouse'&&!(event.buttons&1)){cancelDrag();return;}
      if(!seekContextIsCurrent(drag.current)){cancelDrag();return;}
      drag.current={...drag.current,target:{...drag.current.target,value:logicalValue(event.clientX)}};setDragValue(drag.current);return;
    }
    if(event.buttons||!latest.current.allowed||!seekable(current)){hideHover();return;}
    const next=facade.nextHoverSerial();if(next===null){hideHover();return;}serial.current=next;
    const value=logicalValue(event.clientX),request:Hover={bindingToken:current!.bindingToken,itemId:current!.active.itemId!,serial:next,time:timeForValue(value,current!.duration),status:'loading'};
    hoverRef.current=request;setHover(request);setHoverX(event.clientX);
  }
  function release(event:ReactPointerEvent<HTMLDivElement>){
    if(!capture.owns(event))return;
    const seek=drag.current;cancelDrag();
    if(!seek||!seekContextIsCurrent(seek))return;
    // Exact-end is native sequence navigation. Until that request settles
    // or its binding retires, another release cannot replace it or navigate twice.
    const nextPending=[queuedSeek.current,inFlightSeek.current].some(
      pending=>pending?.target.value===TIMELINE_MAXIMUM&&seekContextIsCurrent(pending),
    );
    if(nextPending)return;
    queuedSeek.current=seek;
    setReleasedSeek({seek});
    onSeekFailure('');
    pumpSeek();
  }
  function cancelPointer(event:ReactPointerEvent<HTMLDivElement>){
    if(!capture.owns(event))return;
    cancelDrag();hideHover();
  }
  const pendingTarget=releasedSeek&&releasedSeek.seek.document===documentIncarnation&&enabled&&currentTarget(sample,releasedSeek.seek.target)
    &&(!releasedSeek.confirmation||!sample||compareRuntimeSamples(sample,releasedSeek.confirmation)<0)?releasedSeek.seek.target:null;
  const previewTarget=dragValue?.target??pendingTarget;
  const value=dragValue?.target.value??keyboardValue??pendingTarget?.value??timelineValueForTime(time,duration);
  const displayedTime=previewTarget?timeForValue(previewTarget.value,previewTarget.duration):time;
  const placement=useTimelineHoverOverlay(Boolean(hover),hoverX,aspect,groove,true);
  useLayoutEffect(()=>{
    const target=hoverRef.current;
    if(!target)return;
    if(!placement){if(presentedSerial.current===target.serial)hideHover();return;}
    // A hover serial and its layout sample must refer to the same pointer. The
    // measurement hook can commit after the interaction render that requested it.
    if(placement.measurement.pointerX!==hoverX||!placement.measurement.lane)return;
    const delivery=++presentationDelivery.current;presentedSerial.current=target.serial;setPresentation('pending');
    const {bindingToken,itemId,serial:requestSerial,time:requestTime}=target;
    void facade.presentHover({bindingToken,itemId,serial:requestSerial,time:requestTime,
      rect:{x:placement.left,y:placement.top,width:placement.width,height:placement.height},
      lane:placement.measurement.lane,imageSize:{width:placement.contentWidth,height:placement.contentHeight},
      viewport:placement.measurement.viewport,rasterizationScale:placement.measurement.rasterizationScale,
      timeLabel:formatRuntimeTime(requestTime)}).then(ack=>{
      if(delivery!==presentationDelivery.current||hoverRef.current?.serial!==requestSerial)return;
      setPresentation(ack.serial===requestSerial&&ack.status==='shown'?'shown':'unavailable');
    }).catch(()=>{
      if(delivery===presentationDelivery.current&&hoverRef.current?.serial===requestSerial)setPresentation('unavailable');
    });
    scheduleHoverRequest();
  },[placement,hover?.serial,hoverX,facade]);
  return <section className={'session-timeline'+(hover?' is-hovering':'')} aria-label={t('Active runtime timeline')}
    data-enabled={enabled} data-item-id={sample?.active.itemId??''} data-binding-token={sample?.bindingToken??''} data-runtime-time={time}
    data-duration={duration} data-preview-time={displayedTime} data-dragging={Boolean(dragValue)} data-hover-active={Boolean(hover)} data-hover-status={hover?.status??'unavailable'} data-serial={hover?.serial??serial.current}
    data-hover-presentation-owner="native-session" data-hover-presentation={presentation}>
    <SessionProgressRow value={value} time={displayedTime} duration={duration} enabled={enabled} preview={Boolean(dragValue)} controlRef={control} grooveRef={groove}
      controlProps={{onPointerDown:press,onPointerMove:move,onPointerUp:release,onPointerCancel:cancelPointer,onLostPointerCapture:cancelPointer,
        onPointerLeave:hideHover,onBlur:()=>{cancelDrag();hideHover();setKeyboardValue(null);},onKeyDown:event=>{
          if(event.key==='Escape'){event.preventDefault();cancelDrag();hideHover();setKeyboardValue(null);return;}
          if(!enabled)return;
          const increments:Record<string,number>={ArrowLeft:-1,ArrowDown:-1,ArrowRight:1,ArrowUp:1,PageDown:-10,PageUp:10};
          if(event.key in increments||event.key==='Home'||event.key==='End'){
            event.preventDefault();setKeyboardValue(event.key==='Home'?0:event.key==='End'?TIMELINE_MAXIMUM:Math.min(TIMELINE_MAXIMUM,Math.max(0,value+increments[event.key])));
          }
        }}}/>
    <span className="timeline-sr" role="status">{hover&&presentation==='unavailable'?t('Preview unavailable'):''}</span>
  </section>;
}
