import {useLayoutEffect, useRef, useState, type ReactNode} from 'react';
import {
  captionActions, productWindowContext, readProductChromeState,
  type CaptionAction, type ProductChromeState,
} from './productWindowHost.ts';
import './productTitleBar.css';

const modalRegions = {drag: null, minimize: null, maximize: null, close: null};

function rect(element: HTMLElement) {
  const {x, y, width, height} = element.getBoundingClientRect();
  return {x, y, width, height};
}

function outsideModal(header: HTMLElement | null): boolean {
  return Array.from(document.querySelectorAll('dialog:modal')).some(dialog => !header?.contains(dialog));
}

function containsOpenDialog(node: Node): boolean {
  return node instanceof Element && (node.matches('dialog[open]') || node.querySelector('dialog[open]') !== null);
}

function CaptionGlyph({action, maximized}: {action: CaptionAction; maximized: boolean}) {
  return <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true">
    {action === 'minimize' && <path d="M3 8.5h10" />}
    {action === 'maximize' && (maximized
      ? <><path d="M5.5 5.5v-3H13v7.5h-3" /><path d="M3 6h7v7H3z" /></>
      : <path d="M3.5 3.5h9v9h-9z" />)}
    {action === 'close' && <path d="m3.5 3.5 9 9m0-9-9 9" />}
  </svg>;
}

export function ProductTitleBar({beforeDrag, afterDrag, beforeCaption, text}: {
  beforeDrag?: ReactNode;
  afterDrag?: ReactNode;
  beforeCaption?: ReactNode;
  text: (message: string) => string;
}) {
  const context = productWindowContext();
  const bridge = context?.bridge;
  const generation = context?.generation;
  const header = useRef<HTMLElement>(null);
  const drag = useRef<HTMLSpanElement>(null);
  const minimize = useRef<HTMLButtonElement>(null);
  const maximize = useRef<HTMLButtonElement>(null);
  const close = useRef<HTMLButtonElement>(null);
  const [state, setState] = useState<ProductChromeState | null>(null);

  useLayoutEffect(() => {
    const titleBar = header.current;
    const dragRegion = drag.current;
    const minButton = minimize.current, maxButton = maximize.current, closeButton = close.current;
    if (!bridge || !generation || !titleBar || !dragRegion || !minButton || !maxButton || !closeButton) return;
    const receive = ({data}: {data: unknown}) => {
      const next = readProductChromeState(data, generation);
      if (next) setState(next);
    };
    let previous = '';
    let frame = 0;
    let resolution: MediaQueryList;

    const publish = () => {
      const viewport = {width: window.innerWidth, height: window.innerHeight, dpr: window.devicePixelRatio};
      let regions: Record<'drag' | CaptionAction, ReturnType<typeof rect> | null> = modalRegions;
      if (!outsideModal(titleBar)) {
        const dragBounds = rect(dragRegion);
        regions = {
          drag: dragBounds.width > 0 && dragBounds.height > 0 ? dragBounds : null,
          minimize: rect(minButton), maximize: rect(maxButton), close: rect(closeButton),
        };
      }
      const sample = JSON.stringify({viewport, regions});
      if (sample === previous) return;
      bridge.postMessage({kind: 'shell-chrome-geometry', version: 1, generation, viewport, regions});
      previous = sample;
    };
    function schedule() {
      if (!frame) frame = requestAnimationFrame(() => { frame = 0; publish(); });
    }
    function watchResolution() {
      resolution?.removeEventListener('change', onResolutionChanged);
      resolution = matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`);
      resolution.addEventListener('change', onResolutionChanged);
    }
    function onResolutionChanged() {
      watchResolution();
      schedule();
    }

    bridge.addEventListener('message', receive);
    const observer = new ResizeObserver(schedule);
    for (const element of [titleBar, dragRegion, minButton, maxButton, closeButton]) observer.observe(element);
    const modalObserver = new MutationObserver(records => {
      const changed = records.some(record => record.type === 'attributes'
        ? record.target instanceof HTMLDialogElement
        : Array.from(record.addedNodes).some(containsOpenDialog) || Array.from(record.removedNodes).some(containsOpenDialog));
      if (changed) publish();
    });
    modalObserver.observe(document.body, {attributes: true, attributeFilter: ['open'], childList: true, subtree: true});
    window.addEventListener('resize', schedule);
    window.addEventListener('scroll', schedule, true);
    watchResolution();
    publish();
    return () => {
      if (frame) cancelAnimationFrame(frame);
      observer.disconnect();
      modalObserver.disconnect();
      bridge.removeEventListener('message', receive);
      window.removeEventListener('resize', schedule);
      window.removeEventListener('scroll', schedule, true);
      resolution.removeEventListener('change', onResolutionChanged);
    };
  }, [bridge, generation]);

  const labels = {minimize: 'Minimize', maximize: state?.maximized ? 'Restore' : 'Maximize', close: 'Close'};
  const buttonRefs = {minimize, maximize, close};

  const buttons = context && <div className="product-caption-controls" role="group" aria-label={text('Window controls')}>
      {captionActions.map(action => <button key={action} ref={buttonRefs[action]}
        type="button" className={`window-control window-control--${action}`}
        aria-label={text(labels[action])} data-chrome-action={action}
        data-chrome-hovered={state?.hovered === action} data-chrome-pressed={state?.pressed === action}
        onClick={event => {
          // Win32 owns physical non-client clicks. Keyboard and assistive technology invoke the button here.
          if (event.detail === 0 && !outsideModal(header.current)) context.bridge.postMessage({
            kind: 'shell-chrome-command', version: 1, generation: context.generation, action,
          });
        }}>
        <CaptionGlyph action={action} maximized={state?.maximized === true} />
      </button>)}
    </div>;
  return <header ref={header} className="title-bar" data-product-chrome={context ? '' : undefined}
    data-chrome-active={state?.active} data-chrome-maximized={state?.maximized} data-chrome-minimized={state?.minimized}>
    {beforeDrag}
    <span ref={drag} className="title-bar-space" aria-hidden="true" data-chrome-drag={context ? '' : undefined} />
    {afterDrag}
    {buttons && beforeCaption ? <div className="window-controls">{beforeCaption}{buttons}</div> : <>{beforeCaption}{buttons}</>}
  </header>;
}
