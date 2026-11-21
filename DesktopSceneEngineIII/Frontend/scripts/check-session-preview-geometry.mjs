import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { FRAME_MARGIN, fitCanvasInsidePreviewViewport, previewGeometry } from '../src/sessionPreviewGeometry.ts';

const checks = [];
function check(name, test) {
  try { test(); checks.push({ name, pass: true }); }
  catch (error) { checks.push({ name, pass: false, error: error.message }); }
}
const rect = (x, y, width, height) => ({ x, y, width, height });
const size = (width, height) => ({ width, height });
const fit = (width, height, aspect = [16, 9]) => fitCanvasInsidePreviewViewport(size(width, height), aspect);
const geometry = (width, height, aspect = [16, 9]) => previewGeometry(size(width, height), aspect);
const zero = rect(0, 0, 0, 0);

// Golden outputs copied from PreviewViewportHostTest, independently of the TS implementation.
for (const [name, viewport, aspect, expected] of [
  ['wide 16:9', [3000, 1000], [16, 9], rect(611, 0, 1777, 1000)],
  ['square 16:9', [1000, 1000], [16, 9], rect(0, 219, 1000, 562)],
  ['matching 16:9', [1600, 900], [16, 9], rect(0, 0, 1600, 900)],
  ['matching 16:10', [1600, 1000], [16, 10], rect(0, 0, 1600, 1000)],
  ['16:10 inside 16:9', [1600, 900], [16, 10], rect(80, 0, 1440, 900)],
  ['square 16:10', [1000, 1000], [16, 10], rect(0, 187, 1000, 625)],
  ['resized 16:10', [3000, 1000], [16, 10], rect(700, 0, 1600, 1000)],
  ['tiny bounded zero-height result', [1, 1], [16, 9], rect(0, 0, 1, 0)],
]) check(`Production fit golden: ${name}`, () => assert.deepEqual(fit(...viewport, aspect), expected));

check('Permanent four-pixel chrome margin is production CanvasBoundaryRingThickness', () => assert.equal(FRAME_MARGIN, 4));
for (const [name, viewport, aspect, frame, native] of [
  ['16:9 square', [1000, 1000], [16, 9], rect(0, 217, 1000, 566), rect(4, 221, 992, 558)],
  ['16:9 landscape', [1600, 900], [16, 9], rect(3, 0, 1593, 900), rect(7, 4, 1585, 892)],
  ['16:10 landscape', [1600, 900], [16, 10], rect(82, 0, 1435, 900), rect(86, 4, 1427, 892)],
  ['4:3', [1000, 1000], [4, 3], rect(0, 124, 1000, 752), rect(4, 128, 992, 744)],
  ['1:1', [1000, 1000], [1, 1], rect(0, 0, 1000, 1000), rect(4, 4, 992, 992)],
  ['ultrawide', [1000, 1000], [32, 9], rect(0, 356, 1000, 287), rect(4, 360, 992, 279)],
  ['portrait', [1000, 1000], [9, 16], rect(217, 0, 566, 1000), rect(221, 4, 558, 992)],
  ['one native pixel remains valid', [9, 9], [1, 1], rect(0, 0, 9, 9), rect(4, 4, 1, 1)],
  ['two by one native pixels', [10, 10], [16, 9], rect(0, 0, 10, 9), rect(4, 4, 2, 1)],
  ['odd slack stays bottom/right', [17, 13], [16, 9], rect(0, 0, 16, 13), rect(4, 4, 8, 5)],
]) check(`Chrome-aware geometry: ${name}`, () => {
  const actual = geometry(...viewport, aspect);
  assert.equal(actual.visible, true); assert.deepEqual(actual.frame, frame); assert.deepEqual(actual.native, native);
  assert.deepEqual(actual.surfaceInFrame, rect(4, 4, native.width, native.height));
});

check('Zero/negative raw viewports and invalid aspect produce an empty fit', () => {
  for (const viewport of [[0, 100], [100, 0], [-1, 100], [100, -1]]) assert.deepEqual(fit(...viewport), zero);
  for (const aspect of [[0, 1], [1, 0], [-1, 1], [1, -1]]) assert.deepEqual(fit(100, 100, aspect), zero);
});
check('Empty chrome-adjusted fit hides the whole frame instead of inventing a minimum extent', () => {
  for (const viewport of [[0, 0], [0, 100], [100, 0], [1, 1], [8, 100], [100, 8], [9, 9], [9, 100], [-1, 100]]) {
    const actual = geometry(...viewport);
    assert.equal(actual.visible, false, JSON.stringify(viewport));
    assert.deepEqual(actual.frame, zero); assert.deepEqual(actual.native, zero); assert.deepEqual(actual.surfaceInFrame, zero);
  }
});
check('Nonfinite/invalid CSS measurements cannot produce a render target', () => {
  for (const bad of [NaN, Infinity, -Infinity, -1, 2147483648]) {
    assert.equal(geometry(bad, 100).visible, false); assert.equal(geometry(100, bad).visible, false);
  }
  for (const aspect of [[NaN, 9], [16, Infinity], [1.5, 1], [1, 1.5], [2147483648, 1]]) assert.equal(geometry(100, 100, aspect).visible, false);
});
check('Measured CSS viewport is quantized down to integer logical pixels before fit', () => {
  assert.deepEqual(geometry(1000.9, 1000.25), geometry(1000, 1000));
  assert.equal(geometry(8.99, 10.99, [1, 1]).visible, false);
  assert.deepEqual(geometry(9.99, 9.99, [1, 1]), geometry(9, 9, [1, 1]));
});
check('Unreduced positive aspect components have equivalent geometry', () => {
  assert.deepEqual(geometry(1920, 1080, [1920, 1080]), geometry(1920, 1080, [16, 9]));
  assert.deepEqual(geometry(700, 500, [32, 20]), geometry(700, 500, [16, 10]));
});
check('Exact integer products preserve signed-int maximum ratio precision', () => {
  assert.deepEqual(fit(2147483647, 2147483646, [2147483647, 2147483646]), rect(0, 0, 2147483647, 2147483646));
  assert.deepEqual(fit(2147483646, 2147483647, [2147483647, 2147483646]), rect(0, 1, 2147483646, 2147483645));
});
check('Canvas aspect alone owns fit; Display metrics are not a geometry input', () => {
  const viewport = Object.freeze(size(1000, 700)); const canvas = Object.freeze([16, 9]);
  const before = JSON.stringify([viewport, canvas]); const actual = previewGeometry(viewport, canvas);
  assert.equal(actual.visible, true); assert.equal(JSON.stringify([viewport, canvas]), before);
  assert.deepEqual(previewGeometry({ ...viewport, displayWidth: 2560, displayHeight: 1600 }, canvas), actual);
});
check('Highlight toggling cannot change geometry because it is a paint-only chrome input', () => {
  const viewport = size(1000, 700); const before = previewGeometry(viewport, [16, 9]);
  for (const highlighted of [true, false, true, false]) assert.deepEqual(previewGeometry({ ...viewport, highlighted }, [16, 9]), before);
});
check('All supported fixture ratios remain centered/bounded with a disjoint four-pixel chrome ring', () => {
  for (const aspect of [[16, 9], [16, 10], [4, 3], [1, 1], [32, 9], [9, 16], [2147483647, 1], [1, 2147483647]]) {
    for (const [width, height] of [[9, 9], [10, 10], [17, 13], [99, 80], [319, 241], [1000, 700], [3000, 1000]]) {
      const actual = geometry(width, height, aspect); if (!actual.visible) continue;
      const { frame, native, surfaceInFrame } = actual;
      for (const value of [...Object.values(frame), ...Object.values(native)]) assert(Number.isInteger(value) && value >= 0);
      assert(frame.x + frame.width <= width); assert(frame.y + frame.height <= height);
      assert.equal(native.x - frame.x, 4); assert.equal(native.y - frame.y, 4);
      assert.equal(frame.width - native.width, 8); assert.equal(frame.height - native.height, 8);
      assert.equal(surfaceInFrame.width, native.width); assert.equal(surfaceInFrame.height, native.height);
      assert(native.width > 0 && native.height > 0);
      assert(Math.abs((width - frame.width - frame.x) - frame.x) <= 1);
      assert(Math.abs((height - frame.height - frame.y) - frame.y) <= 1);
    }
  }
});

const status = checks.every(check => check.pass) ? 'PASS' : 'FAIL';
const result = { suite: 'MainWindow Phase E Preview geometry projection', checks: checks.length, passed: checks.filter(check => check.pass).length, status, results: checks };
const directory = fileURLToPath(new URL('../node_modules/.cache/session-phase-e/', import.meta.url));
mkdirSync(directory, { recursive: true });
const stage = process.env.DSE_CHECK_STAGE || 'green';
writeFileSync(`${directory}/geometry-${stage}.json`, `${JSON.stringify(result, null, 2)}\n`);
writeFileSync(`${directory}/geometry-${stage}.log`, `${JSON.stringify(result, null, 2)}\n`);
console.log(JSON.stringify(result, null, 2));
if (status !== 'PASS') process.exitCode = 1;
