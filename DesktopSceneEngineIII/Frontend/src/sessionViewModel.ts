export type DesktopBackgroundMode = 'cover' | 'reveal';
export type ViewPreferenceSnapshot = { highlight: boolean; backgrounds: Record<string, DesktopBackgroundMode> };
export type SessionViewState = { boundId: string; highlighted: boolean; background: DesktopBackgroundMode; persisted: ViewPreferenceSnapshot; highlightWrites: number; backgroundWrites: number; storeAvailable: boolean; highlightSaveFails: boolean; backgroundSaveFails: boolean; warning: string | null };
export type ViewAction = { type: 'highlight'; value: boolean } | { type: 'background'; mode: DesktopBackgroundMode } | { type: 'external-background'; id: string; mode: DesktopBackgroundMode } | { type: 'dismiss-warning' };
export function createSessionView(boundId: string, persisted: ViewPreferenceSnapshot = { highlight: false, backgrounds: {} }): SessionViewState {
  return { boundId, highlighted: persisted.highlight, background: persisted.backgrounds[boundId] ?? 'cover', persisted: structuredClone(persisted), highlightWrites: 0, backgroundWrites: 0, storeAvailable: true, highlightSaveFails: false, backgroundSaveFails: false, warning: null };
}
// MainWindow.cpp 775–785 and 4114–4145. This store is a detached in-memory
// fixture, not a choice of future preference persistence architecture.
export function viewTransition(state: SessionViewState, action: ViewAction): SessionViewState {
  if (action.type === 'dismiss-warning') return state.warning ? { ...state, warning: null } : state;
  // The production warning runs a modal acknowledgement. User preference
  // intents cannot replace it or discard its live choice before acknowledgement.
  if (state.warning && action.type !== 'external-background') return state;
  if (action.type === 'highlight') {
    if (action.value === state.highlighted) return state;
    const next = structuredClone(state); next.highlighted = action.value; next.warning = null;
    if (next.storeAvailable) { ++next.highlightWrites; if (!next.highlightSaveFails) next.persisted.highlight = action.value; }
    return next; // Production ignores the void Highlight save result.
  }
  if (action.mode !== 'cover' && action.mode !== 'reveal') return state;
  if (action.type === 'external-background') {
    return { ...state, persisted: { ...state.persisted, backgrounds: { ...state.persisted.backgrounds, [action.id]: action.mode } } };
  }
  if (action.mode === state.background) return state;
  const next = structuredClone(state); next.background = action.mode; next.warning = null;
  if (next.storeAvailable) {
    ++next.backgroundWrites;
    if (next.backgroundSaveFails) next.warning = 'The desktop background setting could not be saved. This session keeps the selected mode.';
    else next.persisted.backgrounds[next.boundId] = action.mode;
  }
  return next;
}
