// Value-only projection of SessionPresentation.cpp and SessionCoordinatorJoin.
// These facts are deterministic mock inputs, never tray/IPC/native authority.
import type { SessionLifecycle } from './sessionLifecycleModel.ts';

export const HIDE_QUALIFICATION_LIFETIME_MS = 3000;
const READY: SessionLifecycle = { closePending: false, ending: false };

export type HideRecoveryFacts = {
  roleEnding: boolean;
  joined: boolean;
  recoveryQualified: boolean;
  controlConnected: boolean;
  coordinatorLifetime: boolean;
  coordinatorAlive: boolean;
  qualificationAgeValid: boolean;
  qualificationAgeMs: number;
};
export type HideRequest = { request: number; showGeneration: number; sentAtMs: number };
export type HideResult = 'none' | 'requested' | 'not-eligible' | 'hidden' | 'permit-denied'
  | 'permit-expired' | 'stale-generation' | 'recheck-failed' | 'restored' | 'ending' | 'close-pending' | 'close-aborted';
export type SessionHideState = {
  windowAvailable: boolean;
  hidden: boolean;
  showGeneration: number;
  // Caller aggregates relevant application/Session/native-owner modal blockers.
  // An independent modeless BGM window alone is not a blocker.
  modalBlocked: boolean;
  closeWasHidden: boolean;
  closeRecoveryLost: boolean;
  recovery: HideRecoveryFacts;
  nowMs: number;
  sequence: number;
  pending: HideRequest | null;
  lastResult: HideResult;
};
export type HideAction =
  | { type: 'facts'; facts?: Partial<Pick<SessionHideState, 'windowAvailable' | 'modalBlocked'>>; recovery?: Partial<HideRecoveryFacts> }
  | { type: 'request'; modalBlocked?: boolean }
  | { type: 'permit'; request: number; allowed: boolean; modalBlocked?: boolean }
  | { type: 'advance'; milliseconds: number }
  | { type: 'revoke' }
  | { type: 'restore' }
  | { type: 'teardown' }
  | { type: 'close-begin' }
  | { type: 'close-interaction' }
  | { type: 'close-abort' };

export function createSessionHide(): SessionHideState {
  return {
    windowAvailable: true, hidden: false, showGeneration: 0,
    modalBlocked: false, closeWasHidden: false, closeRecoveryLost: false,
    recovery: { roleEnding: false, joined: true, recoveryQualified: true, controlConnected: true,
      coordinatorLifetime: true, coordinatorAlive: true, qualificationAgeValid: true, qualificationAgeMs: 0 },
    nowMs: 0, sequence: 0, pending: null, lastResult: 'none',
  };
}

// ApplicationCoordinator.cpp 1153–1159: a cached "qualified" reply alone is
// insufficient without a current living owner/channel and an unexpired lease.
export function hideFacts(state: SessionHideState, modalBlocked = state.modalBlocked, lifecycle: SessionLifecycle = READY) {
  const recovery = state.recovery;
  const qualified = !recovery.roleEnding && recovery.joined && recovery.recoveryQualified
    && recovery.controlConnected && recovery.coordinatorLifetime && recovery.coordinatorAlive
    && recovery.qualificationAgeValid && recovery.qualificationAgeMs < HIDE_QUALIFICATION_LIFETIME_MS;
  const reason = lifecycle.ending ? 'ending' : !state.windowAvailable ? 'window-unavailable'
    : lifecycle.closePending ? 'close-pending' : !qualified ? 'recovery-unavailable'
      : modalBlocked ? 'modal-blocked' : 'ready';
  return {
    qualified,
    eligible: reason === 'ready',
    // Ordinary Close never needs recovery qualification (SessionPresentation 73).
    canClose: !lifecycle.ending && state.windowAvailable && !lifecycle.closePending && !modalBlocked,
    hidden: state.hidden,
    pending: state.pending?.request ?? null,
    reason,
  };
}

function restore(state: SessionHideState, lifecycle: SessionLifecycle): SessionHideState {
  if (lifecycle.ending || !state.windowAvailable) return state;
  // SessionPresentation.cpp 150–153: even an already-visible Show invalidates
  // any previously requested Hide; recovery qualification is not a prerequisite.
  return { ...state, hidden: false, showGeneration: state.showGeneration + 1, lastResult: 'restored' };
}

function refreshEligibility(state: SessionHideState, lifecycle: SessionLifecycle): SessionHideState {
  const qualified = hideFacts(state, state.modalBlocked, lifecycle).qualified;
  let next = state;
  if (lifecycle.closePending && !qualified && !state.closeRecoveryLost) next = { ...next, closeRecoveryLost: true };
  // A temporary modal does not imply recovery loss. Known qualification loss
  // must immediately surface a hidden primary, including during Close Pending.
  if (next.hidden && !lifecycle.ending && !qualified) next = restore(next, lifecycle);
  return next;
}

export function hideTransition(state: SessionHideState, action: HideAction, lifecycle: SessionLifecycle = READY): SessionHideState {
  switch (action.type) {
    case 'facts':
      return refreshEligibility({ ...state, ...action.facts, recovery: { ...state.recovery, ...action.recovery } }, lifecycle);
    case 'advance': {
      if (!Number.isFinite(action.milliseconds) || action.milliseconds < 0) return state;
      const elapsed = Math.floor(action.milliseconds);
      // Sample monotonic ages without pretending to receive a qualification
      // renewal. A separately delivered revoke action represents expiry control.
      return refreshEligibility({ ...state, nowMs: state.nowMs + elapsed,
        recovery: { ...state.recovery, qualificationAgeMs: state.recovery.qualificationAgeMs + elapsed } }, lifecycle);
    }
    case 'request': {
      if (!hideFacts(state, action.modalBlocked ?? state.modalBlocked, lifecycle).eligible) return { ...state, lastResult: 'not-eligible' };
      if (state.pending) return state; // One outstanding request (Coordinator 1203).
      const request = state.sequence + 1;
      return { ...state, sequence: request,
        pending: { request, showGeneration: state.showGeneration, sentAtMs: state.nowMs }, lastResult: 'requested' };
    }
    case 'permit': {
      // Only an authenticated current-channel permit should be delivered here;
      // wire parsing/epoch/incarnation authentication remains native integration.
      const pending = state.pending;
      if (!pending || !action.request || action.request !== pending.request) return state;
      const next = { ...state, pending: null };
      // Matching replies consume the request before any authority/local recheck,
      // including denied, overdue, stale-generation and modal-raced replies.
      if (!action.allowed) return { ...next, lastResult: 'permit-denied' };
      if (state.nowMs - pending.sentAtMs >= HIDE_QUALIFICATION_LIFETIME_MS) return { ...next, lastResult: 'permit-expired' };
      if (pending.showGeneration !== state.showGeneration) return { ...next, lastResult: 'stale-generation' };
      if (!hideFacts(state, action.modalBlocked ?? state.modalBlocked, lifecycle).eligible) return { ...next, lastResult: 'recheck-failed' };
      return { ...next, hidden: true, lastResult: 'hidden' };
    }
    case 'revoke':
      return refreshEligibility({ ...state, pending: null, recovery: { ...state.recovery, recoveryQualified: false } }, lifecycle);
    case 'restore': return restore(state, lifecycle);
    case 'teardown':
      return lifecycle.ending ? state : { ...state, pending: null,
        showGeneration: state.showGeneration + 1,
        recovery: { ...state.recovery, roleEnding: true, recoveryQualified: false }, lastResult: 'ending' };
    case 'close-begin':
      if (!hideFacts(state, state.modalBlocked, lifecycle).canClose) return state;
      return { ...state, closeWasHidden: state.hidden, closeRecoveryLost: false, lastResult: 'close-pending' };
    case 'close-interaction':
      return lifecycle.closePending && state.closeWasHidden ? restore(state, lifecycle) : state;
    case 'close-abort': {
      if (!lifecycle.closePending || lifecycle.ending) return state;
      const afterLifecycle = { ...lifecycle, closePending: false };
      let next: SessionHideState = { ...state, lastResult: 'close-aborted' };
      if (state.closeWasHidden) {
        if (!state.closeRecoveryLost && hideFacts(state, state.modalBlocked, afterLifecycle).qualified && !state.modalBlocked) next = { ...next, hidden: true };
        else next = restore(next, afterLifecycle);
      }
      return refreshEligibility(next, afterLifecycle);
    }
  }
}
