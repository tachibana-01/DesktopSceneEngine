import {useLayoutEffect,useMemo,useRef,type PointerEvent as ReactPointerEvent} from 'react';

// Track the pointer that owns a gesture. Native range controls may keep their
// own capture, so callers can request identity tracking without taking capture.
export function usePointerDragCapture<T extends HTMLElement>() {
  const current=useRef<{element:T;pointerId:number;explicit:boolean}|null>(null);
  const capture=useMemo(()=>({
    start(event:Pick<ReactPointerEvent<T>,'button'|'currentTarget'|'pointerId'>,explicit=true) {
      if(event.button!==0||current.current)return false;
      const {currentTarget:element,pointerId}=event;
      if(explicit) {
        try {
          element.setPointerCapture(pointerId);
        } catch(error) {
          if(error instanceof DOMException&&error.name==='NotFoundError')return false;
          throw error;
        }
        if(!element.hasPointerCapture(pointerId))return false;
      }
      current.current={element,pointerId,explicit};
      return true;
    },
    owns(event:Pick<ReactPointerEvent<T>,'currentTarget'|'pointerId'>) {
      return current.current?.element===event.currentTarget&&current.current.pointerId===event.pointerId;
    },
    active() {
      return current.current!==null;
    },
    pointerId() {
      return current.current?.pointerId??null;
    },
    end() {
      const owner=current.current;
      current.current=null;
      if(owner?.explicit&&owner.element.hasPointerCapture(owner.pointerId))owner.element.releasePointerCapture(owner.pointerId);
    },
  }),[]);
  useLayoutEffect(()=>capture.end,[capture]);
  return capture;
}
