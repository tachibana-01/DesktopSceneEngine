import { createSessionHide, hideTransition } from './sessionHideModel.ts';
import type { HideAction, SessionHideState } from './sessionHideModel.ts';

// Session role lifetime is shared authority for Hide and Display capability.
// Presentation models consume these facts without storing their own copies.
export interface SessionLifecycle {
  closePending: boolean;
  ending: boolean;
}

export interface SessionRoleState {
  lifecycle: SessionLifecycle;
  hide: SessionHideState;
}

export function createSessionRole(): SessionRoleState {
  return { lifecycle: { closePending: false, ending: false }, hide: createSessionHide() };
}

export function roleTransition(state: SessionRoleState, action: HideAction): SessionRoleState {
  const hide = hideTransition(state.hide, action, state.lifecycle);
  if (hide === state.hide) return state;
  let lifecycle = state.lifecycle;
  if (action.type === 'close-begin') lifecycle = { ...lifecycle, closePending: true };
  else if (action.type === 'close-abort') lifecycle = { ...lifecycle, closePending: false };
  else if (action.type === 'teardown') lifecycle = { ...lifecycle, ending: true };
  return { lifecycle, hide };
}
