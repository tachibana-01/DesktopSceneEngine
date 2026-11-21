import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// Own page in the existing isolated QA browser; never uses the user's profile.
const evidence = new URL('../node_modules/.cache/timeline-floating-001/', import.meta.url);
await fs.mkdir(evidence, { recursive: true });
const stage = process.argv.includes('--red') ? 'red' : 'green';
const tab = await (await fetch('http://127.0.0.1:9227/json/new?about:blank', { method: 'PUT' })).json();
const socket = new WebSocket(tab.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
let serial = 0;
const requests = new Map(), checks = [], runtimeErrors = [], warnings = [];
socket.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (message.method === 'Runtime.exceptionThrown') runtimeErrors.push(message.params);
  if (message.method === 'Runtime.consoleAPICalled' && ['warning', 'error'].includes(message.params.type)) warnings.push(message.params);
  if (message.method === 'Log.entryAdded' && ['warning', 'error'].includes(message.params.entry.level)) warnings.push(message.params.entry);
  const request = requests.get(message.id);
  if (request) { requests.delete(message.id); message.error ? request.reject(message.error) : request.resolve(message.result); }
});
function send(method, params = {}) { return new Promise((resolve, reject) => { const id = ++serial; requests.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); }); }
async function evaluate(expression) {
  const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails)); return result.result.value;
}
const q = selector => 'document.querySelector(' + JSON.stringify(selector) + ')';
async function rest() { await evaluate('new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))'); }
async function viewport(width = 1440, height = 900) { await send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false }); await rest(); }
async function mouse(type, point, down = false) {
  await send('Input.dispatchMouseEvent', { type, ...point, button: type === 'mouseMoved' && !down ? 'none' : 'left', buttons: down ? 1 : 0, clickCount: type === 'mouseMoved' ? 0 : 1 }); await rest();
}
const leave = () => mouse('mouseMoved', { x: 10, y: 80 });
async function load(scenario = 'hover-valid', extra = '') {
  const url = 'http://127.0.0.1:5173/?view=session&timelineClock=manual&sessionScenario=' + scenario + extra;
  await send('Page.navigate', { url });
  let ready = false;
  for (let index = 0; index < 80; index++) {
    if (await evaluate('location.href===' + JSON.stringify(url) + '&&document.readyState==="complete"&&!!document.querySelector(".session-shell")')) { ready = true; break; }
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  assert(ready, 'Session loaded'); await evaluate('document.fonts.ready'); await rest(); await leave();
}
async function point(value) { return evaluate('(()=>{const r=' + q('.timeline-groove') + '.getBoundingClientRect();return {x:Math.round(r.left)+(Math.round(r.width)-1)*' + value + '/10000,y:r.top+1};})()'); }
async function hover(value = 5000) { const p = await point(value); await mouse('mouseMoved', p); return p; }
async function dev(action) { await evaluate('window.dispatchEvent(new CustomEvent("dse-timeline-dev",{detail:' + JSON.stringify(action) + '}))'); await rest(); }
const timeline = () => evaluate('({...document.querySelector(".session-timeline").dataset})');
const layout = () => evaluate('(()=>{const r=s=>{const e=document.querySelector(s);const a=e.getBoundingClientRect();return {x:a.x,y:a.y,width:a.width,height:a.height}};return Object.fromEntries([".session-workspace",".session-library",".preview-workspace",".preview-viewport",".preview-canvas",".preview-native-surface",".session-inspector",".session-timeline",".timeline-groove",".playback-rail"].map(s=>[s,r(s)]));})()');
const popup = () => evaluate('(()=>{const p=document.querySelector(".timeline-hover-popup"),h=document.querySelector(".timeline-hover-overlay"),image=document.querySelector(".timeline-thumbnail");const r=e=>{const a=e.getBoundingClientRect();return {x:a.x,y:a.y,width:a.width,height:a.height,right:a.right,bottom:a.bottom}};return {visible:!!p&&!!p.getBoundingClientRect().width&&getComputedStyle(p).visibility!=="hidden",rect:p?r(p):null,image:image?r(image):null,cssWidth:h?parseFloat(h.style.getPropertyValue("--thumbnail-width")):null,cssHeight:h?parseFloat(h.style.getPropertyValue("--thumbnail-height")):null,host:!!h,hostFixed:!!h&&getComputedStyle(h).position==="fixed",inTimeline:!!p?.closest(".session-timeline"),pointerEvents:p?getComputedStyle(p).pointerEvents:null,time:document.querySelector(".timeline-hover-time")?.textContent,aspect:document.querySelector("[data-canvas-aspect]")?.dataset.canvasAspect};})()');
function near(actual, expected, message, tolerance = 1) { assert(Math.abs(actual - expected) <= tolerance, `${message}: ${actual} vs ${expected}`); }
async function check(name, run) { try { await run(); checks.push({ name, pass: true }); } catch (error) { checks.push({ name, pass: false, error: error.message }); } }

try {
  await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable'); await send('Log.clear'); await send('Runtime.discardConsoleEntries');
  await viewport();
  // These same three desired-behavior checks run before and after implementation.
  for (const state of ['loading', 'valid', 'unavailable']) await check(`${state}: zero reflow of Preview, three columns, Timeline and Rail`, async () => {
    await load(state === 'unavailable' ? 'hover-unavailable' : 'hover-valid');
    const before = await layout(); await hover(5000);
    if (state !== 'loading') { await dev('advance75'); await dev('resolve-latest'); }
    assert.equal((await timeline()).hoverStatus, state);
    const during = await layout();
    assert.deepEqual(during, before, `${state} must not change any MainWindow layout rectangle`);
    const p = await popup(); assert(p.visible); assert(p.hostFixed && !p.inTimeline, 'Popup belongs to a separate fixed overlay');
    await leave(); assert.deepEqual(await layout(), before); assert.equal((await popup()).visible, false);
  });

  if (stage === 'green') {
    await check('Popup absent before hover; Loading → Valid keeps the same host, footprint and secondary timestamp', async () => {
      await load(); assert.equal((await popup()).visible, false); await hover(3700);
      const first = await popup(); assert(first.visible); assert.equal((await timeline()).hoverStatus, 'loading');
      await evaluate('void(window.floatingHost=document.querySelector(".timeline-hover-overlay"))');
      await dev('advance75'); await dev('resolve-latest');
      const second = await popup(); assert.equal((await timeline()).hoverStatus, 'valid'); assert.deepEqual(second.rect, first.rect);
      assert(await evaluate('window.floatingHost===document.querySelector(".timeline-hover-overlay")'));
      assert(second.time); assert(await evaluate('document.querySelector(".timeline-hover-time").getBoundingClientRect().top>=document.querySelector(".timeline-thumbnail").getBoundingClientRect().bottom'));
    });
    await check('Loading → Unavailable keeps the same overlay footprint', async () => {
      await load('hover-unavailable'); await hover(); const first = await popup(); await dev('advance75'); await dev('resolve-latest');
      assert.equal((await timeline()).hoverStatus, 'unavailable'); assert.deepEqual((await popup()).rect, first.rect);
    });
    const aspects = [['16-9', 16, 9], ['16-10', 16, 10], ['4-3', 4, 3], ['1-1', 1, 1], ['ultrawide', 32, 9], ['portrait', 9, 16], ['extreme-wide', 1000, 1], ['extreme-portrait', 1, 1000]];
    for (const [name, n, d] of aspects) await check(`Canvas ${name}: aspect authority, bounded thumbnail, no layout reflow`, async () => {
      await load('hover-floating-' + name); const before = await layout(); await hover(); await dev('advance75'); await dev('resolve-latest');
      const p = await popup(); assert(p.visible); assert.equal(p.aspect, `${n}:${d}`);
      near(p.cssWidth / p.cssHeight, n / d, 'Exact authored Canvas CSS aspect', Math.max(1e-10, n / d * 1e-10));
      // Chromium lays out boxes in 1/64 CSS px units. At 1000:1, a 0.2px
      // height becomes 0.1875px; compare each dimension at that pixel precision.
      near(p.image.width, p.cssWidth, 'Rendered content width', 1 / 64);
      near(p.image.height, p.cssHeight, 'Rendered content height', 1 / 64);
      assert(p.image.width <= 201 && p.image.height <= 141, 'Bounded content'); assert(p.rect.width <= 217 && p.rect.height <= 181);
      assert(p.rect.x >= 7 && p.rect.right <= 1433 && p.rect.y >= 7); assert.deepEqual(await layout(), before);
    });
    for (const [name, value] of [['left', 0], ['center', 5000], ['right', 10000]]) await check(`${name}: cursor anchor/clamp and original hover timestamp`, async () => {
      await load('hover-' + (name === 'center' ? 'center' : name + '-edge')); const anchor = await hover(value), p = await popup();
      assert(p.rect.x >= 8 && p.rect.right <= 1432);
      const desired = anchor.x - p.rect.width / 2, expected = Math.max(8, Math.min(desired, 1432 - p.rect.width));
      near(p.rect.x, expected, 'Pointer-centered horizontal clamp', .1);
      const groove = (await layout())['.timeline-groove']; near(p.rect.bottom, groove.y - 8, 'Small gap above current groove', .1);
      const before = await timeline(); await dev('advance75'); await dev('resolve-latest');
      const rawSeconds = Number(before.duration) * value / 10000;
      const expectedTime = `${String(Math.floor(rawSeconds / 60)).padStart(2, '0')}:${String(Math.floor(rawSeconds % 60)).padStart(2, '0')}`;
      // Pointer mapping uses integer groove pixels. Allow its existing subpixel rounding at the center.
      if (value !== 5000) assert.equal((await popup()).time, expectedTime);
      assert.equal((await timeline()).previewTime, before.previewTime, 'Hover never seeks');
      assert.equal((await popup()).pointerEvents, 'none');
    });
    await check('Clamping never substitutes playback thumb position or bucket for hovered timestamp', async () => {
      await load('hover-valid'); await hover(7500); await dev('advance75'); await dev('resolve-latest');
      assert.equal((await popup()).time, '00:17'); assert.equal((await timeline()).frameBucket, '15000'); assert.equal((await timeline()).runtimeTime, '8');
    });
    await check('Selected ≠ active: content is active scene; authored Canvas owns container ratio', async () => {
      await load('hover-selected-not-active'); await hover(); await dev('advance75'); await dev('resolve-latest');
      const t = await timeline(); assert.equal(t.hoverItem, 'a-opening'); assert.equal(await evaluate(q('.session-shell') + '.dataset.selectedItemId'), 'b-image');
      assert.equal(await evaluate(q('[data-hover-source-frame]') + '.dataset.sourceIdentity.includes("a-opening")'), true);
      assert.equal((await popup()).aspect, '16:9');
    });
    for (const invalidation of ['leave', 'drag']) await check(`${invalidation}: immediate hide; late result cannot reopen; drag/Release suppress hover`, async () => {
      await load('hover-then-' + invalidation); await hover(2500); await dev('advance75');
      if (invalidation === 'leave') await leave();
      else await mouse('mousePressed', await point(2500), true);
      assert.equal((await popup()).visible, false); assert.equal((await timeline()).hoverActive, 'false');
      await dev('resolve-oldest'); assert.equal((await popup()).visible, false); assert.equal((await timeline()).staleRejected, '1');
      if (invalidation === 'drag') { await mouse('mouseMoved', await point(6000), true); assert.equal((await popup()).visible, false); await mouse('mouseReleased', await point(6000)); assert.equal((await popup()).visible, false); }
    });
    await check('Rapid hover coalescing and out-of-order result rejection preserve the floating surface', async () => {
      await load('hover-out-of-order'); const before = await layout(); await hover(2500); await dev('advance74'); assert.equal((await timeline()).submissions, '0');
      await hover(7500); await dev('advance1'); assert.equal((await timeline()).submissions, '1'); await dev('resolve-latest');
      await hover(2000); await dev('advance75'); await hover(8000); await dev('advance75'); await dev('resolve-latest');
      const frame = (await timeline()).frameId; await dev('resolve-oldest'); assert.equal((await timeline()).frameId, frame); assert.equal((await timeline()).staleRejected, '1'); assert.deepEqual(await layout(), before);
    });
    await check('Visible popup recomputes viewport clamp during in-region resize; hide never changes resized layout', async () => {
      // Adjustment 004: x=150 is not inside a wide center Timeline. Exercise
      // real groove points within each existing responsive regime. Crossing a
      // breakpoint can legitimately dispatch leave while columns reflow; the
      // following separate case still verifies resize-away invalidation.
      for (const [from, width] of [[1440,1280],[1280,1200],[1000,900],[680,620],[440,480]]) {
        await viewport(from, 900); await load('hover-window-resize');
        const anchor = await hover(2500); await dev('advance75'); await dev('resolve-latest');
        await viewport(width, 900);
        assert(await evaluate('(()=>{const r=document.querySelector(".timeline-control").getBoundingClientRect();return '+anchor.x+'>=r.left&&'+anchor.x+'<=r.right&&'+anchor.y+'>=r.top&&'+anchor.y+'<=r.bottom})()'), 'Stationary pointer stays inside actual resized control');
        const p = await popup(); assert(p.visible, 'In-region hover remains visible');
        assert(p.rect.x >= 8 && p.rect.right <= width - 8 + .1); near(p.rect.bottom, (await layout())['.timeline-groove'].y - 8, 'Fresh resized groove anchor');
        const active = await layout(); await leave(); assert.deepEqual(await layout(), active);
      }
    });
    await check('Resize moving Timeline away from the pointer keeps ordinary leave invalidation', async () => {
      await viewport(440, 900); await load('hover-window-resize'); await hover(1000); await dev('advance75');
      await viewport(1440, 900); assert.equal((await timeline()).hoverActive, 'false');
      await dev('resolve-latest'); assert.equal((await popup()).visible, false); assert.equal((await timeline()).staleRejected, '1');
    });
    await check('Both splitter changes use current geometry and cannot make hover resize siblings', async () => {
      await viewport(); await load(); await hover(); await dev('advance75'); await dev('resolve-latest');
      for (const side of ['left', 'right']) {
        await evaluate(q('[data-layout-splitter="' + side + '"]') + '.dispatchEvent(new KeyboardEvent("keydown",{key:"ArrowRight",bubbles:true,cancelable:true}))'); await rest();
        const before = await layout(); assert((await popup()).visible); near((await popup()).rect.bottom, before['.timeline-groove'].y - 8, 'Current splitter-era groove anchor');
        await leave(); assert.deepEqual(await layout(), before); await hover(); await dev('advance75'); await dev('resolve-latest'); assert.deepEqual(await layout(), before);
      }
    });
    await check('Production F06 combined composition remains active-scene geometry inside the overlay', async () => {
      await load('gap-hover-geometry'); await hover(); await dev('advance75'); await dev('resolve-latest');
      const geometry = await evaluate('(()=>{const s=document.querySelector(".timeline-thumbnail svg").viewBox.baseVal,r=document.querySelector("[data-hover-destination]");return [Number(r.getAttribute("x"))/s.width,Number(r.getAttribute("y"))/s.height,Number(r.getAttribute("width"))/s.width,Number(r.getAttribute("height"))/s.height]})()');
      [.35, .0875, .7, .525].forEach((expected, index) => near(geometry[index], expected, 'F06 destination', .000001));
      assert((await popup()).hostFixed);
    });
    await check('Existing 9999 seek and 10000 Next remain distinct', async () => {
      await load('right-edge-9999'); await dev('9999'); assert.equal((await timeline()).itemId, 'a-scene'); assert(Number((await timeline()).runtimeTime) > 106.98);
      await load('right-edge-10000'); await dev('10000'); assert.equal((await timeline()).itemId, 'b-opening');
    });
    for (const view of ['session', 'launcher', 'background-music']) await check(`${view} route smoke`, async () => {
      const url = 'http://127.0.0.1:5173/?view=' + view;
      await send('Page.navigate', { url }); let ready = false;
      const selector = view === 'session' ? '.session-shell' : view === 'launcher' ? '.launcher-shell' : '.background-music-shell';
      for (let index = 0; index < 80; index++) { if (await evaluate('location.href===' + JSON.stringify(url) + '&&!!' + q(selector))) { ready = true; break; } await new Promise(r => setTimeout(r, 50)); }
      assert(ready, view + ' ready'); await rest();
    });
  }
  const result = { stage, pass: checks.every(check => check.pass) && !runtimeErrors.length && !warnings.length, passed: checks.filter(check => check.pass).length, total: checks.length, checks, runtimeErrors, warnings };
  await fs.writeFile(new URL(`presentation-${stage}.json`, evidence), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result, null, 2)); if (!result.pass) process.exitCode = 1;
} finally { socket.close(); await fetch('http://127.0.0.1:9227/json/close/' + tab.id); }
