import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { transformSync } from 'rolldown/utils';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { createSessionFixture } from '../src/sessionFixtures.ts';
import { TimelinePreviewMock } from '../src/sessionTimelinePreviewMock.ts';

// Render the actual component without a browser. Only the clock/driver instance
// and translation hook are supplied; source geometry comes from production JSX.
const directory = new URL('../node_modules/.cache/semantic-gap-closure/', import.meta.url);
mkdirSync(directory, { recursive: true });
const harness = new URL('f06-render-harness.mjs', directory);
writeFileSync(harness, 'export class TimelinePreviewMock { constructor() { return globalThis.__DSE_F06_DRIVER; } }\nexport const useSessionText = () => value => value;\n');
const componentUrl = new URL('../src/SessionTimeline.tsx', import.meta.url);
const transformed = transformSync(fileURLToPath(componentUrl), readFileSync(componentUrl, 'utf8'));
assert.equal(transformed.errors.length, 0, 'The actual Timeline component must transpile for the runtime composition check');
let code = transformed.code;
code = code.replace(/import ['"]\.\/sessionTimeline\.css['"];?/, '')
  .replace(/from ['"]\.\/sessionTimelinePreviewMock\.ts['"]/, `from ${JSON.stringify(harness.href)}`)
  .replace(/from ['"]\.\/sessionLanguage['"]/, `from ${JSON.stringify(harness.href)}`)
  .replace(/from (['"])(\.\/[^'"]+)\1/g, (_, quote, path) => `from ${JSON.stringify(new URL(path.endsWith('.ts') ? path : path + '.ts', componentUrl).href)}`);
const renderedModule = new URL('f06-SessionTimeline.mjs', directory);
writeFileSync(renderedModule, code);
globalThis.window = { location: { search: '?timelineClock=manual' } };
const { SessionTimeline } = await import(renderedModule.href + '?run=' + Date.now());
// The first 127 checks were run against the unmodified component to prove RED.
// Additional boundary checks below exercise the new pure geometry port directly.
const { resolveHoverComposition, createHoverMockFrame } = await import('../src/sessionHoverComposition.ts');
const checks = [];
function check(name, test) {
  try { test(); checks.push({ name, pass: true }); }
  catch (error) { checks.push({ name, pass: false, error: String(error) }); }
}
const near = (actual, expected, label) => assert(Math.abs(actual - expected) < 0.000001, `${label}: expected ${expected}, received ${actual}`);
const rectNear = (actual, expected, label) => ['x', 'y', 'width', 'height'].forEach((key, index) => near(actual[key], expected[index], `${label}.${key}`));
function attributes(tag) { return Object.fromEntries([...tag.matchAll(/([\w-]+)="([^"]*)"/g)].map(match => [match[1], match[2]])); }
function numbers(text) { return (text.match(/[-+]?(?:\d*\.)?\d+(?:e[-+]?\d+)?/gi) ?? []).map(Number); }

function render(presentation, aspect = [16, 9], scenario = 'selected-not-active', mediaType = 'video') {
  const state = createSessionFixture(scenario);
  state.project.canvasAspect = aspect;
  const item = state.project.playlists.flatMap(playlist => playlist.items).find(item => item.id === state.activeItemId);
  item.kind = mediaType; item.presentation = structuredClone(presentation);
  state.runtime.items.find(bound => bound.id === item.id).presentation = structuredClone(presentation);
  const driver = new TimelinePreviewMock('normal', true, () => {});
  driver.compose({ key: JSON.stringify(presentation), canvasNumerator: aspect[0], canvasDenominator: aspect[1] });
  driver.hover({ itemId: item.id, source: item.source, mediaType, duration: item.duration }, 7);
  driver.advance(315);
  globalThis.__DSE_F06_DRIVER = driver;
  const html = renderToStaticMarkup(React.createElement(SessionTimeline, { mock: { state, scenario, timelineEpoch: 0, timelineDragEpoch: 0 } }));
  driver.dispose();
  return { html, canvas: driver.model.canvasSize, item, state, driver };
}

// Read the actual SVG mapping. The baseline path reads its existing painted
// rectangle and translate, so RED is wrong runtime geometry, never missing API.
function mapping(rendered) {
  const { html, canvas } = rendered;
  const transformTag = html.match(/<g\b[^>]*data-hover-source-frame="true"[^>]*>/)?.[0];
  if (!transformTag) {
    const translate = numbers(html.match(/<g transform="translate\(([^)]*)\)/)?.[1] ?? '0 0');
    const rectangle = attributes(html.match(/<rect\b[^>]*fill="hsl\([^"]*"[^>]*>/)?.[0] ?? '');
    return { sourceUv: { x: 0, y: 0, width: 1, height: 1 }, destination: {
      x: (Number(rectangle.x) + translate[0]) / canvas.width,
      y: (Number(rectangle.y) + translate[1]) / canvas.height,
      width: Number(rectangle.width) / canvas.width, height: Number(rectangle.height) / canvas.height,
    } };
  }
  const source = attributes(transformTag);
  const [sx, , , sy, tx, ty] = numbers(source.transform);
  const clip = attributes(html.match(/<rect\b[^>]*data-hover-destination="true"[^>]*>/)[0]);
  const x = Number(clip.x), y = Number(clip.y), width = Number(clip.width), height = Number(clip.height);
  return { sourceUv: {
    x: (x - tx) / sx / Number(source['data-source-width']), y: (y - ty) / sy / Number(source['data-source-height']),
    width: width / sx / Number(source['data-source-width']), height: height / sy / Number(source['data-source-height']),
  }, destination: { x: x / canvas.width, y: y / canvas.height, width: width / canvas.width, height: height / canvas.height } };
}
const presentation = (mode, customAspect = [1, 1], crop = [0, 0, 1, 1], layout = [0, 0, 1, 1]) => ({
  crop: Object.fromEntries(['x', 'y', 'width', 'height'].map((key, index) => [key, crop[index]])),
  layout: Object.fromEntries(['positionX', 'positionY', 'scaleX', 'scaleY'].map((key, index) => [key, layout[index]])),
  mediaPresentation: { mode, customAspect: { numerator: customAspect[0], denominator: customAspect[1] } },
});

// Rational goldens use the popup's rounded pixel size, as the native composer
// does (4:3 gives 187x140). They are independent of the implementation module.
const full = [0, 0, 1, 1], wideCrop = [.125, 0, .75, 1], arbitraryCrop = [.05, 0, .9, 1];
const golden = [
  [[16, 9], 'native', [1, 1], full, full],
  [[16, 9], 'auto-fill', [1, 1], full, full],
  [[16, 9], 'custom', [16, 9], full, full],
  [[16, 9], 'custom', [4, 3], wideCrop, [.125, 0, .75, 1]],
  [[16, 9], 'custom', [8, 5], arbitraryCrop, [.05, 0, .9, 1]],
  [[4, 3], 'native', [1, 1], full, [0, 557 / 4480, 1, 1683 / 2240]],
  [[4, 3], 'auto-fill', [1, 1], [557 / 4480, 0, 1683 / 2240, 1], full],
  [[4, 3], 'custom', [16, 9], full, [0, 557 / 4480, 1, 1683 / 2240]],
  [[4, 3], 'custom', [4, 3], wideCrop, [1 / 1122, 0, 560 / 561, 1]],
  [[4, 3], 'custom', [8, 5], arbitraryCrop, [0, 37 / 448, 1, 187 / 224]],
  [[1, 1], 'native', [1, 1], full, [0, .21875, 1, .5625]],
  [[1, 1], 'auto-fill', [1, 1], [.21875, 0, .5625, 1], full],
  [[1, 1], 'custom', [16, 9], full, [0, .21875, 1, .5625]],
  [[1, 1], 'custom', [4, 3], wideCrop, [0, .125, 1, .75]],
  [[1, 1], 'custom', [8, 5], arbitraryCrop, [0, .1875, 1, .625]],
];
for (const [aspect, mode, custom, uv, baseline] of golden) {
  for (const layout of [[0, 0, 1, 1], [0, 0, .5, .75], [0, 0, 1.5, 2], [.2, 0, 1, 1], [-.2, 0, 1, 1], [0, .2, 1, 1], [0, -.2, 1, 1], [.1, -.2, 1.25, .8]]) {
    check(`${mode} ${custom.join(':')} on Canvas ${aspect.join(':')} layout ${layout}`, () => {
      const actual = mapping(render(presentation(mode, custom, full, layout), aspect));
      rectNear(actual.sourceUv, uv, 'sourceUv');
      rectNear(actual.destination, [baseline[0] + baseline[2] * (1 - layout[2]) / 2 + layout[0], baseline[1] + baseline[3] * (1 - layout[3]) / 2 - layout[1], baseline[2] * layout[2], baseline[3] * layout[3]], 'destination');
    });
  }
}
for (const [mode, custom, crop, layout, uv, dest] of [
  ['native', [1, 1], [0, 0, .5, 1], [.1, -.1, 1.2, .8], [0, 0, .5, 1], [.3, .2, .6, .8]],
  ['auto-fill', [1, 1], [.1, .2, .6, .5], [.1, -.1, .5, 1.5], [.15, .2, .5, .5], [.35, -.15, .5, 1.5]],
  ['custom', [1, 1], [.2, .1, .4, .4], [.25, -.1, 1.5, .75], [.2875, .1, .225, .4], [.328125, .225, .84375, .75]],
]) check(`${mode} asymmetric Crop + Scale + Position production golden`, () => {
  const actual = mapping(render(presentation(mode, custom, crop, layout)));
  rectNear(actual.sourceUv, uv, 'sourceUv'); rectNear(actual.destination, dest, 'destination');
});
check('Native portrait mock uses its source dimensions before Canvas fitting', () => {
  const actual = mapping(render(presentation('native'), [16, 9], 'selected-not-active', 'image'));
  rectNear(actual.destination, [.2890625, 0, .421875, 1], 'portrait destination');
});
check('Accepted frame is asymmetric and identifies full source, edges, center and dimensions', () => {
  const { html, item } = render(presentation('native'));
  assert(html.includes('data-source-width="320"') && html.includes('data-source-height="180"'));
  for (const marker of ['left', 'right', 'top', 'bottom', 'center']) assert(html.includes(`data-source-marker="${marker}"`), marker);
  assert(html.includes(item.id) && html.includes(item.source));
});
check('Selected Item presentation cannot alter the active frame or Canvas composition', () => {
  const active = presentation('custom', [4, 3], [.1, .2, .6, .5], [.15, -.1, .8, 1.2]);
  const different = render(active);
  assert.notEqual(different.state.selectedItemId, different.state.activeItemId);
  const same = render(active, [16, 9], 'selected-video-active');
  assert.deepEqual(mapping(different), mapping(same));
  assert.equal(different.driver.submissions, 1);
});
check('Canvas background and destination clipping remain explicit in the rendered output', () => {
  const { html } = render(presentation('native', [1, 1], full, [.2, -.2, 2, 2]));
  assert.match(html, /<rect[^>]*fill="#1a1a1a"/);
  assert.match(html, /<clipPath[^>]*clipPathUnits="userSpaceOnUse"/);
  assert.match(html, /clip-path="url\(#[^)]+\)"/);
});
for (const [name, width, height, canvasWidth, canvasHeight, input] of [
  ['zero source width', 0, 180, 224, 126, presentation('native')],
  ['zero source height', 320, 0, 224, 126, presentation('native')],
  ['zero Canvas width', 320, 180, 0, 126, presentation('native')],
  ['zero Canvas height', 320, 180, 224, 0, presentation('native')],
  ['negative crop', 320, 180, 224, 126, presentation('native', [1, 1], [-.1, 0, .5, 1])],
  ['crop outside source', 320, 180, 224, 126, presentation('auto-fill', [1, 1], [.8, 0, .3, 1])],
  ['zero crop height', 320, 180, 224, 126, presentation('custom', [4, 3], [0, 0, 1, 0])],
  ['non-finite crop', 320, 180, 224, 126, presentation('native', [1, 1], [NaN, 0, 1, 1])],
  ['non-finite position', 320, 180, 224, 126, presentation('native', [1, 1], full, [Infinity, 0, 1, 1])],
  ['zero scale', 320, 180, 224, 126, presentation('auto-fill', [1, 1], full, [0, 0, 0, 1])],
  ['negative scale', 320, 180, 224, 126, presentation('native', [1, 1], full, [0, 0, 1, -.5])],
  ['invalid custom aspect', 320, 180, 224, 126, presentation('custom', [0, 1])],
  ['non-integral custom aspect', 320, 180, 224, 126, presentation('custom', [1.5, 1])],
]) check(`Geometry rejects ${name}`, () => assert.equal(resolveHoverComposition(width, height, canvasWidth, canvasHeight, input), null));
check('Native and Auto Fill ignore an irrelevant invalid Custom aspect as production does', () => {
  for (const mode of ['native', 'auto-fill']) assert(resolveHoverComposition(320, 180, 224, 126, presentation(mode, [0, 0])));
});
check('Literal float32 crop validation accepts the same rounded boundary as Domain float fields', () => {
  assert(resolveHoverComposition(320, 180, 224, 126, presentation('native', [1, 1], [.9, 0, .1, 1])));
});
check('Arbitrary portrait Custom aspect combines asymmetric Crop and Layout without mutating inputs', () => {
  const input = presentation('custom', [7, 11], [.15, .2, .5, .4], [-.1, .2, 1.4, .6]);
  const before = JSON.stringify(input);
  const actual = resolveHoverComposition(240, 320, 224, 126, input);
  rectNear(actual.sourceUv, [.230303030303, .2, .339393939394, .4], 'portrait custom sourceUv');
  rectNear(actual.destination, [.149431818182, 0, .501136363636, .6], 'portrait custom destination');
  assert.equal(JSON.stringify(input), before);
});
check('Synthetic frame dimensions and source identity are deterministic and source-specific', () => {
  const source = { itemId: 'a', source: 'A.mp4', mediaType: 'video', duration: 23 };
  const original = JSON.stringify(source), frame = createHoverMockFrame(source, 'a:5000');
  assert.deepEqual(createHoverMockFrame(source, 'a:5000'), frame);
  assert.notEqual(createHoverMockFrame({ ...source, source: 'B.mp4' }, 'a:5000').identity, frame.identity);
  assert.notEqual(createHoverMockFrame({ ...source, itemId: 'b' }, 'a:5000').identity, frame.identity);
  assert.deepEqual([frame.width, frame.height], [320, 180]);
  const portrait = createHoverMockFrame({ ...source, mediaType: 'image' }, 'still');
  assert.deepEqual([portrait.width, portrait.height], [240, 320]);
  assert.equal(JSON.stringify(source), original);
});
check('Retained raw frame recomposes for presentation and Canvas without a new decode identity', () => {
  const state = createSessionFixture('selected-not-active'), item = state.runtime.items.find(item => item.id === state.activeItemId);
  const source = { itemId: item.id, source: item.source, mediaType: item.kind, duration: item.duration };
  const driver = new TimelinePreviewMock('normal', true, () => {});
  driver.hover(source, 7); driver.advance(315);
  const identity = structuredClone(driver.model.displayed), generation = driver.model.generation, serial = driver.model.serial;
  const results = [];
  for (const [mode, aspect] of [['native', [16, 9]], ['auto-fill', [1, 1]], ['custom', [4, 3]]]) {
    item.presentation = presentation(mode, [8, 5], [.1, .2, .6, .5], [.15, -.1, .8, 1.2]);
    state.project.canvasAspect = aspect;
    driver.compose({ key: JSON.stringify(item.presentation), canvasNumerator: aspect[0], canvasDenominator: aspect[1] });
    globalThis.__DSE_F06_DRIVER = driver;
    const html = renderToStaticMarkup(React.createElement(SessionTimeline, { mock: { state, scenario: 'selected-not-active' } }));
    results.push(mapping({ html, canvas: driver.model.canvasSize }));
    assert.equal(driver.submissions, 1); assert.equal(driver.model.generation, generation); assert.equal(driver.model.serial, serial);
    assert.equal(driver.model.displayed.imageId, identity.imageId); assert.deepEqual(driver.model.displayed.request, identity.request);
  }
  assert.notDeepEqual(results[0], results[1]); assert.notDeepEqual(results[1], results[2]);
  driver.dispose();
});
const report = { suite: 'F06 active Timeline Canvas composition', checks: checks.length, passed: checks.filter(check => check.pass).length, results: checks };
report.status = report.passed === report.checks ? 'PASS' : 'FAIL';
const stage = process.env.DSE_CHECK_STAGE || 'green';
writeFileSync(new URL(`f06-${stage}.json`, directory), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report, null, 2));
delete globalThis.__DSE_F06_DRIVER;
if (report.status !== 'PASS') process.exitCode = 1;
