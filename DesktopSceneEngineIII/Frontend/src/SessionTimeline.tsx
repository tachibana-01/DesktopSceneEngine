import { useEffect, useId, useLayoutEffect, useMemo, useReducer, useRef, useState } from 'react';
import type { CSSProperties, PointerEvent as ReactPointerEvent } from 'react';
import { findItem } from './sessionEntities.ts';
import { formatRuntimeTime } from './sessionPlayback.ts';
import { beginTimelineDrag, moveTimelineDrag, timelineDragTargetsCurrentScene, timelineFacts, timeForValue, valueForGroovePosition } from './sessionTimelineModel.ts';
import type { TimelineDrag } from './sessionTimelineModel.ts';
import { TimelinePreviewMock } from './sessionTimelinePreviewMock.ts';
import { createHoverMockFrame, resolveHoverComposition } from './sessionHoverComposition.ts';
import type { ThumbnailMockMode } from './sessionTimelinePreviewMock.ts';
import type { useSessionPrototype } from './useSessionPrototype';
import { useSessionText } from './sessionLanguage';
import { useTimelineHoverOverlay } from './useTimelineHoverOverlay.ts';
import { usePointerDragCapture } from './usePointerDragCapture.ts';
import './sessionTimeline.css';
import { SessionProgressRow } from './SessionProgressRow';

export type TimelineDevAction = 'advance74' | 'advance1' | 'advance75' | 'resolve-latest' | 'resolve-oldest' | 'invalidate' | 'hide' | '9999' | '10000' | 'external-next' | 'duration' | 'complete';
export function timelineDevAction(action: TimelineDevAction) {
  window.dispatchEvent(new CustomEvent('dse-timeline-dev', { detail: action }));
}

export function SessionTimeline({ mock }: { mock: ReturnType<typeof useSessionPrototype> }) {
  const t = useSessionText();
  const [, render] = useReducer(value => value + 1, 0);
  const [dragValue, setDragValue] = useState<TimelineDrag | null>(null);
  const [keyboardValue, setKeyboardValue] = useState<number | null>(null);
  const drag = useRef<TimelineDrag | null>(null);
  const capture = usePointerDragCapture<HTMLDivElement>();
  const staleTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const control = useRef<HTMLDivElement>(null), groove = useRef<HTMLSpanElement>(null);
  const [hoverX, setHoverX] = useState(0);
  const sourceClipId = useId() + '-timeline-source';
  const facts = timelineFacts(mock.state);
  const active = facts.enabled ? findItem(mock.state, facts.itemId)?.item : undefined;
  const bound = mock.state.runtime?.items.find(item => item.id === facts.itemId);
  const mode: ThumbnailMockMode = mock.scenario === 'hover-unavailable' ? 'unavailable' : mock.scenario === 'hover-loading' ? 'loading'
    : mock.scenario === 'hover-out-of-order' ? 'out-of-order' : 'normal';
  const manual = new URLSearchParams(window.location.search).get('timelineClock') === 'manual';
  const driver = useMemo(() => new TimelinePreviewMock(mode, manual, render), [mode, manual, mock.scenario]);
  const sourceKey = active ? JSON.stringify([active.id, active.source, active.kind]) : '';
  const compositionKey = JSON.stringify([bound?.presentation, mock.state.project.canvasAspect]);
  const latest = useRef(mock); latest.current = mock;

  function cancelDrag() {
    clearTimeout(staleTimer.current); staleTimer.current = undefined;
    drag.current = null; capture.end(); setDragValue(null);
  }
  useLayoutEffect(() => { driver.invalidate(); }, [driver, sourceKey, mock.timelineEpoch, mock.state.timelineInvalidation]);
  useLayoutEffect(() => {
    if (!facts.enabled) driver.invalidate();
    if (!timelineDragTargetsCurrentScene(mock.state, drag.current)) cancelDrag();
    setKeyboardValue(null);
  }, [driver, facts.enabled, facts.itemId, facts.duration, facts.time]);
  useLayoutEffect(() => { cancelDrag(); }, [mock.timelineDragEpoch, mock.state.timelineDragCancellation]);
  useLayoutEffect(() => {
    driver.compose({ key: compositionKey, canvasNumerator: mock.state.project.canvasAspect[0], canvasDenominator: mock.state.project.canvasAspect[1] });
  }, [driver, compositionKey]);
  useEffect(() => {
    const hidden = () => { driver.hide(); cancelDrag(); setKeyboardValue(null); };
    const visibility = () => { if (document.hidden) hidden(); };
    window.addEventListener('blur', hidden); window.addEventListener('pagehide', hidden); document.addEventListener('visibilitychange', visibility);
    return () => { window.removeEventListener('blur', hidden); window.removeEventListener('pagehide', hidden); document.removeEventListener('visibilitychange', visibility); cancelDrag(); driver.dispose(); };
  }, [driver]);
  useEffect(() => {
    if (!import.meta.env.DEV) return;
    function dev(event: Event) {
      const action = (event as CustomEvent<TimelineDevAction>).detail;
      if (action === 'advance74') driver.advance(74);
      else if (action === 'advance1') driver.advance(1);
      else if (action === 'advance75') driver.advance(75);
      else if (action === 'resolve-latest') driver.resolve('latest');
      else if (action === 'resolve-oldest') driver.resolve('oldest');
      else if (action === 'invalidate') driver.invalidate();
      else if (action === 'hide') driver.hide();
      else if (action === '9999' || action === '10000') {
        const snapshot = beginTimelineDrag(latest.current.state);
        if (snapshot) { driver.hide(); latest.current.timelineRelease(moveTimelineDrag(snapshot, Number(action))); }
      } else if (action === 'external-next') latest.current.playback({ type: 'next' });
      else if (action === 'complete') latest.current.playback({ type: 'natural-completion' });
      else if (action === 'duration') latest.current.injectTimelineDuration();
    }
    window.addEventListener('dse-timeline-dev', dev);
    return () => window.removeEventListener('dse-timeline-dev', dev);
  }, [driver]);

  function logicalValue(clientX: number) {
    const rect = groove.current!.getBoundingClientRect();
    return valueForGroovePosition(clientX, rect.left, rect.width);
  }
  function press(event: ReactPointerEvent<HTMLDivElement>) {
    if (!facts.enabled) return;
    const snapshot = beginTimelineDrag(latest.current.state);
    if (!snapshot || !capture.start(event)) return;
    event.preventDefault(); event.currentTarget.focus(); driver.hide(); setKeyboardValue(null);
    drag.current = moveTimelineDrag(snapshot, logicalValue(event.clientX)); setDragValue(drag.current);
    // DEV scenarios inject an external runtime fact while the pointer remains down.
    if (mock.scenario === 'drag-stale-item' || mock.scenario === 'drag-stale-duration' || mock.scenario === 'drag-natural-completion') {
      const action = mock.scenario === 'drag-stale-duration' ? 'duration' : mock.scenario === 'drag-natural-completion' ? 'complete' : 'external-next';
      clearTimeout(staleTimer.current); staleTimer.current = setTimeout(() => timelineDevAction(action), 500);
    }
  }
  function move(event: ReactPointerEvent<HTMLDivElement>) {
    if (drag.current) {
      if (!capture.owns(event)) return;
      if (event.pointerType === 'mouse' && !(event.buttons & 1)) { cancelDrag(); return; }
      if (!timelineDragTargetsCurrentScene(latest.current.state, drag.current)) { cancelDrag(); return; }
      drag.current = moveTimelineDrag(drag.current, logicalValue(event.clientX)); setDragValue(drag.current); return;
    }
    if (event.buttons || !facts.enabled || !active) { driver.hide(); return; }
    const value = logicalValue(event.clientX);
    setHoverX(event.clientX);
    driver.hover({ itemId: active.id, source: active.source, mediaType: active.kind, duration: facts.duration }, timeForValue(value, facts.duration));
  }
  function release(event: ReactPointerEvent<HTMLDivElement>) {
    if (!capture.owns(event)) return;
    const snapshot = drag.current;
    cancelDrag();
    if (snapshot) latest.current.timelineRelease(snapshot);
  }
  function cancelPointer(event: ReactPointerEvent<HTMLDivElement>) {
    if (!capture.owns(event)) return;
    cancelDrag(); driver.hide();
  }
  const hover = driver.model;
  const value = dragValue?.value ?? keyboardValue ?? facts.value;
  const displayedTime = dragValue ? timeForValue(dragValue.value, dragValue.duration) : facts.time;
  const frame = hover.displayed;
  const canvas = hover.canvasSize;
  const sourceFrame = frame ? createHoverMockFrame(frame.request.source, frame.imageId) : null;
  const composition = sourceFrame && bound ? resolveHoverComposition(sourceFrame.width, sourceFrame.height, canvas.width, canvas.height, bound.presentation) : null;
  const destination = composition ? { x: composition.destination.x * canvas.width, y: composition.destination.y * canvas.height,
    width: composition.destination.width * canvas.width, height: composition.destination.height * canvas.height } : null;
  const sourceRect = composition && sourceFrame ? { x: composition.sourceUv.x * sourceFrame.width, y: composition.sourceUv.y * sourceFrame.height,
    width: composition.sourceUv.width * sourceFrame.width, height: composition.sourceUv.height * sourceFrame.height } : null;
  const sourceScaleX = destination && sourceRect ? destination.width / sourceRect.width : 1;
  const sourceScaleY = destination && sourceRect ? destination.height / sourceRect.height : 1;
  const overlay = useTimelineHoverOverlay(hover.active, hoverX, mock.state.project.canvasAspect, groove);
  // A fixed sibling of Timeline, outside its grid row. Keep the logical F06
  // Canvas/SVG mapping above intact; only the CSS footprint is downscaled.
  const hoverPopup = hover.active ? <div className="timeline-hover-overlay" data-placement-ready={Boolean(overlay)}
    style={{ '--thumbnail-width': (overlay?.contentWidth ?? canvas.width) + 'px', '--thumbnail-height': (overlay?.contentHeight ?? canvas.height) + 'px' } as CSSProperties}>
      <div className="timeline-hover-popup" role="status" aria-label={t('Timeline hover preview')}
        style={overlay ? { left: overlay.left, top: overlay.top, width: overlay.width, height: overlay.height } : undefined}>
        <div className="timeline-thumbnail" data-state={hover.status}>
          {hover.status === 'valid' && frame && sourceFrame && composition && destination && sourceRect ? <svg viewBox={'0 0 ' + canvas.width + ' ' + canvas.height} role="img" style={{ overflow: 'hidden' }} aria-label={t('Mock thumbnail') + ' · ' + frame.request.source.itemId + ' · ' + t('bucket') + ' ' + (frame.request.representativeTimestampMilliseconds ?? t('image')) + ' · ' + t('composition') + ' ' + hover.presentationGeneration}>
            <rect width={canvas.width} height={canvas.height} fill="#1a1a1a" />
            <defs><clipPath id={sourceClipId} clipPathUnits="userSpaceOnUse"><rect data-hover-destination="true" {...destination} /></clipPath></defs>
            <g clipPath={'url(#' + sourceClipId + ')'}>
              <g data-hover-source-frame="true" data-source-width={sourceFrame.width} data-source-height={sourceFrame.height} data-source-identity={sourceFrame.identity}
                transform={'matrix(' + sourceScaleX + ' 0 0 ' + sourceScaleY + ' ' + (destination.x - sourceRect.x * sourceScaleX) + ' ' + (destination.y - sourceRect.y * sourceScaleY) + ')'}>
                <title>{sourceFrame.sourceId + ' · ' + sourceFrame.sourcePath + ' · ' + sourceFrame.width + '×' + sourceFrame.height}</title>
                <rect width={sourceFrame.width} height={sourceFrame.height} fill={'hsl(' + sourceFrame.seed % 360 + ' 20% 27%)'} />
                <rect width={sourceFrame.width * .37} height={sourceFrame.height * .43} fill="#914844" />
                <rect x={sourceFrame.width * .37} width={sourceFrame.width * .63} height={sourceFrame.height * .43} fill="#a48344" />
                <rect y={sourceFrame.height * .43} width={sourceFrame.width * .61} height={sourceFrame.height * .57} fill="#416c94" />
                <rect x={sourceFrame.width * .61} y={sourceFrame.height * .43} width={sourceFrame.width * .39} height={sourceFrame.height * .57} fill="#487c67" />
                <rect data-source-marker="left" width={sourceFrame.width * .04} height={sourceFrame.height} fill="#d76862" />
                <rect data-source-marker="right" x={sourceFrame.width * .94} width={sourceFrame.width * .06} height={sourceFrame.height} fill="#65a7d1" />
                <rect data-source-marker="top" width={sourceFrame.width} height={sourceFrame.height * .05} fill="#dfbd67" />
                <rect data-source-marker="bottom" y={sourceFrame.height * .92} width={sourceFrame.width} height={sourceFrame.height * .08} fill="#69b795" />
                <path data-source-marker="center" d={'M' + sourceFrame.width * .5 + ' ' + sourceFrame.height * .32 + 'V' + sourceFrame.height * .68 + 'M' + sourceFrame.width * .4 + ' ' + sourceFrame.height * .5 + 'H' + sourceFrame.width * .6} stroke="#f1eee4" strokeWidth="2" fill="none" />
                <circle cx={sourceFrame.width * .72} cy={sourceFrame.height * .26} r={sourceFrame.height * .08} fill={'hsl(' + sourceFrame.seed % 360 + ' 35% 65%)'} />
                <text x={sourceFrame.width * .16} y={sourceFrame.height * .72}>{sourceFrame.sourceId}</text>
                <text x={sourceFrame.width * .16} y={sourceFrame.height * .82}>{frame.request.source.mediaType === 'image' ? t('STATIC MOCK') : t('MOCK') + ' · ' + frame.request.representativeTimestampMilliseconds + ' ms'}</text>
              </g>
            </g>
          </svg> : <span>{t(hover.status === 'loading' ? 'Loading preview…' : 'Preview unavailable')}</span>}
        </div>
        <div className="timeline-hover-time">{formatRuntimeTime(hover.rawTimeSeconds ?? 0)}</div>
      </div>
    </div> : null;
  return <><section className={'session-timeline' + (hover.active ? ' is-hovering' : '')} aria-label={t('Active runtime timeline')}
    data-enabled={facts.enabled} data-item-id={facts.itemId ?? ''} data-runtime-time={facts.time} data-preview-time={displayedTime}
    data-duration={facts.duration} data-dragging={Boolean(dragValue)} data-hover-active={hover.active} data-hover-status={hover.status}
    data-hover-source={hover.source?.source ?? ''} data-hover-item={hover.source?.itemId ?? ''} data-generation={hover.generation}
    data-serial={hover.serial} data-submissions={driver.submissions} data-image-reads={driver.imageReads} data-stale-rejected={hover.staleResultsRejected}
    data-frame-id={frame?.imageId ?? ''} data-frame-bucket={frame?.request.representativeTimestampMilliseconds ?? ''}
    data-presentation-generation={hover.presentationGeneration}>
    <SessionProgressRow value={value} time={displayedTime} duration={facts.duration} enabled={facts.enabled} preview={Boolean(dragValue)} controlRef={control} grooveRef={groove}
      controlProps={{ onPointerDown:press,onPointerMove:move,onPointerUp:release,
        onPointerCancel:cancelPointer,onLostPointerCapture:cancelPointer,onPointerLeave:()=>driver.hide(),
        onBlur:()=>setKeyboardValue(null),onKeyDown:event=>{
          if(event.key==='Escape'&&drag.current){
            event.preventDefault();cancelDrag();driver.hide();setKeyboardValue(null);return;
          }
          const increments:Record<string,number>={ArrowLeft:-1,ArrowDown:-1,ArrowRight:1,ArrowUp:1,PageDown:-10,PageUp:10};
          if(event.key in increments||event.key==='Home'||event.key==='End'){
            event.preventDefault();setKeyboardValue(event.key==='Home'?0:event.key==='End'?10000:Math.min(10000,Math.max(0,value+increments[event.key])));
          }
        }}} />
  </section>{hoverPopup}</>;
}
