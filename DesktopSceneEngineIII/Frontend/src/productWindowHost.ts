import {useLayoutEffect, useRef, useState} from 'react';
import {sessionUuid} from './ProductionSessionIdentity.ts';

type ProductWindow = Window & {
  __dseProductShell?: boolean;
  chrome?: {webview?: ProductWindowBridge};
};

type ProductWindowBridge = {
  postMessage(message: unknown): void;
  addEventListener(type: 'message', listener: (message: {data: unknown}) => void): void;
  removeEventListener(type: 'message', listener: (message: {data: unknown}) => void): void;
};

export const captionActions = ['minimize', 'maximize', 'close'] as const;
export type CaptionAction = typeof captionActions[number];

export type ProductChromeState = {
  active: boolean;
  maximized: boolean;
  minimized: boolean;
  hovered: CaptionAction | null;
  pressed: CaptionAction | null;
};

export function productWindowContext() {
  if (typeof window === 'undefined' || (window as ProductWindow).__dseProductShell !== true) return null;
  const bridge = (window as ProductWindow).chrome?.webview;
  const generation = new URLSearchParams(window.location.search).get('generation');
  return bridge && sessionUuid(generation) && generation !== '00000000-0000-0000-0000-000000000000'
    ? {bridge, generation} : null;
}

function record(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function captionValue(value: unknown): value is CaptionAction | null {
  return value === null || captionActions.some(action => action === value);
}

export function readProductChromeState(message: unknown, generation: string): ProductChromeState | null {
  if (!record(message) || message.kind !== 'shell-chrome-state' || message.version !== 1
      || message.generation !== generation || Object.keys(message).length !== 4 || !record(message.state)) return null;
  const state = message.state;
  const {active, maximized, minimized, hovered, pressed} = state;
  if (Object.keys(state).length !== 5 || typeof active !== 'boolean' || typeof maximized !== 'boolean'
      || typeof minimized !== 'boolean' || !captionValue(hovered) || !captionValue(pressed)) return null;
  return {active, maximized, minimized, hovered, pressed};
}

// Report that React committed the current document's useful presentation.
// This event carries no authority over native window or application state.
export function useProductPresentation(state: 'loading' | 'ready' | 'failed') {
  const previous = useRef('');
  useLayoutEffect(() => {
    const context = productWindowContext();
    if (!context || state === 'loading' || previous.current === state) return;
    context.bridge.postMessage({kind: 'shell-presentation', version: 1, generation: context.generation, state});
    previous.current = state;
  }, [state]);
}

export function useSessionHide(sessionIncarnation: string | null) {
  const context = productWindowContext();
  const bridge = context?.bridge;
  const generation = context?.generation;
  const [eligible, setEligible] = useState(false);

  useLayoutEffect(() => {
    setEligible(false);
    if (!bridge || !generation || !sessionIncarnation) return;
    const receive = ({data}: {data: unknown}) => {
      if (!record(data) || data.kind !== 'session-hide-state' || data.version !== 1
          || data.generation !== generation || data.sessionIncarnation !== sessionIncarnation
          || typeof data.eligible !== 'boolean' || Object.keys(data).length !== 5) return;
      setEligible(data.eligible);
    };
    bridge.addEventListener('message', receive);
    return () => bridge.removeEventListener('message', receive);
  }, [bridge, generation, sessionIncarnation]);

  if (!eligible || !bridge || !generation || !sessionIncarnation) return undefined;
  return () => {
    if (document.querySelector('dialog:modal')) return;
    bridge.postMessage({kind: 'session-hide-request', version: 1, generation, sessionIncarnation});
  };
}
