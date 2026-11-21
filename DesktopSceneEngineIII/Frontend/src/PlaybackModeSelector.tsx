import { useEffect, useId, useLayoutEffect, useRef, useState, type CSSProperties, type KeyboardEvent, type PointerEvent } from 'react';
import { createPortal } from 'react-dom';
import { PlaybackModeIcon } from './PlaybackModeIcon';
import type { SequenceMode } from './sessionPlaybackSequence.ts';
import { useSessionText } from './sessionLanguage';
import { documentShortcut } from './sessionShortcuts.ts';
import {useNativePreviewOverlay} from './NativePreviewOverlayScope';
import {playbackModePaint,placePlaybackModePopup,type PlaybackModePlacement} from './playbackModePlacement';
import './playbackModeSelector.css';

const options = [['list-loop', 'List Loop'], ['random', 'Random'], ['repeat-current', 'Repeat Current']] as const;
type Point = { x: number; y: number };

// Only interaction state lives here. Policy always comes from the owner.
export function PlaybackModeSelector({ scope, owner, label, value, pendingValue, disabled = false, onValue, open, popupActive, onOpen, onClose }: {
  scope: 'project' | 'playlist'; owner: string | null; label: string; value: SequenceMode | '';
  disabled?: boolean; pendingValue?: () => SequenceMode | undefined; onValue: (mode: SequenceMode) => void;
  open: boolean; popupActive: boolean; onOpen: () => void; onClose: () => void;
}) {
  const t = useSessionText(), popupId = useId(), tooltipId = useId();
  const previewOverlay=useNativePreviewOverlay('floating');
  const trigger = useRef<HTMLButtonElement>(null), popup = useRef<HTMLDivElement>(null);
  const [tip, setTip] = useState<Point | null>(null);
  const [anchor, setAnchor] = useState<PlaybackModePlacement|null>(null);
  const opened = useRef(false), pointerInside = useRef(false), needsLeave = useRef(false);
  const point = useRef<Point>({ x: 0, y: 0 }), timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const currentLabel = options.find(([mode]) => mode === value)?.[1] ?? '';
  function hideTooltip() { clearTimeout(timer.current); timer.current = undefined; setTip(null); }
  function dismiss(restoreFocus = false) {
    opened.current = false; needsLeave.current = pointerInside.current;
    hideTooltip(); onClose();
    if (restoreFocus) trigger.current?.focus({ preventScroll: true });
  }
  function showPopup() {
    if (disabled || !owner) return;
    hideTooltip(); needsLeave.current = true; opened.current = true; onOpen();
  }
  function enter(event: PointerEvent<HTMLButtonElement>) {
    if (event.pointerType === 'touch') return;
    pointerInside.current = true; point.current = { x: event.clientX, y: event.clientY };
    if (disabled || popupActive || opened.current || needsLeave.current || !currentLabel) return;
    timer.current = setTimeout(() => {
      timer.current = undefined;
      if (pointerInside.current && !opened.current && !needsLeave.current) setTip({ ...point.current });
    }, 350);
  }
  useEffect(() => () => clearTimeout(timer.current), []);
  useLayoutEffect(() => { dismiss(); }, [owner, disabled]);
  useLayoutEffect(() => {
    opened.current = open;
    // One Library owner suppresses both tooltips while either menu is open.
    hideTooltip(); needsLeave.current = pointerInside.current;
  }, [open, popupActive]);
  useLayoutEffect(() => {
    if (!open) { setAnchor(null); return; }
    const button = trigger.current!, menu = popup.current!;
    const inside = (target: EventTarget | null) => target instanceof Node && (button.contains(target) || menu.contains(target));
    const outside = (event: Event) => { if (!inside(event.target)) dismiss(); };
    const blur = () => dismiss();
    function escape(event: globalThis.KeyboardEvent) {
      if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); dismiss(true); }
      // Retire temporary interaction before an existing application shortcut;
      // do not consume or reimplement the command's semantics.
      else if (documentShortcut(event)) dismiss();
    }
    const measure = () => {
      const sidebar=button.closest<HTMLElement>('.session-library'),r=button.getBoundingClientRect();
      if(!sidebar){dismiss();return false;}
      const style=getComputedStyle(menu),arrow=getComputedStyle(menu,'::after');
      const arrowDepth=Math.max(0,-parseFloat(arrow.bottom)),arrowWidth=parseFloat(arrow.width);
      const paint=playbackModePaint(style.boxShadow,arrowDepth);
      const next=paint&&placePlaybackModePopup(r,sidebar.getBoundingClientRect(),menu.getBoundingClientRect(),
        {width:innerWidth,height:innerHeight},paint,arrowWidth,arrowDepth,12);
      if(!next){dismiss();return false;}
      // Preserve upward placement; X and the attached arrow use one CSS-client
      // sample. Paint (including bounded shadow) stays in Library-owned airspace.
      setAnchor(old=>JSON.stringify(old)===JSON.stringify(next)?old:next);
      return true;
    };
    if (!measure()) return;
    const observer = new ResizeObserver(measure); observer.observe(button);observer.observe(menu);
    const sidebar=button.closest('.session-library');if(sidebar)observer.observe(sidebar);
    window.addEventListener('resize', measure); window.addEventListener('scroll', measure, true);
    document.addEventListener('pointerdown', outside, true); document.addEventListener('contextmenu', outside, true);
    document.addEventListener('focusin', outside, true); window.addEventListener('blur', blur);
    window.addEventListener('keydown', escape, true);
    return () => {
      observer.disconnect(); window.removeEventListener('resize', measure); window.removeEventListener('scroll', measure, true);
      document.removeEventListener('pointerdown', outside, true); document.removeEventListener('contextmenu', outside, true);
      document.removeEventListener('focusin', outside, true); window.removeEventListener('blur', blur);
      window.removeEventListener('keydown', escape, true);
    };
  }, [open]);
  // Initial positioning is measured while non-painted. Focus only after that
  // layout commit makes the menu visible; later position updates do not steal
  // keyboard focus from the row the user has navigated to.
  useLayoutEffect(()=>{
    if(open&&anchor)popup.current?.querySelector<HTMLElement>('[aria-checked=true]')?.focus({preventScroll:true});
  },[open,Boolean(anchor)]);
  function navigate(event: KeyboardEvent<HTMLDivElement>) {
    if (event.key === 'Tab') { dismiss(true); return; }
    if (!['ArrowUp', 'ArrowDown', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    const items = [...event.currentTarget.querySelectorAll<HTMLButtonElement>('[role=menuitemradio]')];
    const index = items.indexOf(document.activeElement as HTMLButtonElement);
    const next = event.key === 'Home' ? 0 : event.key === 'End' ? items.length - 1
      : (index + (event.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length;
    items[next]?.focus();
  }
  return <>
    <button ref={trigger} type="button" className="playback-mode-trigger" data-playback-mode-trigger={scope}
      data-policy-owner={owner ?? ''} value={value} disabled={disabled} aria-label={t(label)}
      aria-haspopup="menu" aria-expanded={open} aria-controls={open ? popupId : undefined}
      aria-describedby={tip ? tooltipId : undefined} onPointerEnter={enter}
      onPointerMove={event => { point.current = { x: event.clientX, y: event.clientY }; if (tip) setTip({ ...point.current }); }}
      onPointerLeave={() => { pointerInside.current = false; needsLeave.current = false; hideTooltip(); }}
      onClick={event => { event.stopPropagation(); if (opened.current) dismiss(); else showPopup(); }}>
      <PlaybackModeIcon mode={value} />
    </button>
    {tip && !popupActive && createPortal(<div id={tooltipId} role="tooltip" data-playback-mode-tooltip
      className="playback-mode-tooltip" style={{ left: tip.x, top: tip.y - 12 }}>{t(currentLabel)}</div>, document.body)}
    {open && createPortal(<div ref={popup} id={popupId} role="menu" aria-label={t(label)} data-playback-mode-popup
      data-native-preview-overlay={previewOverlay}
      data-mode-scope={scope} className="playback-mode-popover" style={anchor?{
        left:anchor.left,top:anchor.top,'--playback-mode-arrow-x':anchor.arrowX+'px',
        clipPath:`inset(${-anchor.paint.top}px ${-anchor.paint.right}px ${-anchor.paint.bottom}px ${-anchor.paint.left}px)`,
      } as CSSProperties:{visibility:'hidden'}} onKeyDown={navigate}>
      {options.map(([mode, text]) => <button key={mode} type="button" role="menuitemradio" aria-checked={value === mode}
        tabIndex={-1} data-choice-value={mode} onClick={() => { if (mode !== (pendingValue?.() ?? value)) onValue(mode); dismiss(true); }}>
        <PlaybackModeIcon mode={mode} /><span className="playback-mode-label">{t(text)}</span>
        <span className="playback-mode-check" aria-hidden="true">{value === mode ? '✓' : ''}</span>
      </button>)}
    </div>, document.body)}
  </>;
}
