// UI-only literal projection of Player/PlaybackSequenceController.h.
// No React, project persistence, media binding, or authoritative domain state.
export type SequenceMode = 'list-loop' | 'random' | 'repeat-current';
export interface Position { playlistId: string; itemId: string }
export interface PlaylistItems { playlistId: string; itemIds: string[]; itemMode: SequenceMode }
export interface Collections { playlistIds: string[]; itemsByPlaylist: PlaylistItems[] }
export type SequenceResult<T = Position> = { kind: 'identity'; identity: T }
  | { kind: 'round-complete' } | { kind: 'no-target' };
export interface ScriptedSampler { samples: number[]; cursor: number }
export interface LayerState {
  mode: SequenceMode; current: string | null; invocationAnchor: string | null;
  randomState: number; randomRound: string[]; nextRandomIndex: number; currentRandomIndex: number | null;
}
export interface Invocation { playlistId: string; itemLayer: LayerState }
export interface ControllerState {
  playlistMode: SequenceMode; playlistLayer: LayerState; innerSeedGenerator: number;
  playlistSampler: ScriptedSampler; itemSampler: ScriptedSampler;
  currentInvocation: Invocation | null; previousInvocation: Invocation | null; forwardInvocation: Invocation | null;
}
export function createLayer(mode: SequenceMode = 'list-loop', seed = 0): LayerState {
  return { mode, current: null, invocationAnchor: null, randomState: seed >>> 0,
    randomRound: [], nextRandomIndex: 0, currentRandomIndex: null };
}
export function createController(seed = 0, playlistSamples: number[] = [], itemSamples: number[] = []): ControllerState {
  return { playlistMode: 'list-loop', playlistLayer: createLayer('list-loop', seed), innerSeedGenerator: (seed ^ 0x7f4a7c15) >>> 0,
    playlistSampler: { samples: [...playlistSamples], cursor: 0 }, itemSampler: { samples: [...itemSamples], cursor: 0 },
    currentInvocation: null, previousInvocation: null, forwardInvocation: null };
}

const selected = <T>(identity: T): SequenceResult<T> => ({ kind: 'identity', identity });
const noTarget = (): { kind: 'no-target' } => ({ kind: 'no-target' });
const roundComplete = (): { kind: 'round-complete' } => ({ kind: 'round-complete' });

// Deterministic mock index sampling implements production's sampler seam, not
// its mt19937_64 bitstream. Stored rounds/history and replacement semantics are
// literal ports. All RNG state is data so clone/rollback remains reproducible.
function nextRandom(value: number): number {
  return (Math.imul(value, 1664525) + 1013904223) >>> 0;
}
function sampleIndex(state: LayerState, count: number, sampler?: ScriptedSampler): number {
  if (sampler && sampler.cursor < sampler.samples.length) {
    const sample = Math.trunc(sampler.samples[sampler.cursor++]!);
    return ((sample % count) + count) % count;
  }
  state.randomState = nextRandom(state.randomState);
  return Math.floor((state.randomState / 0x100000000) * count);
}
function clearRandomRound(state: LayerState): void {
  state.randomRound = [];
  state.nextRandomIndex = 0;
  state.currentRandomIndex = null;
}
function generateRandomRound(state: LayerState, ids: readonly string[], sampler?: ScriptedSampler): void {
  for (let index = 0; index < ids.length; index++) state.randomRound.push(ids[sampleIndex(state, ids.length, sampler)]!);
}
export function layerSetMode(state: LayerState, mode: SequenceMode, ids: readonly string[], sampler?: ScriptedSampler): void {
  state.mode = mode;
  state.invocationAnchor = state.current;
  clearRandomRound(state);
  if (mode === 'random' && state.current !== null) generateRandomRound(state, ids, sampler);
}
export function layerRebase(state: LayerState, id: string, ids: readonly string[], sampler?: ScriptedSampler): void {
  state.current = id;
  state.invocationAnchor = id;
  clearRandomRound(state);
  if (state.mode === 'random') generateRandomRound(state, ids, sampler);
}
export function layerClear(state: LayerState): void {
  state.current = null;
  state.invocationAnchor = null;
  clearRandomRound(state);
}
function hasValidCurrent(state: LayerState, ids: readonly string[]): boolean {
  return state.current !== null && ids.includes(state.current);
}
function moveListForward(state: LayerState, ids: readonly string[], allowWrap: boolean): SequenceResult<string> {
  const index = ids.indexOf(state.current!);
  if (index < 0) return noTarget();
  if (index + 1 === ids.length) {
    if (!allowWrap) return roundComplete();
    state.current = ids[0]!;
  } else state.current = ids[index + 1]!;
  return selected(state.current);
}
function moveListBackward(state: LayerState, ids: readonly string[], allowWrap: boolean): SequenceResult<string> {
  const index = ids.indexOf(state.current!);
  if (index < 0) return noTarget();
  if (index === 0) {
    if (!allowWrap) return noTarget();
    state.current = ids[ids.length - 1]!;
  } else state.current = ids[index - 1]!;
  return selected(state.current);
}
function moveRandomForward(state: LayerState, ids: readonly string[]): SequenceResult<string> {
  while (state.nextRandomIndex < state.randomRound.length) {
    const index = state.nextRandomIndex++;
    const candidate = state.randomRound[index]!;
    if (!ids.includes(candidate)) continue;
    state.current = candidate;
    state.currentRandomIndex = index;
    return selected(candidate);
  }
  return roundComplete();
}
function moveRandomBackward(state: LayerState, ids: readonly string[]): SequenceResult<string> {
  if (state.currentRandomIndex === null) return noTarget();
  let index = state.currentRandomIndex;
  while (index > 0) {
    index--;
    const candidate = state.randomRound[index]!;
    if (!ids.includes(candidate)) continue;
    state.current = candidate;
    state.currentRandomIndex = index;
    state.nextRandomIndex = index + 1;
    return selected(candidate);
  }
  if (state.invocationAnchor === null || !ids.includes(state.invocationAnchor)) return noTarget();
  state.current = state.invocationAnchor;
  state.currentRandomIndex = null;
  state.nextRandomIndex = 0;
  return selected(state.current);
}
// Direct primitive navigation wraps ListLoop. MainWindow Next uses the finite
// controllerCompleteCurrentItem below, just like production navigateScene.
export function layerNext(state: LayerState, ids: readonly string[]): SequenceResult<string> {
  if (!hasValidCurrent(state, ids)) return noTarget();
  switch (state.mode) {
    case 'list-loop': return moveListForward(state, ids, true);
    case 'random': return moveRandomForward(state, ids);
    case 'repeat-current': return selected(state.current!);
  }
}
export function layerPrevious(state: LayerState, ids: readonly string[]): SequenceResult<string> {
  if (!hasValidCurrent(state, ids)) return noTarget();
  switch (state.mode) {
    case 'list-loop': return moveListBackward(state, ids, true);
    case 'random': return moveRandomBackward(state, ids);
    case 'repeat-current': return selected(state.current!);
  }
}
export function layerAdvanceInvocation(state: LayerState, ids: readonly string[]): SequenceResult<string> {
  if (!hasValidCurrent(state, ids)) return noTarget();
  switch (state.mode) {
    case 'list-loop': return moveListForward(state, ids, false);
    case 'random': return moveRandomForward(state, ids);
    case 'repeat-current': return selected(state.current!);
  }
}
export function layerPreviousInInvocation(state: LayerState, ids: readonly string[]): SequenceResult<string> {
  if (!hasValidCurrent(state, ids)) return noTarget();
  switch (state.mode) {
    case 'list-loop':
      if (state.invocationAnchor !== null && state.current === state.invocationAnchor) return noTarget();
      return moveListBackward(state, ids, false);
    case 'random': return moveRandomBackward(state, ids);
    case 'repeat-current': return selected(state.current!);
  }
}

function playlistItemsFor(collections: Collections, playlistId: string): PlaylistItems | undefined {
  return collections.itemsByPlaylist.find(entry => entry.playlistId === playlistId);
}
function clearInvocationHistory(state: ControllerState): void {
  state.previousInvocation = null;
  state.forwardInvocation = null;
}
function makeItemLayer(state: ControllerState, collections: Collections, playlistId: string): LayerState {
  state.innerSeedGenerator = nextRandom(state.innerSeedGenerator);
  return createLayer(playlistItemsFor(collections, playlistId)?.itemMode ?? 'list-loop', state.innerSeedGenerator);
}
function hasValidInvocation(invocation: Invocation, collections: Collections): boolean {
  return collections.playlistIds.includes(invocation.playlistId)
    && invocation.itemLayer.current !== null
    && Boolean(playlistItemsFor(collections, invocation.playlistId)?.itemIds.includes(invocation.itemLayer.current));
}
function hasValidCurrentInvocation(state: ControllerState, collections: Collections): boolean {
  return state.currentInvocation !== null && state.playlistLayer.current !== null
    && state.playlistLayer.current === state.currentInvocation.playlistId
    && hasValidInvocation(state.currentInvocation, collections);
}
function currentResult(state: ControllerState): SequenceResult {
  const current = controllerCurrent(state);
  return current ? selected(current) : noTarget();
}
export function controllerSetPlaylistMode(state: ControllerState, mode: SequenceMode, collections: Collections): void {
  state.playlistMode = mode;
  layerSetMode(state.playlistLayer, mode, collections.playlistIds, state.playlistSampler);
  clearInvocationHistory(state);
}
export function controllerSynchronizeItemMode(state: ControllerState, collections: Collections): void {
  if (!state.currentInvocation) return;
  const entry = playlistItemsFor(collections, state.currentInvocation.playlistId);
  if (entry && state.currentInvocation.itemLayer.mode !== entry.itemMode) {
    layerSetMode(state.currentInvocation.itemLayer, entry.itemMode, entry.itemIds, state.itemSampler);
    clearInvocationHistory(state);
  }
}
export function controllerRebase(state: ControllerState, collections: Collections, playlistId: string, itemId: string): void {
  layerRebase(state.playlistLayer, playlistId, collections.playlistIds, state.playlistSampler);
  state.currentInvocation = { playlistId, itemLayer: makeItemLayer(state, collections, playlistId) };
  layerRebase(state.currentInvocation.itemLayer, itemId, playlistItemsFor(collections, playlistId)?.itemIds ?? [], state.itemSampler);
  clearInvocationHistory(state);
}
export function controllerClear(state: ControllerState): void {
  layerClear(state.playlistLayer);
  state.currentInvocation = null;
  clearInvocationHistory(state);
}
export function controllerCurrent(state: ControllerState): Position | null {
  if (!state.currentInvocation || state.currentInvocation.itemLayer.current === null) return null;
  return { playlistId: state.currentInvocation.playlistId, itemId: state.currentInvocation.itemLayer.current };
}
function advanceOuterInvocation(state: ControllerState, collections: Collections): SequenceResult {
  if (!collections.playlistIds.some(id => (playlistItemsFor(collections, id)?.itemIds.length ?? 0) > 0)) return noTarget();
  const completedInvocation = structuredClone(state.currentInvocation!);
  const playlistSnapshot = structuredClone(state.playlistLayer);
  for (let attempt = 0; attempt < collections.playlistIds.length; attempt++) {
    const playlistResult = layerNext(state.playlistLayer, collections.playlistIds);
    if (playlistResult.kind === 'round-complete') {
      if (state.playlistMode !== 'random') { state.playlistLayer = playlistSnapshot; return noTarget(); }
      layerRebase(state.playlistLayer, completedInvocation.playlistId, collections.playlistIds, state.playlistSampler);
      return roundComplete();
    }
    if (playlistResult.kind !== 'identity') { state.playlistLayer = playlistSnapshot; return noTarget(); }
    const entry = playlistItemsFor(collections, playlistResult.identity);
    if (!entry || !entry.itemIds.length) continue;
    state.previousInvocation = completedInvocation;
    if (state.forwardInvocation?.playlistId === playlistResult.identity && hasValidInvocation(state.forwardInvocation, collections)) {
      state.currentInvocation = structuredClone(state.forwardInvocation);
      state.forwardInvocation = null;
    } else {
      state.currentInvocation = { playlistId: playlistResult.identity, itemLayer: makeItemLayer(state, collections, playlistResult.identity) };
      layerRebase(state.currentInvocation.itemLayer, entry.itemIds[0]!, entry.itemIds, state.itemSampler);
      state.forwardInvocation = null;
    }
    return currentResult(state);
  }
  if (state.playlistMode === 'random') {
    layerRebase(state.playlistLayer, completedInvocation.playlistId, collections.playlistIds, state.playlistSampler);
    return roundComplete();
  }
  state.playlistLayer = playlistSnapshot;
  return noTarget();
}
export function controllerCompleteCurrentItem(state: ControllerState, collections: Collections): SequenceResult {
  if (!hasValidCurrentInvocation(state, collections)) return noTarget();
  const invocation = state.currentInvocation!;
  const result = layerAdvanceInvocation(invocation.itemLayer, playlistItemsFor(collections, invocation.playlistId)!.itemIds);
  if (result.kind === 'identity') return currentResult(state);
  if (result.kind === 'no-target') return noTarget();
  return advanceOuterInvocation(state, collections);
}
export function controllerPrevious(state: ControllerState, collections: Collections): SequenceResult {
  if (!hasValidCurrentInvocation(state, collections)) return noTarget();
  const invocation = state.currentInvocation!;
  const result = layerPreviousInInvocation(invocation.itemLayer, playlistItemsFor(collections, invocation.playlistId)!.itemIds);
  if (result.kind === 'identity') return currentResult(state);
  if (!state.previousInvocation || !hasValidInvocation(state.previousInvocation, collections)) return noTarget();
  const playlistSnapshot = structuredClone(state.playlistLayer);
  const playlistResult = layerPrevious(state.playlistLayer, collections.playlistIds);
  if (playlistResult.kind !== 'identity' || playlistResult.identity !== state.previousInvocation.playlistId) {
    state.playlistLayer = playlistSnapshot;
    return noTarget();
  }
  state.forwardInvocation = structuredClone(state.currentInvocation!);
  state.currentInvocation = structuredClone(state.previousInvocation);
  state.previousInvocation = null;
  return currentResult(state);
}
