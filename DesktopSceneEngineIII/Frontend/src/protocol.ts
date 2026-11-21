export interface NativeSnapshot {
  role: 'launcher' | 'session';
  windowId: number;
  counter: number;
  revision: number;
  generation: number;
  pong?: string;
}

export interface SpikeEvidence {
  connected: boolean;
  role: NativeSnapshot['role'] | 'unknown';
  counter: number;
  revision: number;
  generation: number;
  pingCount: number;
  lastPong: string;
  eventsReceived: number;
}

export type SpikeCommand =
  | { type: 'spike.ready' | 'spike.openLauncher' | 'spike.invalid'; payload: Record<string, never> }
  | { type: 'spike.ping'; payload: { value: string } }
  | { type: 'preview.geometry'; payload: {
      x: number; y: number; width: number; height: number; visible: boolean;
      generation: number; devicePixelRatio: number; viewportWidth: number; viewportHeight: number;
      overlayActive: boolean; mitigateOverlay: boolean;
    } };

interface NativeResult {
  kind: 'result';
  version: 1;
  requestId: string;
  status: 'completed' | 'failed';
  payload?: NativeSnapshot;
  error?: string;
}

interface CounterEvent {
  kind: 'event';
  version: 1;
  type: 'spike.counterChanged';
  revision: number;
  payload: { value: number; generation: number };
}

interface NativeWebView {
  postMessage(message: unknown): void;
  addEventListener(type: 'message', callback: (event: MessageEvent<unknown>) => void): void;
  removeEventListener(type: 'message', callback: (event: MessageEvent<unknown>) => void): void;
}

declare global {
  interface Window {
    chrome?: { webview?: NativeWebView };
    __spikeEvidence?: () => Readonly<SpikeEvidence>;
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isNatural(value: unknown): value is number {
  return typeof value === 'number' && Number.isSafeInteger(value) && value >= 0;
}

function isSnapshot(value: unknown): value is NativeSnapshot {
  return isRecord(value)
    && (value.role === 'launcher' || value.role === 'session')
    && isNatural(value.windowId) && isNatural(value.counter)
    && isNatural(value.revision) && isNatural(value.generation)
    && (value.pong === undefined || typeof value.pong === 'string');
}

export function isNativeResult(value: unknown): value is NativeResult {
  return isRecord(value) && value.kind === 'result' && value.version === 1
    && typeof value.requestId === 'string'
    && (value.status === 'completed' || value.status === 'failed')
    && (value.payload === undefined || isSnapshot(value.payload))
    && (value.error === undefined || typeof value.error === 'string');
}

export function isCounterEvent(value: unknown): value is CounterEvent {
  return isRecord(value) && value.kind === 'event' && value.version === 1
    && value.type === 'spike.counterChanged' && isNatural(value.revision)
    && isRecord(value.payload) && isNatural(value.payload.value) && isNatural(value.payload.generation);
}
