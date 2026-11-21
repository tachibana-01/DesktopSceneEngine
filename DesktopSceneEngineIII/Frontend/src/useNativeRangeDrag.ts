import {useCallback, useLayoutEffect, useMemo, useRef, type KeyboardEvent, type PointerEvent} from 'react';
import {usePointerDragCapture} from './usePointerDragCapture.ts';

const rangeKeys = ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Home', 'End', 'PageUp', 'PageDown'];

// The browser can continue generating range input after our captured gesture
// was cancelled. Fence that input until Release or a new intentional edit.
export function useNativeRangeDrag() {
  const capture = usePointerDragCapture<HTMLInputElement>();
  const ignored = useRef<((event:globalThis.PointerEvent)=>void) | null>(null);
  const clear = useCallback(() => {
    const previous = ignored.current;
    if (!previous) return;
    ignored.current = null;
    window.removeEventListener('pointerup', previous, true);
  }, []);
  useLayoutEffect(() => clear, [clear]);

  return useMemo(() => ({
    ...capture,
    start(event:PointerEvent<HTMLInputElement>, explicit = true) {
      if (!capture.start(event, explicit)) return false;
      clear();
      return true;
    },
    cancel() {
      const pointerId = capture.pointerId();
      if (pointerId === null) return false;
      clear();
      const finished = (event:globalThis.PointerEvent) => {
        if (event.pointerId !== pointerId) return;
        queueMicrotask(() => { if (ignored.current === finished) clear(); });
      };
      ignored.current = finished;
      window.addEventListener('pointerup', finished, true);
      capture.end();
      return true;
    },
    ignoresInput() {
      return ignored.current !== null;
    },
    onKeyDown(event:KeyboardEvent<HTMLInputElement>) {
      if (rangeKeys.includes(event.key)) clear();
    },
  }), [capture, clear]);
}
