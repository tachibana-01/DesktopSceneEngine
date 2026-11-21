// Duration accompanies the caller's active source, but is not part of production decode identity.
export type TimelineHoverSource = { itemId: string; source: string; mediaType: 'image' | 'video'; duration: number };
export type TimelineHoverRequest = { source: TimelineHoverSource; timeSeconds: number; durationMilliseconds: number; generation: number; serial: number; representativeTimestampMilliseconds: number | null };
export type TimelineHoverResult = { request: TimelineHoverRequest; imageId: string | null };
export type TimelineHoverComposition = { key: string; canvasNumerator: number; canvasDenominator: number };
export type TimelineHoverModel = {
  active: boolean; source: TimelineHoverSource | null; rawTimeSeconds: number | null;
  status: 'loading' | 'unavailable' | 'valid'; generation: number; serial: number;
  presentationGeneration: number; composition: TimelineHoverComposition; compositionDirty: boolean;
  canvasSize: { width: number; height: number };
  pending: (TimelineHoverRequest & { submitted: boolean }) | null;
  displayed: { imageId: string; request: TimelineHoverRequest; presentationGeneration: number } | null;
  dueAt: number | null; submittedRequests: TimelineHoverRequest[]; imageReads: TimelineHoverRequest[]; staleResultsRejected: number;
};
export function createTimelineHover(): TimelineHoverModel {
  return { active: false, source: null, rawTimeSeconds: null, status: 'unavailable', generation: 0, serial: 0, presentationGeneration: 0, composition: { key: 'initial', canvasNumerator: 16, canvasDenominator: 9 }, compositionDirty: false, canvasSize: { width: 224, height: 126 }, pending: null, displayed: null, dueAt: null, submittedRequests: [], imageReads: [], staleResultsRejected: 0 };
}
// Player/TimelineRepresentativeTimestamp.h and Resources/RepresentativeFrameSampling.cpp.
// Times here are milliseconds, using the production 120-frame sampling cap.
export function representativeTimelineTimestamp(duration: number, hover: number): number | null {
  if (!Number.isFinite(duration) || duration <= 0) return null;
  const piecewise = duration < 300000 ? 5000 : duration < 600000 ? 10000
    : duration < 1200000 ? 15000 : duration < 1800000 ? 20000
    : duration < 3600000 ? 30000 : duration < 7200000 ? 60000 : 120000;
  const interval = Math.max(piecewise, Math.ceil(Math.ceil(duration / 120) / 5000) * 5000);
  const clamped = hover < 0 ? 0 : hover >= duration ? duration - 1 : hover;
  return Math.floor(clamped / interval) * interval;
}

// The popup is a reduced Desktop Canvas, not the source media's aspect or Preview widget size.
export function timelineHoverCanvasSize(n: number, d: number) {
  if (!Number.isFinite(n) || !Number.isFinite(d) || n <= 0 || d <= 0) return { width: 0, height: 0 };
  const scale = Math.min(224 / n, 140 / d);
  return { width: Math.max(1, Math.round(n * scale)), height: Math.max(1, Math.round(d * scale)) };
}
export const timelinePreviewCanvasSize = timelineHoverCanvasSize;

function sameSource(a: TimelineHoverSource | null, b: TimelineHoverSource | null): boolean {
  return a !== null && b !== null && a.itemId === b.itemId && a.source === b.source && a.mediaType === b.mediaType;
}
function durationMilliseconds(source: TimelineHoverSource | null): number | null {
  if (!source || !source.itemId || !source.source) return null;
  const seconds = Math.fround(source.duration);
  if (!Number.isFinite(seconds) || seconds <= 0) return null;
  const rounded = Math.round(seconds * 1000);
  return rounded > 0 && rounded <= 9223372036854775807 ? rounded : null;
}
function requestOnly(pending: TimelineHoverRequest): TimelineHoverRequest {
  return { source: structuredClone(pending.source), timeSeconds: pending.timeSeconds,
    durationMilliseconds: pending.durationMilliseconds, generation: pending.generation,
    serial: pending.serial, representativeTimestampMilliseconds: pending.representativeTimestampMilliseconds };
}
function refreshComposition(model: TimelineHoverModel, composition: TimelineHoverComposition): void {
  const size = timelineHoverCanvasSize(composition.canvasNumerator, composition.canvasDenominator);
  if (size.width === 0 || size.height === 0) { model.compositionDirty = true; return; }
  const changed = size.width !== model.canvasSize.width || size.height !== model.canvasSize.height
    || composition.key !== model.composition.key;
  if (!changed && !model.compositionDirty) return;
  model.composition = structuredClone(composition);
  model.canvasSize = size;
  ++model.presentationGeneration;
  if (!model.displayed) { model.compositionDirty = false; return; }
  if (!model.active) { model.compositionDirty = true; return; }
  // Source-frame retrieval stays untouched; UI can compose the same deterministic frame again.
  model.displayed.presentationGeneration = model.presentationGeneration;
  model.compositionDirty = false;
}

// A pointer event has already mapped the actual slider groove to scene-local time.
// Single-shot timer parity: replacing a request does NOT restart an active 75ms timer.
export function updateTimelineHover(model: TimelineHoverModel, source: TimelineHoverSource | null, seconds: number, now: number): TimelineHoverModel {
  const duration = durationMilliseconds(source);
  if (duration === null || !source) return invalidateTimelineHover(model);
  const next = structuredClone(model);
  if (!sameSource(next.source, source)) {
    next.dueAt = null;
    next.pending = null;
    ++next.generation;
    next.displayed = null;
    next.status = 'unavailable';
  }
  next.source = structuredClone(source);
  next.active = true;
  next.rawTimeSeconds = Math.fround(seconds);
  refreshComposition(next, next.composition);

  if (source.mediaType === 'image') {
    if (next.displayed && sameSource(next.displayed.request.source, source)
      && next.displayed.request.generation === next.generation
      && next.displayed.presentationGeneration === next.presentationGeneration) return next;
    next.pending = { source: structuredClone(source), timeSeconds: Math.fround(seconds),
      durationMilliseconds: duration, generation: next.generation, serial: ++next.serial,
      representativeTimestampMilliseconds: null, submitted: false };
    next.displayed = null;
    next.status = 'loading';
    next.dueAt ??= now + 75;
    return next;
  }

  const bucket = representativeTimelineTimestamp(duration, Math.round(Math.fround(seconds) * 1000));
  if (bucket === null) { next.status = 'unavailable'; next.displayed = null; return next; }
  const displayedMatchesSource = next.displayed !== null && sameSource(next.displayed.request.source, source);
  const displayedMatches = displayedMatchesSource && next.displayed!.request.representativeTimestampMilliseconds === bucket;
  const pendingMatches = next.pending !== null && sameSource(next.pending.source, source)
    && next.pending.representativeTimestampMilliseconds === bucket;
  // decideTimelinePreviewImageRequest: returning to the displayed frame cancels obsolete work.
  const shouldSubmit = !displayedMatches && !pendingMatches;
  const shouldCancelPending = next.pending !== null && !pendingMatches;
  if (shouldCancelPending) {
    const waiting = !next.pending!.submitted;
    next.pending = null;
    ++next.serial;
    if (waiting && !shouldSubmit) next.dueAt = null;
  }
  if (!shouldSubmit) return next;
  next.pending = { source: structuredClone(source), timeSeconds: Math.fround(seconds),
    durationMilliseconds: duration, generation: next.generation, serial: ++next.serial,
    representativeTimestampMilliseconds: bucket, submitted: false };
  if (!displayedMatchesSource) next.status = 'loading';
  next.dueAt ??= now + 75;
  return next;
}

// Clock and provider are supplied by the caller. Image reads are synchronous after the same
// debounce; only Video submits worker requests. No real decoding, caching, or timer is used.
export function tickTimelineHover(model: TimelineHoverModel, now: number, imageId?: string | null): TimelineHoverModel {
  if (model.dueAt === null || now < model.dueAt) return model;
  const next = structuredClone(model);
  next.dueAt = null;
  if (!next.pending || next.pending.submitted) return next;
  const request = requestOnly(next.pending);
  if (request.generation !== next.generation || !sameSource(next.source, request.source)) {
    next.pending = null;
    return next;
  }
  if (request.source.mediaType === 'image') {
    next.pending = null;
    next.imageReads.push(request);
    const synchronousImage = imageId === undefined
      ? `${request.source.itemId}:image:${request.source.source}` : imageId;
    return applyTimelineHoverResult(next, { request, imageId: synchronousImage });
  }
  next.pending.submitted = true;
  next.submittedRequests.push(request);
  return next;
}

export function applyTimelineHoverResult(model: TimelineHoverModel, result: TimelineHoverResult): TimelineHoverModel {
  const next = structuredClone(model), request = result.request;
  // TimelinePreviewWorker::shouldApplyTimelinePreviewResult checks these four facts. Bucket
  // participates in request coalescing; no extra result rule is invented at this boundary.
  if (!next.active || !sameSource(next.source, request.source)
    || next.generation !== request.generation || next.serial !== request.serial) {
    ++next.staleResultsRejected;
    return next;
  }
  if (next.pending && sameSource(next.pending.source, request.source)
    && next.pending.generation === request.generation && next.pending.serial === request.serial) next.pending = null;
  if (result.imageId === null) {
    next.displayed = null;
    next.status = 'unavailable';
    return next;
  }
  refreshComposition(next, next.composition);
  next.displayed = { imageId: result.imageId, request: requestOnly(request), presentationGeneration: next.presentationGeneration };
  next.status = 'valid';
  return next;
}

// MainWindow::hideTimelineHoverPopup: leave, pointer press/move-with-buttons, or popup hide.
// A retained source frame remains available on reentry; late results cannot reopen the UI.
export function hideTimelineHover(model: TimelineHoverModel): TimelineHoverModel {
  const next = structuredClone(model);
  next.dueAt = null;
  next.pending = null;
  ++next.serial;
  next.active = false;
  next.rawTimeSeconds = null;
  return next;
}
export function invalidateTimelineHover(model: TimelineHoverModel): TimelineHoverModel {
  const next = hideTimelineHover(model);
  next.displayed = null;
  next.source = null;
  ++next.generation;
  return next;
}
export function refreshTimelineHoverComposition(model: TimelineHoverModel, composition: TimelineHoverComposition): TimelineHoverModel {
  const next = structuredClone(model);
  refreshComposition(next, composition);
  return next;
}
