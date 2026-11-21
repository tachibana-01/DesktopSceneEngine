import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createTimelineHover, updateTimelineHover, tickTimelineHover, applyTimelineHoverResult, hideTimelineHover, invalidateTimelineHover, refreshTimelineHoverComposition, representativeTimelineTimestamp, timelineHoverCanvasSize } from '../src/sessionTimelineHover.ts';

const checks = [];
function check(name, test) { try { test(); checks.push({ name, pass: true }); } catch (error) { checks.push({ name, pass: false, error: error.message }); } }
const video = { itemId: 'active-a2', source: 'C:/fixture/active-a2.mp4', mediaType: 'video', duration: 23 };
const image = { itemId: 'active-b1', source: 'C:/fixture/active-b1.png', mediaType: 'image', duration: 10 };
const hover = (seconds = 7, now = 0, model = createTimelineHover(), source = video) => updateTimelineHover(model, source, seconds, now);
const submitted = () => tickTimelineHover(hover(), 75);
const result = (model, request = model.pending, imageId = 'a2:bucket-5000') => applyTimelineHoverResult(model, { request, imageId });
const valid = () => result(submitted());
const freeze = value => { if (value && typeof value === 'object') { Object.freeze(value); for (const child of Object.values(value)) freeze(child); } return value; };

check('Production representative mapping precedes raw hover and clamps EOF', () => {
  for (const [raw, expected] of [[-4, 0], [0, 0], [4999, 0], [5000, 5000], [9999, 5000], [20000, 15000], [21000, 15000]]) assert.equal(representativeTimelineTimestamp(20000, raw), expected);
  assert.equal(representativeTimelineTimestamp(0, 1000), null);
});
check('Committed representative intervals and 120-frame cap are preserved', () => {
  for (const [duration, raw, expected] of [[300000,19999,10000], [600000,29999,15000], [1200000,39999,20000], [1800000,59999,30000], [3600000,119999,60000], [7200000,239999,120000], [36000000,599999,300000]]) assert.equal(representativeTimelineTimestamp(duration, raw), expected);
});
check('Popup canvas preserves logical canvas aspect within 224x140', () => {
  for (const [n,d,w,h] of [[16,9,224,126],[16,10,224,140],[9,16,79,140],[10,7,200,140]]) assert.deepEqual(timelineHoverCanvasSize(n,d), { width:w,height:h });
  assert.deepEqual(timelineHoverCanvasSize(0,9),{width:0,height:0});
});
check('First hover shows Loading with raw timestamp and active source', () => {
  const m=hover(7.53); assert.equal(m.active,true); assert.equal(m.status,'loading'); assert.equal(m.rawTimeSeconds,Math.fround(7.53)); assert.deepEqual(m.source,video); assert.equal(m.pending.representativeTimestampMilliseconds,5000); assert.equal(m.pending.durationMilliseconds,23000); assert.equal(m.pending.timeSeconds,Math.fround(7.53)); assert.equal(m.dueAt,75);
});
check('75ms deterministic single shot emits no early worker submission', () => {
  const first=hover(); assert.equal(tickTimelineHover(first,74).submittedRequests.length,0); const fired=tickTimelineHover(first,75); assert.equal(fired.submittedRequests.length,1); assert.equal(fired.pending.submitted,true); assert.equal(fired.dueAt,null);
});
check('Tick is single shot and never repeats an already submitted request', () => {
  const m=tickTimelineHover(submitted(),1000); assert.equal(m.submittedRequests.length,1); assert.equal(m.imageReads.length,0);
});
check('Same pending representative bucket does not replace identity or deadline', () => {
  const a=hover(6), b=hover(8,40,a); assert.equal(b.pending.serial,a.pending.serial); assert.equal(b.pending.timeSeconds,a.pending.timeSeconds); assert.equal(b.rawTimeSeconds,8); assert.equal(b.dueAt,75); assert.equal(tickTimelineHover(b,75).submittedRequests.length,1);
});
check('Cross bucket replacement keeps original 75ms timer rather than trailing debounce', () => {
  const a=hover(6), b=hover(12,60,a); assert.ok(b.pending.serial>a.pending.serial); assert.equal(b.pending.representativeTimestampMilliseconds,10000); assert.equal(b.dueAt,75); assert.equal(tickTimelineHover(b,74).submittedRequests.length,0); assert.equal(tickTimelineHover(b,75).submittedRequests[0].representativeTimestampMilliseconds,10000);
});
check('Accepted result displays Valid and clears pending without replacing raw timestamp', () => {
  const a=hover(8,90,submitted()), b=result(a); assert.equal(b.status,'valid'); assert.equal(b.displayed.imageId,'a2:bucket-5000'); assert.equal(b.rawTimeSeconds,8); assert.equal(b.pending,null);
});
check('Displayed representative bucket suppresses equivalent new requests', () => {
  const a=valid(), b=hover(9.8,100,a); assert.equal(b.status,'valid'); assert.equal(b.pending,null); assert.equal(b.serial,a.serial); assert.equal(b.rawTimeSeconds,Math.fround(9.8)); assert.equal(tickTimelineHover(b,1000).submittedRequests.length,1);
});
check('Cross bucket retains same source valid frame while replacement is pending', () => {
  const a=valid(), b=hover(12,100,a); assert.equal(b.displayed.imageId,a.displayed.imageId); assert.equal(b.status,'valid'); assert.equal(b.pending.representativeTimestampMilliseconds,10000); assert.equal(b.dueAt,175);
});
check('Returning to displayed bucket cancels unsubmitted request and stops timer', () => {
  const a=hover(12,100,valid()), b=hover(8,120,a); assert.equal(b.pending,null); assert.equal(b.dueAt,null); assert.ok(b.serial>a.serial); assert.equal(b.status,'valid'); assert.equal(tickTimelineHover(b,1000).submittedRequests.length,1);
});
check('Returning to displayed bucket cancels submitted replacement and rejects late result', () => {
  const a=tickTimelineHover(hover(12,100,valid()),175), req=a.pending, b=hover(8,200,a), c=result(b,req,'wrong'); assert.equal(c.displayed.imageId,'a2:bucket-5000'); assert.equal(c.pending,null); assert.equal(c.staleResultsRejected,b.staleResultsRejected+1);
});
check('Latest serial wins when newer bucket completes before older bucket', () => {
  const a=submitted(), old=a.pending, b=tickTimelineHover(hover(12,100,a),175), newest=b.pending; let c=result(b,newest,'newest-bucket'); c=result(c,old,'obsolete-bucket'); assert.equal(c.displayed.imageId,'newest-bucket'); assert.equal(c.staleResultsRejected,1);
});
check('Old serial is rejected even while replacement remains Loading', () => {
  const a=submitted(), old=a.pending, b=hover(12,100,a), c=result(b,old,'wrong'); assert.equal(c.status,'loading'); assert.equal(c.displayed,null); assert.equal(c.pending.serial,b.pending.serial);
});
check('Source change clears old displayed frame and generation before new result', () => {
  const a=valid(), b=hover(7,100,a,{...video,itemId:'new',source:'new.mp4'}); assert.equal(b.displayed,null); assert.equal(b.status,'loading'); assert.ok(b.generation>a.generation); assert.equal(b.pending.source.itemId,'new');
});
check('Full source identity includes ItemId, path, and media kind independently', () => {
  for (const changed of [{...video,itemId:'different'}, {...video,source:'different.mp4'}, {...video,mediaType:'image'}]) { const a=valid(), b=hover(7,100,a,changed); assert.ok(b.generation>a.generation); assert.equal(b.displayed,null); }
});
check('Old source rejected independently of matching generation and serial', () => {
  const a=submitted(), req={...a.pending,source:{...video,source:'other.mp4'}}, b=result(a,req,'wrong'); assert.equal(b.displayed,null); assert.equal(b.staleResultsRejected,1);
});
check('Old generation rejected independently of matching source and serial', () => {
  const a=submitted(), b=result(a,{...a.pending,generation:a.generation-1},'wrong'); assert.equal(b.displayed,null); assert.equal(b.staleResultsRejected,1);
});
check('Leave hides popup, invalidates pending serial, and late result never reopens', () => {
  const a=submitted(), req=a.pending, b=hideTimelineHover(a), c=result(b,req,'wrong'); assert.equal(c.active,false); assert.equal(c.rawTimeSeconds,null); assert.equal(c.pending,null); assert.equal(c.dueAt,null); assert.ok(c.serial>a.serial); assert.equal(c.displayed,null);
});
check('Leave preserves valid raw frame cache for same source and bucket reentry', () => {
  const a=valid(), b=hideTimelineHover(a), c=hover(8,300,b); assert.equal(b.displayed.imageId,a.displayed.imageId); assert.equal(c.active,true); assert.equal(c.pending,null); assert.equal(c.status,'valid'); assert.equal(c.submittedRequests.length,1);
});
check('Press/drag hide has same pending invalidation as leave', () => {
  const a=submitted(), b=hideTimelineHover(a), c=result(b,a.pending,'wrong'); assert.equal(c.active,false); assert.equal(c.displayed,null); assert.equal(c.staleResultsRejected,1);
});
check('Lifecycle/source invalidation drops source/cache and fences generation', () => {
  const a=valid(), b=invalidateTimelineHover(a); assert.equal(b.active,false); assert.equal(b.source,null); assert.equal(b.displayed,null); assert.ok(b.generation>a.generation); assert.equal(b.dueAt,null);
});
check('Same source after lifecycle generation invalidation still rejects old result', () => {
  const a=submitted(), b=hover(7,100,invalidateTimelineHover(a)), c=result(b,a.pending,'wrong'); assert.equal(c.active,true); assert.equal(c.displayed,null); assert.equal(c.pending.generation,b.generation); assert.equal(c.staleResultsRejected,1);
});
check('Invalid/no active source disables hover and invalidates existing request', () => {
  for (const source of [null,{...video,source:''},{...video,duration:0},{...video,duration:NaN},{...video,duration:Infinity}]) { const a=updateTimelineHover(submitted(),source,7,100); assert.equal(a.active,false); assert.equal(a.pending,null); assert.equal(a.source,null); }
});
check('Unavailable result clears retained old frame and remains visible as Unavailable', () => {
  const a=tickTimelineHover(hover(12,100,valid()),175), b=result(a,a.pending,null); assert.equal(b.active,true); assert.equal(b.status,'unavailable'); assert.equal(b.displayed,null); assert.equal(b.pending,null); assert.equal(b.rawTimeSeconds,12);
});
check('Unavailable bucket can be requested again on later hover', () => {
  const a=result(submitted(),undefined,null), b=hover(7,100,a); assert.equal(b.status,'loading'); assert.ok(b.pending.serial>a.serial); assert.equal(b.dueAt,175);
});
check('Presentation refresh recomposes retained frame without changing decode identity', () => {
  const a=valid(), b=refreshTimelineHoverComposition(a,{key:'native-new',canvasNumerator:9,canvasDenominator:16}); assert.deepEqual(b.canvasSize,{width:79,height:140}); assert.ok(b.presentationGeneration>a.presentationGeneration); assert.equal(b.generation,a.generation); assert.equal(b.serial,a.serial); assert.equal(b.displayed.imageId,a.displayed.imageId); assert.equal(b.displayed.presentationGeneration,b.presentationGeneration); assert.deepEqual(b.submittedRequests,a.submittedRequests); assert.equal(b.pending,null);
});
check('Presentation refresh does not cancel pending decode or reject its valid result', () => {
  const a=submitted(), b=refreshTimelineHoverComposition(a,{key:'crop-or-layout',canvasNumerator:16,canvasDenominator:10}), c=result(b,a.pending,'current'); assert.equal(c.status,'valid'); assert.equal(c.displayed.presentationGeneration,c.presentationGeneration); assert.deepEqual(c.canvasSize,{width:224,height:140}); assert.equal(c.submittedRequests.length,1);
});
check('Unchanged composition has no unnecessary generation or work', () => {
  const a=valid(), b=refreshTimelineHoverComposition(a,a.composition); assert.deepEqual(b,a);
});
check('Hidden composition refresh defers retained frame recomposition until hover resumes', () => {
  const a=hideTimelineHover(valid()), b=refreshTimelineHoverComposition(a,{key:'hidden-update',canvasNumerator:9,canvasDenominator:16}); assert.equal(b.compositionDirty,true); assert.notEqual(b.displayed.presentationGeneration,b.presentationGeneration); const c=hover(8,100,b); assert.equal(c.compositionDirty,false); assert.equal(c.displayed.presentationGeneration,c.presentationGeneration); assert.equal(c.submittedRequests.length,1);
});
check('Image uses same 75ms timer but synchronous provider branch without worker submit', () => {
  const a=hover(2,0,createTimelineHover(),image); assert.equal(a.status,'loading'); assert.equal(tickTimelineHover(a,74).imageReads.length,0); const b=tickTimelineHover(a,75,'image-source-frame'); assert.equal(b.status,'valid'); assert.equal(b.displayed.imageId,'image-source-frame'); assert.equal(b.imageReads.length,1); assert.equal(b.submittedRequests.length,0); assert.equal(b.pending,null); assert.equal(b.displayed.request.representativeTimestampMilliseconds,null);
});
check('Image frame content remains valid for every synthetic hover time', () => {
  const a=tickTimelineHover(hover(2,0,createTimelineHover(),image),75,'static-image'), b=hover(9,100,a,image); assert.equal(b.displayed.imageId,'static-image'); assert.equal(b.rawTimeSeconds,9); assert.equal(b.pending,null); assert.equal(b.imageReads.length,1);
});
check('Image pending hover replaces timestamp/serial without restarting first timer', () => {
  const a=hover(2,0,createTimelineHover(),image), b=hover(8,60,a,image); assert.ok(b.serial>a.serial); assert.equal(b.dueAt,75); const c=tickTimelineHover(b,75,'static'); assert.equal(c.imageReads.length,1); assert.equal(c.imageReads[0].timeSeconds,8);
});
check('Image provider unavailable branch displays Unavailable without worker request', () => {
  const a=tickTimelineHover(hover(2,0,createTimelineHover(),image),75,null); assert.equal(a.status,'unavailable'); assert.equal(a.displayed,null); assert.equal(a.submittedRequests.length,0); assert.equal(a.imageReads.length,1);
});
check('Duration is request snapshot and not decode source identity', () => {
  const a=valid(), b=hover(8,100,a,{...video,duration:24}); assert.equal(b.generation,a.generation); assert.equal(b.pending,null); assert.equal(b.displayed.imageId,a.displayed.imageId);
});
check('Model transitions never mutate frozen inputs or source', () => {
  const initial=freeze(createTimelineHover()), source=freeze({...video}), before=JSON.stringify(initial), a=hover(7,0,initial,source); assert.equal(JSON.stringify(initial),before); const frozen=freeze(a), b=tickTimelineHover(frozen,75); result(freeze(b)); hideTimelineHover(frozen); invalidateTimelineHover(frozen); refreshTimelineHoverComposition(frozen,{key:'new',canvasNumerator:9,canvasDenominator:16}); assert.equal(a.active,true);
});

const status=checks.every(x=>x.pass)?'PASS':'FAIL';
const report={suite:'MainWindow Phase D hover model',checks:checks.length,passed:checks.filter(x=>x.pass).length,status,results:checks};
const directory=fileURLToPath(new URL('../node_modules/.cache/session-phase-d/',import.meta.url)); mkdirSync(directory,{recursive:true}); writeFileSync(`${directory}/hover-model-${process.env.DSE_CHECK_STAGE||'green'}.json`,`${JSON.stringify(report,null,2)}\n`); console.log(JSON.stringify(report,null,2)); if(status!=='PASS')process.exitCode=1;
