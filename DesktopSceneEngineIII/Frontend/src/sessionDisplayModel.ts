/** Value-only projection of DisplaySelectionState and MainWindow's desktop endpoint.
 * These are deterministic fixture facts, never real display/host/Engine handles.
 * Authored Project, selected objects, playback and preferences deliberately do not
 * belong to this model. Their owners can retain this state across document changes.
 */
import type { SessionLifecycle } from './sessionLifecycleModel.ts';

export interface DisplayDescriptor {
  id: string;
  displayNumber: number;
  friendlyName: string;
  pixelWidth: number;
  pixelHeight: number;
  primary: boolean;
}

export interface DisplayEnumeration {
  status: 'success' | 'api-failure' | 'incomplete-path-metadata' | 'duplicate-identity';
  displays: DisplayDescriptor[];
}

export interface DisplayFaults {
  hostCreate: 'ready' | 'display-unavailable' | 'transiently-unavailable' | 'unsupported-shell-topology' | 'native-failure';
  attachment: 'ready' | 'transiently-unavailable' | 'replacement-required' | 'structurally-unsupported' | 'native-failure';
  attach: boolean;
  pacing: boolean;
  detach: boolean;
  destroy: boolean;
}

export type DisplayIdentifyStatus = 'shown' | 'invalid-request' | 'unresolved' | 'api-failure';
export type DisplayDeploymentResult = 'completed' | 'already-satisfied' | 'rejected' | 'failure';

export interface DisplaySessionState {
  readonly boundId: string;
  enumeration: DisplayEnumeration;
  engineReady: boolean;
  replacementInProgress: boolean;
  desired: boolean;
  targetRegistered: boolean;
  /** Equivalent to the live owned nativeHandle, independently of graphics ID. */
  hostOwned: boolean;
  /** Attachment health is separate from retained ownership. */
  hostHealthy: boolean;
  continuationPending: boolean;
  faults: DisplayFaults;
  // DEV diagnostics only: production failures log to stderr, not a new toast.
  lastResult: DisplayDeploymentResult | null;
  identifyStatus: DisplayIdentifyStatus | null;
  identifyDurationMilliseconds: number;
  lastIdentifyRequestId: string | null;
  lastDeploymentRequestId: string | null;
  counters: {
    enumerations: number;
    identifyRequests: number;
    deployRequests: number;
    undeployRequests: number;
    hostCreateAttempts: number;
    detachAttempts: number;
    destroyAttempts: number;
  };
}

export type DisplayAction =
  | { type: 'enumerate'; result: DisplayEnumeration }
  | { type: 'native-event'; result?: DisplayEnumeration }
  | { type: 'identify'; status?: DisplayIdentifyStatus; refresh?: DisplayEnumeration }
  | { type: 'deploy' | 'undeploy' | 'toggle-deployment'; refresh?: DisplayEnumeration }
  | { type: 'host-lost' }
  | { type: 'continue-reconciliation' }
  | { type: 'capability'; engineReady?: boolean; replacementInProgress?: boolean }
  | { type: 'faults'; faults: Partial<DisplayFaults> };

export function createSessionDisplay(boundId = 'mock-persistent-display-a'): DisplaySessionState {
  return {
    boundId,
    enumeration: { status: 'success', displays: [
      { id: 'mock-persistent-display-a', displayNumber: 1, friendlyName: 'Studio Display', pixelWidth: 2560, pixelHeight: 1440, primary: true },
      { id: 'mock-persistent-display-b', displayNumber: 2, friendlyName: 'Portrait Studio', pixelWidth: 2560, pixelHeight: 1600, primary: false },
    ] },
    engineReady: true, replacementInProgress: false,
    desired: false, targetRegistered: false, hostOwned: false, hostHealthy: false, continuationPending: false,
    faults: { hostCreate: 'ready', attachment: 'ready', attach: false, pacing: false, detach: false, destroy: false },
    lastResult: null, identifyStatus: null, identifyDurationMilliseconds: 2500,
    lastIdentifyRequestId: null, lastDeploymentRequestId: null,
    counters: { enumerations: 0, identifyRequests: 0, deployRequests: 0, undeployRequests: 0,
      hostCreateAttempts: 0, detachAttempts: 0, destroyAttempts: 0 },
  };
}

function boundDescriptor(state: DisplaySessionState): DisplayDescriptor | null {
  if (!state.boundId || state.enumeration.status !== 'success') return null;
  const matches = state.enumeration.displays.filter(display => display.id === state.boundId);
  // DisplaySnapshot::resolve does not resolve duplicate identities or fallback.
  return matches.length === 1 ? matches[0]! : null;
}

export function displayFacts(state: DisplaySessionState, lifecycle: SessionLifecycle = { closePending: false, ending: false }) {
  const descriptor = boundDescriptor(state);
  const availability = state.enumeration.status !== 'success'
    ? 'enumeration-failed' : descriptor ? 'available' : 'unavailable';
  const label = availability === 'enumeration-failed' ? 'Unable to enumerate displays'
    : !descriptor ? 'Selected display unavailable'
      : `Display ${descriptor.displayNumber}${descriptor.friendlyName ? ` — ${descriptor.friendlyName}` : ''} — ${descriptor.pixelWidth}×${descriptor.pixelHeight}${descriptor.primary ? ' (Primary)' : ''}`;
  const retained = state.desired || state.targetRegistered || state.hostOwned;
  const ready = state.engineReady && !state.replacementInProgress && !lifecycle.closePending && !lifecycle.ending;
  // desktopDeploymentProjection exposes both capabilities. The local button
  // chooses only the capability corresponding to retained ownership.
  const canDeploy = ready && (retained || descriptor !== null);
  const canUndeploy = ready;
  // Cached enumeration alone does not detach or invalidate an existing endpoint.
  // This is last reconciled attachment health; availability is a separate fact.
  const healthy = state.desired && state.targetRegistered && state.hostOwned && state.hostHealthy;
  const presentation = !retained ? 'preview-only' : healthy ? 'deployed' : 'cleanup-required';
  return {
    boundId: state.boundId, descriptor, availability, label, tooltip: label,
    canIdentify: descriptor !== null, retained, healthy, presentation,
    deploymentLabel: retained ? 'Undeploy' : 'Deploy',
    deploymentEnabled: retained ? canUndeploy : canDeploy,
    canDeploy, canUndeploy,
  } as const;
}

function refresh(state: DisplaySessionState, result?: DisplayEnumeration) {
  state.counters.enumerations += 1;
  if (result) state.enumeration = structuredClone(result);
}

/** Mirrors removeDesktopPresentation: never destroy a borrowed host after a
 * failed detach. Destroy failure retains only the host after graphics released. */
function removePresentation(state: DisplaySessionState): boolean {
  if (state.targetRegistered) {
    state.counters.detachAttempts += 1;
    if (state.faults.detach) return false;
    state.targetRegistered = false;
  }
  state.counters.destroyAttempts += 1;
  if (state.hostOwned && state.faults.destroy) { state.hostHealthy = false; return false; }
  state.hostOwned = false;
  state.hostHealthy = false;
  return true;
}

type Attempt = 'ready' | 'transiently-unavailable' | 'failed';

function tryDeploy(state: DisplaySessionState): Attempt {
  state.counters.hostCreateAttempts += 1;
  // Platform host creation independently re-resolves the same bound identity.
  const status = boundDescriptor(state) ? state.faults.hostCreate : 'display-unavailable';
  if (status !== 'ready') return status === 'display-unavailable' || status === 'transiently-unavailable'
    ? 'transiently-unavailable' : 'failed';
  state.hostOwned = true;
  state.hostHealthy = true;
  if (state.faults.attach) { removePresentation(state); return 'failed'; }
  state.targetRegistered = true;
  if (state.faults.pacing) { removePresentation(state); return 'failed'; }
  return 'ready';
}

/** One delivered control-queue pass. Native message coalescing and timer delivery
 * are outside this pure model; continue-reconciliation injects the one authorized
 * finite continuation explicitly, rather than installing prototype timers. */
function reconcile(state: DisplaySessionState, allowContinuation: boolean, result?: DisplayEnumeration): boolean {
  if (!state.engineReady || state.replacementInProgress) return false;
  refresh(state, result);
  let deployed = false;
  let cleanupSucceeded = true;
  let attempt: Attempt = 'failed';
  if (!state.desired) {
    cleanupSucceeded = removePresentation(state);
  } else if (state.targetRegistered && state.hostOwned) {
    const attachment = boundDescriptor(state) ? state.faults.attachment : 'transiently-unavailable';
    if (attachment === 'ready') {
      deployed = true; state.hostHealthy = true; attempt = 'ready';
    } else {
      state.hostHealthy = false;
      cleanupSucceeded = removePresentation(state);
    }
  } else if (state.targetRegistered || state.hostOwned) {
    cleanupSucceeded = removePresentation(state);
  }
  if (state.desired && cleanupSucceeded && !deployed) {
    attempt = tryDeploy(state);
    deployed = attempt === 'ready';
  }
  if (!deployed && attempt === 'transiently-unavailable' && allowContinuation && state.desired) {
    state.continuationPending = true;
  }
  return state.desired ? deployed : cleanupSucceeded;
}

function requestDeployment(state: DisplaySessionState, intent: 'deploy' | 'undeploy', lifecycle: SessionLifecycle, result?: DisplayEnumeration) {
  if (intent === 'deploy') state.counters.deployRequests += 1;
  else state.counters.undeployRequests += 1;
  const projection = displayFacts(state, lifecycle);
  if (!state.engineReady || state.replacementInProgress || lifecycle.closePending || lifecycle.ending) {
    state.lastResult = 'rejected'; return;
  }
  if ((intent === 'deploy') === projection.retained) { state.lastResult = 'already-satisfied'; return; }
  state.lastDeploymentRequestId = state.boundId;
  let completed: boolean;
  if (intent === 'undeploy') {
    state.desired = false; // Cancellation precedes detach/destroy and survives failure.
    state.continuationPending = false;
    completed = removePresentation(state);
  } else {
    refresh(state, result); // MainWindow refreshes before checking fresh exact-bound capability.
    if (!boundDescriptor(state)) { state.lastResult = 'rejected'; return; }
    state.desired = true;
    completed = reconcile(state, false);
    if (!completed) state.desired = false; // Initial failure has no retry authority.
  }
  state.lastResult = completed ? 'completed' : 'failure';
}

export function displayTransition(state: DisplaySessionState, action: DisplayAction, lifecycle: SessionLifecycle = { closePending: false, ending: false }): DisplaySessionState {
  const next = structuredClone(state);
  switch (action.type) {
    case 'enumerate':
      refresh(next, action.result);
      return next;
    case 'faults':
      next.faults = { ...next.faults, ...action.faults };
      return next;
    case 'capability':
      if (action.engineReady !== undefined) next.engineReady = action.engineReady;
      if (action.replacementInProgress !== undefined) next.replacementInProgress = action.replacementInProgress;
      return next;
    case 'identify':
      if (!displayFacts(next).canIdentify) return state;
      next.counters.identifyRequests += 1;
      next.lastIdentifyRequestId = next.boundId;
      next.identifyStatus = action.status ?? 'shown';
      if (next.identifyStatus !== 'shown') refresh(next, action.refresh);
      return next;
    case 'deploy':
    case 'undeploy':
      requestDeployment(next, action.type, lifecycle, action.refresh);
      return next;
    case 'toggle-deployment':
      requestDeployment(next, displayFacts(next, lifecycle).retained ? 'undeploy' : 'deploy', lifecycle, action.refresh);
      return next;
    case 'host-lost':
      // External destruction clears the HWND, not the Engine's borrowed target ID.
      next.hostOwned = false;
      next.hostHealthy = false;
      return next;
    case 'native-event':
      if (!next.engineReady) return state;
      next.continuationPending = false;
      reconcile(next, true, action.result);
      return next;
    case 'continue-reconciliation':
      if (!next.continuationPending) return state;
      next.continuationPending = false;
      if (next.engineReady && next.desired && !next.replacementInProgress) reconcile(next, false);
      return next;
  }
}
