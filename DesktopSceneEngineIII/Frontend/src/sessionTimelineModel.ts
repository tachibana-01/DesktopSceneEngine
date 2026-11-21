import type { SessionProjection } from './sessionTypes.ts';
import {TIMELINE_MAXIMUM,timelineValueForTime,timeForValue} from './sessionTimelineGeometry.ts';
export {TIMELINE_MAXIMUM,timelineValueForTime,timeForValue,valueForGroovePosition} from './sessionTimelineGeometry.ts';

// Player/TimelineUtils.h and MainWindow.cpp 5351–5436. This is a UI-only
// projection; it never decodes media or changes the authored Project.
export interface TimelineDrag { itemId: string; duration: number; value: number }
export type TimelineReleaseIntent = { type: 'cancel' } | { type: 'seek'; seconds: number } | { type: 'next' };

function validDuration(duration: number): boolean {
  const value = Math.fround(duration);
  return Number.isFinite(value) && value > 0;
}

export function isTimelineEndIntent(value: number): boolean {
  return value === TIMELINE_MAXIMUM;
}

export function timelineFacts(state: SessionProjection) {
  const item = state.runtime?.items.find(candidate => candidate.id === state.activeItemId);
  if (state.closed || !item || !state.runtime || !validDuration(item.duration)) {
    return { enabled: false, itemId: null as string | null, duration: 0, time: 0, value: 0 };
  }
  return { enabled: true, itemId: item.id, duration: item.duration, time: state.runtime.time, value: timelineValueForTime(state.runtime.time, item.duration) };
}

export function beginTimelineDrag(state: SessionProjection): TimelineDrag | null {
  const facts = timelineFacts(state);
  return facts.enabled && facts.itemId ? { itemId: facts.itemId, duration: facts.duration, value: facts.value } : null;
}

export function moveTimelineDrag(drag: TimelineDrag, value: number): TimelineDrag {
  return { ...drag, value: Number.isFinite(value) ? Math.trunc(value) : drag.value };
}

export function timelineDragTargetsCurrentScene(state: SessionProjection, drag: TimelineDrag | null): boolean {
  const facts = timelineFacts(state);
  return !!drag && facts.enabled && facts.itemId === drag.itemId
    && Math.abs(Math.fround(Math.fround(facts.duration) - Math.fround(drag.duration))) <= Math.fround(0.0001);
}

export function timelineReleaseIntent(state: SessionProjection, drag: TimelineDrag): TimelineReleaseIntent {
  if (!timelineDragTargetsCurrentScene(state, drag)) return { type: 'cancel' };
  return isTimelineEndIntent(drag.value) ? { type: 'next' } : { type: 'seek', seconds: timeForValue(drag.value, drag.duration) };
}

function precedingPositiveFloat(value: number): number {
  const floats = new Float32Array([value]);
  const bits = new Uint32Array(floats.buffer);
  bits[0] -= 1;
  return floats[0];
}

// Engine::seek (Engine.cpp 1833–1940): commit only on successful mock seek,
// clamp before EOF, preserve pause/speed/audio, and clear completion halt.
// Next deliberately remains the existing Phase C playback transition.
export function applyTimelineSeek(state: SessionProjection, seconds: number): SessionProjection {
  const facts = timelineFacts(state);
  const target = Math.fround(seconds);
  if (!facts.enabled || !state.runtime || !Number.isFinite(target) || state.faults.seekFails) return state;
  const duration = Math.fround(facts.duration);
  const time = target >= duration ? precedingPositiveFloat(duration) : Math.max(0, target);
  return { ...state, runtime: { ...state.runtime, time, halted: false } };
}
