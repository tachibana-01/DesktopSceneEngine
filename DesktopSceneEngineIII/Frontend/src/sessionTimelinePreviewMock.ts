import {
  createTimelineHover, updateTimelineHover, tickTimelineHover, applyTimelineHoverResult,
  hideTimelineHover, invalidateTimelineHover, refreshTimelineHoverComposition,
} from './sessionTimelineHover.ts';
import type { TimelineHoverModel, TimelineHoverSource, TimelineHoverRequest } from './sessionTimelineHover.ts';

export type ThumbnailMockMode = 'normal' | 'loading' | 'unavailable' | 'out-of-order';

// Controlled local completion queue. It never reads media or uses the network.
// The same driver accepts a manual clock for browser checks and the DEV lab.
export class TimelinePreviewMock {
  model: TimelineHoverModel = createTimelineHover();
  now: number;
  readonly manual: boolean;
  private jobs: { request: TimelineHoverRequest; due: number; imageId: string | null }[] = [];
  submissions = 0;
  imageReads = 0;
  private timer: ReturnType<typeof setTimeout> | undefined;
  private disposed = false;
  readonly mode: ThumbnailMockMode;
  private changed: () => void;
  constructor(mode: ThumbnailMockMode, manual: boolean, changed: () => void) {
    this.mode = mode; this.changed = changed;
    this.manual = manual;
    this.now = manual ? 0 : performance.now();
  }
  private collect() {
    for (const request of this.model.submittedRequests) {
      ++this.submissions;
      const delay = this.mode === 'loading' ? Infinity : this.mode === 'out-of-order' ? (this.submissions % 2 ? 600 : 120) : 240;
      this.jobs.push({ request, due: this.now + delay,
        imageId: this.mode === 'unavailable' ? null : request.source.itemId + ':' + request.representativeTimestampMilliseconds });
    }
    this.imageReads += this.model.imageReads.length;
    this.model = { ...this.model, submittedRequests: [], imageReads: [] };
    // Keep a bounded collection of deliberately late mock completions. Production
    // has a latest-request worker, not this diagnostic out-of-order queue.
    if (this.jobs.length > 64) this.jobs = this.jobs.slice(-64);
  }
  private nextDue() { return Math.min(this.model.dueAt ?? Infinity, ...this.jobs.map(job => job.due)); }
  private schedule() {
    clearTimeout(this.timer);
    if (this.disposed || this.manual || !Number.isFinite(this.nextDue())) return;
    this.timer = setTimeout(() => this.advanceTo(performance.now()), Math.max(0, this.nextDue() - performance.now()));
  }
  private notify() { if (!this.disposed) { this.schedule(); this.changed(); } }
  private advanceTo(target: number) {
    if (this.disposed) return;
    for (let count = 0; count < 1000 && this.nextDue() <= target; count++) {
      this.now = this.nextDue();
      this.model = tickTimelineHover(this.model, this.now, this.mode === 'unavailable' ? null : undefined);
      this.collect();
      const ready = this.jobs.filter(job => job.due <= this.now);
      this.jobs = this.jobs.filter(job => job.due > this.now);
      for (const job of ready) this.model = applyTimelineHoverResult(this.model, job);
    }
    this.now = target;
    this.notify();
  }
  private synchronizeClock() { if (!this.manual) this.advanceTo(performance.now()); }
  hover(source: TimelineHoverSource | null, seconds: number) {
    this.synchronizeClock();
    this.model = updateTimelineHover(this.model, source, seconds, this.now); this.collect(); this.notify();
  }
  hide() { this.model = hideTimelineHover(this.model); this.notify(); }
  invalidate() { this.model = invalidateTimelineHover(this.model); this.notify(); }
  compose(composition: { key: string; canvasNumerator: number; canvasDenominator: number }) {
    this.model = refreshTimelineHoverComposition(this.model, composition); this.notify();
  }
  advance(milliseconds: number) { if (Number.isFinite(milliseconds) && milliseconds >= 0) this.advanceTo(this.now + milliseconds); }
  resolve(which: 'oldest' | 'latest') {
    const job = which === 'latest' ? this.jobs.pop() : this.jobs.shift();
    if (job) this.model = applyTimelineHoverResult(this.model, job);
    this.notify();
  }
  dispose() { this.disposed = true; clearTimeout(this.timer); this.jobs = []; }
}
