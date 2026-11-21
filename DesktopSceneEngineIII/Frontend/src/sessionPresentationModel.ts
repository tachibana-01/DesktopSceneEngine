import { createSessionDisplay, displayTransition, type DisplayAction } from './sessionDisplayModel.ts';
import { createSessionView, viewTransition, type ViewAction } from './sessionViewModel.ts';
import type { SessionProjection } from './sessionTypes.ts';
import type { SessionLifecycle } from './sessionLifecycleModel.ts';

export type PresentationAction = { scope: 'display'; action: DisplayAction } | { scope: 'view'; action: ViewAction };
export const sessionDisplay = (state: SessionProjection) => state.display ?? createSessionDisplay(state.project.displayOwner);
export const sessionView = (state: SessionProjection) => state.view ?? createSessionView(state.project.displayOwner);

/** Keep platform/presentation and application preference effects outside authored
 * Project, playback, selection and document revision. All services here are mocks. */
export function presentationTransition(state: SessionProjection, event: PresentationAction, lifecycle: SessionLifecycle = { closePending: false, ending: false }): SessionProjection {
  if (state.closed) return state;
  if (event.scope === 'display') return { ...state, display: displayTransition(sessionDisplay(state), event.action, lifecycle) };
  const before = sessionView(state), view = viewTransition(before, event.action);
  return view === before ? state : { ...state, view };
}
