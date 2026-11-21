import { useEffect, useId, useLayoutEffect, useRef, useState, type PointerEvent } from 'react';
import { createPortal } from 'react-dom';
import { useSessionText } from './sessionLanguage';
import {useNativePreviewOverlay} from './NativePreviewOverlayScope';
import {useNativeRangeDrag} from './useNativeRangeDrag.ts';
import './playlistMasterControl.css';

export function PlaylistMasterControl({ id, alias, master, open, onOpen, onClose, onValue, onToggle, disabled = false, toggleDisabled = false }: {
  toggleDisabled?:boolean;
  disabled?: boolean; id: string; alias: string; master: number; open: boolean;
  onOpen: () => void; onClose: () => void;
  onValue: (id: string, percent: number) => void | Promise<boolean>; onToggle: (id: string) => void;
}) {
  const t = useSessionText(), popupId = useId();
  const previewOverlay=useNativePreviewOverlay('floating');
  const trigger = useRef<HTMLButtonElement>(null), surface = useRef<HTMLDivElement>(null), region = useRef<HTMLDivElement>(null);
  const closeTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const capture = useNativeRangeDrag();
  const inputSerial = useRef(0), settledSerial = useRef(0), alive = useRef(true);
  const [draft, setDraft] = useState<number | null>(null);
  const latest = useRef({ master, onValue, onClose }); latest.current = { master, onValue, onClose };
  const [anchor, setAnchor] = useState({ left: 0, top: 0 });
  const percent = Math.round(master * 100);
  const sliderPercent = draft !== null ? draft : percent;
  function cancelClose() { clearTimeout(closeTimer.current); closeTimer.current = undefined; }
  function closeSoon() {
    if (capture.active() || closeTimer.current !== undefined) return;
    closeTimer.current = setTimeout(() => { closeTimer.current = undefined; latest.current.onClose(); }, 100);
  }
  function inside(x: number, y: number) {
    return [trigger.current, region.current].some(element => {
      const r = element?.getBoundingClientRect();
      return r && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    });
  }
  function openNow() { if (disabled) return; cancelClose(); onOpen(); }
  // React delegates wheel passively in this environment. These actual surfaces
  // need native non-passive listeners so one wheel event cannot also scroll a list.
  useLayoutEffect(() => {
    const button = trigger.current, body = surface.current;
    function wheel(event: WheelEvent) {
      if (disabled || toggleDisabled || !event.deltaY || capture.active()) return;
      event.preventDefault(); event.stopPropagation();
      const current = latest.current;
      current.onValue(id, Math.max(0, Math.min(100, Math.round(current.master * 100) + (event.deltaY < 0 ? 5 : -5))));
    }
    button?.addEventListener('wheel', wheel, { passive: false });
    body?.addEventListener('wheel', wheel, { passive: false });
    return () => { button?.removeEventListener('wheel', wheel); body?.removeEventListener('wheel', wheel); };
  }, [id, open, disabled, toggleDisabled, capture]);
  useLayoutEffect(() => {
    if (!open) { setDraft(null); return; }
    const button = trigger.current!;
    const measure = () => {
      const r = button.getBoundingClientRect();
      if (!r.width) { latest.current.onClose(); return; }
      // Intentionally always downward: no fit, flip, desktop/work-area or edge logic.
      setAnchor(old => old.left === r.left + r.width / 2 - 28 && old.top === r.bottom ? old : { left: r.left + r.width / 2 - 28, top: r.bottom });
    };
    const move = (event: globalThis.PointerEvent) => {
      if (inside(event.clientX, event.clientY) || capture.active()) cancelClose(); else closeSoon();
    };
    const exit = (event: globalThis.PointerEvent) => { if (!event.relatedTarget) closeSoon(); };
    const blur = () => { capture.cancel(); latest.current.onClose(); };
    const visibility = () => { if (document.hidden) blur(); };
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape') blur(); };
    measure();
    const observer = new ResizeObserver(measure); observer.observe(button);
    window.addEventListener('pointermove', move); window.addEventListener('pointerout', exit);
    window.addEventListener('resize', measure); window.addEventListener('scroll', measure, true);
    window.addEventListener('blur', blur); window.addEventListener('pagehide', blur); window.addEventListener('keydown', escape);
    document.addEventListener('visibilitychange', visibility);
    return () => {
      capture.end(); cancelClose(); observer.disconnect();
      window.removeEventListener('pointermove', move); window.removeEventListener('pointerout', exit);
      window.removeEventListener('resize', measure); window.removeEventListener('scroll', measure, true);
      window.removeEventListener('blur', blur); window.removeEventListener('pagehide', blur); window.removeEventListener('keydown', escape);
      document.removeEventListener('visibilitychange', visibility);
    };
  }, [open, capture]);
  useEffect(() => {alive.current=true;return()=>{alive.current=false;cancelClose();};}, []);
  const stop = (event: { stopPropagation: () => void }) => event.stopPropagation();
  function finishDrag(event: PointerEvent<HTMLInputElement>, cancelled = false) {
    if (!capture.owns(event)) return;
    if (cancelled) capture.cancel(); else capture.end();
    if(settledSerial.current===inputSerial.current)setDraft(null);
    if (!inside(event.clientX, event.clientY)) closeSoon();
  }
  function input(value:number){
    if (capture.ignoresInput()) return;
    const serial=++inputSerial.current;setDraft(value);
    // Only the thumb uses this interaction draft. Row/Inspector/document facts
    // still come from native acknowledgements. Failure releases the draft too.
    void Promise.resolve(latest.current.onValue(id,value)).then(ok=>{
      if(!alive.current||serial!==inputSerial.current)return;
      settledSerial.current=serial;if(ok===false||!capture.active())setDraft(null);
    },()=>{if(alive.current&&serial===inputSerial.current){settledSerial.current=serial;setDraft(null);}});
  }
  return <>
    <button ref={trigger} type="button" disabled={disabled||toggleDisabled} className="playlist-volume-trigger" data-master-percent={percent}
      data-muted={master === 0} aria-label={t('Playlist Master Volume') + ' · ' + alias}
      aria-expanded={open} aria-controls={open ? popupId : undefined} title={t('Playlist Master Volume') + ' · ' + alias + ' · ' + percent + '%'}
      onPointerEnter={openNow} onPointerLeave={closeSoon} onFocus={openNow}
      onPointerDown={stop} onDoubleClick={stop} onContextMenu={event => { event.preventDefault(); event.stopPropagation(); }}
      onClick={event => { event.stopPropagation(); onToggle(id); }}
      onKeyDown={event => {
        if (event.key === 'ArrowDown') {
          event.preventDefault(); openNow();
          requestAnimationFrame(() => surface.current?.querySelector<HTMLInputElement>('input')?.focus());
        }
      }}>
      <svg width="14" height="14" viewBox="0 0 18 18" fill="none" aria-hidden="true"><path d="M3 7h3l4-3v10l-4-3H3V7Z" />
        {master === 0 ? <path d="m13 7 3 4m0-4-3 4" /> : <path d="M13 6c2 2 2 4 0 6" />}</svg>
    </button>
    {open && createPortal(<div ref={region} className="playlist-volume-overlay" style={anchor} data-playlist-volume-owner={id}>
      <div className="playlist-volume-corridor" aria-hidden="true" />
      <div ref={surface} id={popupId} className="playlist-volume-popover" data-playlist-id={id} role="group"
        data-native-preview-overlay={previewOverlay}
        aria-label={t('Playlist Master Volume') + ' · ' + alias} onPointerEnter={cancelClose} onPointerLeave={closeSoon}
        onClick={stop} onDoubleClick={stop} onContextMenu={event => { event.preventDefault(); event.stopPropagation(); }}>
        <input type="range" data-playlist-master-slider aria-label={t('Playlist Master Volume') + ' · ' + alias}
          aria-orientation="vertical" min={0} max={100} step={1} value={sliderPercent}
          onChange={event => input(Number(event.target.value))}
          onPointerDown={event => {
            event.stopPropagation();
            if (!capture.start(event)) return;
            setDraft(current => current ?? percent);
            cancelClose();
          }}
          onPointerMove={event => { if (event.pointerType === 'mouse' && !(event.buttons & 1)) finishDrag(event, true); }}
          onPointerUp={event => finishDrag(event)}
          onPointerCancel={event => finishDrag(event, true)} onLostPointerCapture={event => finishDrag(event, true)}
          onKeyDown={capture.onKeyDown} />
        <output className="playlist-volume-percent">{sliderPercent}%</output>
      </div>
    </div>, document.body)}
  </>;
}
