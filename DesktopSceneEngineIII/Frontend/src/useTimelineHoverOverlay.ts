import {useCallback,useLayoutEffect,useRef,useState,type RefObject} from 'react';
import {timelineHoverPlacement,type HoverPlacement} from './timelineHoverPlacement';

type OverlayPlacement=HoverPlacement&{measurement:{pointerX:number;
  lane:{x:number;y:number;width:number;height:number}|null;
  viewport:{width:number;height:number};rasterizationScale:number}};

// Presentation measurements only. No hover time, decode identity or scene state.
export function useTimelineHoverOverlay(
  active:boolean,pointerX:number,aspect:readonly[number,number],
  groove:RefObject<HTMLElement|null>,nativePopup=false,
) {
  const [placement,setPlacement]=useState<OverlayPlacement|null>(null);
  const pointer=useRef(pointerX);pointer.current=pointerX;
  const [numerator,denominator]=aspect;
  const measure=useCallback(()=>{
    const line=groove.current,shell=line?.closest('.session-shell');
    if(!line||!shell)return;
    const lane=nativePopup?line.closest('.session-timeline'):null;
    if(nativePopup&&!lane){setPlacement(null);return;}
    const laneRect=lane?.getBoundingClientRect(),viewport={width:innerWidth,height:innerHeight};
    const candidate=timelineHoverPlacement(shell.getBoundingClientRect(),line.getBoundingClientRect(),viewport,
      pointer.current,[numerator,denominator],laneRect);
    const next=candidate?{...candidate,measurement:{pointerX:pointer.current,
      lane:laneRect?{x:laneRect.x,y:laneRect.y,width:laneRect.width,height:laneRect.height}:null,
      viewport,rasterizationScale:devicePixelRatio}}:null;
    setPlacement(old=>JSON.stringify(old)===JSON.stringify(next)?old:next);
  },[groove,numerator,denominator,nativePopup]);
  useLayoutEffect(()=>{if(active)measure();else setPlacement(null);},[active,pointerX,measure]);
  useLayoutEffect(()=>{
    const line=groove.current,shell=line?.closest('.session-shell');
    if(!active||!line||!shell)return;
    const observer=new ResizeObserver(measure);observer.observe(line);observer.observe(shell);
    const lane=nativePopup?line.closest('.session-timeline'):null;if(lane)observer.observe(lane);
    window.addEventListener('resize',measure);window.addEventListener('scroll',measure,true);
    return()=>{observer.disconnect();window.removeEventListener('resize',measure);window.removeEventListener('scroll',measure,true);};
  },[active,groove,measure,nativePopup]);
  return placement;
}
