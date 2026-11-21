import { useEffect, useRef, useState } from 'react';
import { displayFixtures, type Display, type IdentifyState, type LauncherScenario, type RefreshState } from './displayData';

interface LauncherState {
  displays: Display[];
  position: number;
  scenario: LauncherScenario;
  refreshState: RefreshState;
  enumerationFailed: boolean;
  identify: Record<string, IdentifyState>;
  operation: string;
}
const indexAt = (position: number, count: number) => ((position % count) + count) % count;
function initialState(scenario: LauncherScenario): LauncherState {
  const displays = scenario === 'no-displays' ? [] : displayFixtures();
  let position = 0;
  if (scenario === 'identify-required' || scenario === 'identify-failure') {
    position = Math.max(0, displays.findIndex(display => display.identityStatus === 'identifyRequired'));
    if (displays[position]) displays[position] = { ...displays[position], identityStatus: 'identifyRequired', sessionStatus: 'none' };
  }
  if (scenario === 'starting' || scenario === 'disconnected') {
    position = Math.min(1, displays.length - 1);
    if (displays[position]) displays[position] = { ...displays[position], identityStatus: 'ready',
      sessionStatus: scenario === 'starting' ? 'starting' : 'disconnected' };
  }
  return { displays, position, scenario, refreshState: scenario === 'no-displays' ? 'noDisplays' : 'idle',
    enumerationFailed: false, identify: {}, operation: '' };
}

// Mock operation ownership is local to the Launcher. Timers never call native code.
export function useLauncherPrototype(scenario: LauncherScenario) {
  const [state, setState] = useState(() => initialState(scenario));
  const current = useRef(state);
  const timers = useRef(new Set<number>());
  const failNextRefresh = useRef(scenario === 'refresh-failure');
  function update(change: (value: LauncherState) => LauncherState) {
    current.current = change(current.current);
    setState(current.current);
  }
  function later(callback: () => void, milliseconds: number) {
    const timer = window.setTimeout(() => { timers.current.delete(timer); callback(); }, milliseconds);
    timers.current.add(timer);
  }
  function cancelPending() {
    timers.current.forEach(timer => window.clearTimeout(timer));
    timers.current.clear();
  }
  function finishStart(id: string) {
    later(() => update(value => {
      if (!value.displays.some(display => display.id === id && display.sessionStatus === 'starting')) return value;
      return { ...value, displays: value.displays.map(display => display.id === id ? { ...display, sessionStatus: 'ready' } : display),
        operation: value.displays.find(display => display.id === id)?.name + ' · Session ready.' };
    }), 1700);
  }
  useEffect(() => {
    const starting = current.current.displays.find(display => display.sessionStatus === 'starting');
    if (starting) finishStart(starting.id);
    return cancelPending;
  }, []);

  function reset(next: LauncherScenario) {
    cancelPending();
    failNextRefresh.current = next === 'refresh-failure';
    update(() => initialState(next));
    const starting = current.current.displays.find(display => display.sessionStatus === 'starting');
    if (starting) finishStart(starting.id);
  }
  function identify(id: string) {
    const display = current.current.displays.find(item => item.id === id);
    if (!display || display.identityStatus === 'unavailable' || current.current.identify[id] === 'identifying') return;
    update(value => ({ ...value, identify: { ...value.identify, [id]: 'identifying' }, operation: display.name + ' · Identifying display…' }));
    later(() => update(value => {
      if (!value.displays.some(item => item.id === id)) return value;
      const failed = value.scenario === 'identify-failure';
      return { ...value, identify: { ...value.identify, [id]: failed ? 'failure' : 'success' },
        displays: value.displays.map(item => item.id === id && !failed ? { ...item, identityStatus: 'ready' } : item),
        operation: display.name + ' · ' + (failed ? 'Unable to identify this display.' : 'Display identification shown.') };
    }), 950);
  }
  function activate(id: string) {
    const display = current.current.displays.find(item => item.id === id);
    if (!display || display.identityStatus !== 'ready') return;
    if (display.sessionStatus === 'ready') {
      update(value => ({ ...value, operation: display.name + ' · Session activated.' }));
    } else if (display.sessionStatus === 'none') {
      update(value => ({ ...value, displays: value.displays.map(item => item.id === id ? { ...item, sessionStatus: 'starting' } : item),
        operation: display.name + ' · Starting session…' }));
      finishStart(id);
    }
  }
  function closeSelected() {
    const value = current.current;
    const display = value.displays[indexAt(value.position, value.displays.length)];
    if (!display || display.sessionStatus !== 'ready') return;
    update(previous => ({ ...previous, displays: previous.displays.map(item => item.id === display.id ? { ...item, sessionStatus: 'closing' } : item),
      operation: display.name + ' · Closing session…' }));
    later(() => update(previous => ({ ...previous,
      displays: previous.displays.map(item => item.id === display.id && item.sessionStatus === 'closing' ? { ...item, sessionStatus: 'none' } : item),
      operation: display.name + ' · Session closed.' })), 1700);
  }
  function refresh() {
    const before = current.current;
    if (before.refreshState === 'refreshing') return;
    const removedId = before.scenario === 'display-disappearance'
      ? before.displays[indexAt(before.position, before.displays.length)]?.id : undefined;
    const fail = failNextRefresh.current;
    failNextRefresh.current = false;
    update(value => ({ ...value, refreshState: 'refreshing', operation: 'Refreshing displays…' }));
    later(() => update(value => {
      if (fail) return { ...value, refreshState: 'enumerationFailure', enumerationFailed: true, operation: 'Unable to load displays.' };
      const oldIndex = indexAt(value.position, value.displays.length);
      const selectedId = value.displays[oldIndex]?.id;
      // Successful enumeration preserves the current identity/session state.
      // Only the disappearance fixture removes a display; empty fixtures recover.
      const displays = removedId ? value.displays.filter(display => display.id !== removedId)
        : value.displays.length ? value.displays : displayFixtures();
      const retainedIndex = displays.findIndex(display => display.id === selectedId);
      const nextIndex = retainedIndex >= 0 ? retainedIndex : Math.min(Number.isNaN(oldIndex) ? 0 : oldIndex, displays.length - 1);
      const sameTopology = displays.length === value.displays.length;
      return { ...value, displays, position: sameTopology ? value.position : Math.max(0, nextIndex),
        refreshState: displays.length ? 'success' : 'noDisplays', enumerationFailed: false,
        operation: removedId ? 'Display list updated.' : 'Displays refreshed.' };
    }), 850);
  }
  return { state, reset, identify, activate, closeSelected, refresh,
    move: (change: (position: number) => number) => update(value => ({ ...value, position: change(value.position) })),
    feedback: (operation: string) => update(value => ({ ...value, operation })) };
}
