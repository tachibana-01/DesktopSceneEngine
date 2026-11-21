import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// A single isolated loopback QA page; no new test framework or domain model.
const progress = process.argv.includes('--progress');
const evidence = new URL(progress ? '../node_modules/.cache/progress-row-005/' : '../node_modules/.cache/preview-transport-004/', import.meta.url);
await fs.mkdir(evidence, { recursive: true });
const before = process.argv.includes('--before');
const tab = await (await fetch('http://127.0.0.1:9227/json/new?about:blank', { method: 'PUT' })).json();
const ws = new WebSocket(tab.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { ws.addEventListener('open', resolve, { once: true }); ws.addEventListener('error', reject, { once: true }); });
let serial = 0;
const pending = new Map(), checks = [], runtimeErrors = [], consoleWarnings = [];
ws.addEventListener('message', event => {
  const m = JSON.parse(event.data);
  if (m.method === 'Runtime.exceptionThrown') runtimeErrors.push(m.params);
  if (m.method === 'Runtime.consoleAPICalled' && ['warning', 'error'].includes(m.params.type)) consoleWarnings.push(m.params);
  if (m.method === 'Log.entryAdded' && ['warning', 'error'].includes(m.params.entry.level)) consoleWarnings.push(m.params.entry);
  const request = pending.get(m.id);
  if (request) { pending.delete(m.id); m.error ? request.reject(m.error) : request.resolve(m.result); }
});
function send(method, params = {}) { return new Promise((resolve, reject) => { const id = ++serial; pending.set(id, { resolve, reject }); ws.send(JSON.stringify({ id, method, params })); }); }
async function evaluate(expression) { const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }); assert(!r.exceptionDetails, JSON.stringify(r.exceptionDetails)); return r.result.value; }
const q = s => 'document.querySelector(' + JSON.stringify(s) + ')';
const wait = ms => new Promise(r => setTimeout(r, ms));
async function rest() { await evaluate('new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))'); }
async function viewport(width = 1440, height = 900) { await send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false }); await rest(); }
async function load(scenario = 'normal', manual = false) {
  const url = 'http://127.0.0.1:5173/?view=session&sessionScenario=' + scenario + (manual ? '&timelineClock=manual' : '');
  await send('Page.navigate', { url }); let ready = false;
  for (let n = 0; n < 80; n++) { if (await evaluate('location.href===' + JSON.stringify(url) + '&&document.readyState==="complete"&&!!document.querySelector(".session-timeline")')) { ready = true; break; } await wait(50); }
  assert(ready, 'Session ready'); await evaluate('document.fonts.ready'); await rest(); await mouse('mouseMoved', { x: 10, y: 70 });
}
async function mouse(type, p, down = false, deltaY) { await send('Input.dispatchMouseEvent', { type, ...p, button: type === 'mouseMoved' || type === 'mouseWheel' ? down ? 'left' : 'none' : 'left', buttons: down ? 1 : 0, clickCount: ['mousePressed', 'mouseReleased'].includes(type) ? 1 : 0, ...(deltaY === undefined ? {} : { deltaX: 0, deltaY }) }); await rest(); }
async function center(s) { assert(await evaluate('!!' + q(s)), s); return evaluate('(()=>{const r=' + q(s) + '.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()'); }
async function click(s) { const p = await center(s); assert(await evaluate('(()=>{const e=document.elementFromPoint(' + p.x + ',' + p.y + '),t=' + q(s) + ';return !!e&&(e===t||t.contains(e))})()'), 'Pointer hits requested control: ' + s); await mouse('mouseMoved', p); await mouse('mousePressed', p, true); await mouse('mouseReleased', p); }
async function key(key, code, number) { for (const type of ['keyDown', 'keyUp']) await send('Input.dispatchKeyEvent', { type, key, code, windowsVirtualKeyCode: number, ...(key === 'Enter' && type === 'keyDown' ? { text: '\r' } : {}) }); await rest(); }
async function check(name, run) { try { const detail = await run(); checks.push({ name, pass: true, ...(detail ? { detail } : {}) }); } catch (error) { checks.push({ name, pass: false, error: error.message }); } }
function near(a, b, label, tolerance = 1) { assert(Math.abs(a - b) <= tolerance, `${label}: ${a} vs ${b}`); }
const shell = () => evaluate('({...document.querySelector(".session-shell").dataset})');
const rail = () => evaluate('({...document.querySelector(".playback-rail").dataset})');
const timeline = () => evaluate('({...document.querySelector(".session-timeline").dataset})');
const geometry = () => evaluate('(()=>{const rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom,display:getComputedStyle(e).display}};return {window:{width:innerWidth,height:innerHeight,scrollWidth:document.documentElement.scrollWidth,scrollHeight:document.documentElement.scrollHeight},...Object.fromEntries([".session-workspace",".session-library",".session-inspector",".preview-workspace",".preview-viewport",".preview-native-surface",".session-timeline",".timeline-groove",".playback-rail"].map(s=>[s,rect(document.querySelector(s))])),controls:[...document.querySelectorAll(".playback-rail button,.rail-runtime-status,.rail-display,.timeline-control,.timeline-time")].filter(e=>e.getBoundingClientRect().width&&getComputedStyle(e).display!=="none").map(e=>({class:e.className,...rect(e)}))}})()');
async function point(value) { return evaluate('(()=>{const r=document.querySelector(".timeline-groove").getBoundingClientRect();return {x:Math.round(r.x)+(Math.round(r.width)-1)*' + value + '/10000,y:r.y+1}})()'); }
async function sameIdentity(ids) { const next = await shell(); for (const f of ['selectedPlaylistId', 'selectedItemId', 'activePlaylistId', 'activeItemId']) assert.equal(next[f], ids[f]); }

async function checkProgressRow() {
  const metrics = () => evaluate('(()=>{const rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right}};const line=document.querySelector(".timeline-line"),control=line.querySelector(".timeline-control"),track=line.querySelector(".timeline-groove"),time=line.querySelector(".timeline-time"),s=getComputedStyle(line),c=getComputedStyle(control),t=getComputedStyle(time),g=getComputedStyle(track),thumb=getComputedStyle(document.querySelector(".timeline-thumb"));return {line:rect(line),control:rect(control),track:rect(track),time:rect(time),paddingLeft:parseFloat(s.paddingLeft),paddingRight:parseFloat(s.paddingRight),gap:parseFloat(s.columnGap),grow:c.flexGrow,timeShrink:t.flexShrink,timeMin:t.minWidth,nowrap:t.whiteSpace,skin:{trackHeight:g.height,trackColor:g.backgroundColor,thumbWidth:thumb.width,thumbHeight:thumb.height,thumbColor:thumb.backgroundColor},text:time.textContent}})()');
  await viewport(); await load();
  if (before) await fs.writeFile(new URL('geometry-before.json', evidence), JSON.stringify({ progress: await metrics(), layout: await geometry() }, null, 2));
  await check('Progress Row has only slider/time, no POSITION node or reserved wrapper; accessibility and ownership retained', async () => {
    await load(); assert.equal(await evaluate('!!document.querySelector(".timeline-label")'), false);
    assert.equal(await evaluate('document.querySelector(".timeline-line").children.length'), 2);
    assert(await evaluate('(()=>{const s=document.querySelector(".timeline-control"),t=document.querySelector(".timeline-time");return s.parentElement===t.parentElement&&s.getAttribute("role")==="slider"&&s.getAttribute("aria-label")==="Playback position"&&s.getAttribute("aria-describedby")==="timeline-instructions"&&!!document.getElementById("timeline-instructions")&&!!s.closest(".preview-workspace")&&!!document.querySelector(".preview-workspace .rail-mute")})()'));
  });
  for (const [name, width, minimum] of [['wide',1440,false],['normal',1200,false],['minimum center',1440,true]]) await check(name + ': flex groove uses full content padding, content-sized single-line time, exact 14px gap and no overflow', async () => {
    await viewport(width, 900); await load();
    if (minimum) { const p = await center('[data-layout-splitter=left]'); await mouse('mouseMoved', p); await mouse('mousePressed', p, true); await mouse('mouseMoved', { x: 1400, y: p.y }, true); await mouse('mouseReleased', { x: 1400, y: p.y }); }
    const g = await metrics(), layout = await geometry(); assert.equal(g.grow, '1'); assert.equal(g.timeShrink, '0'); assert.equal(g.timeMin, '0px'); assert.equal(g.nowrap, 'nowrap');
    near(g.track.x, g.line.x + g.paddingLeft, 'No left reservation', .1); near(g.time.x - g.track.right, 14, 'Actual visible groove/time gap', .1); near(g.time.right, g.line.right - g.paddingRight, 'Time stays at normal content edge', .1);
    assert(g.control.width > 0 && g.track.right < g.time.x); assert(layout.window.scrollWidth <= width + 1); assert.equal(g.line.height, 34);
    if (name === 'wide' && !before) {
      const baseline = await fs.readFile(new URL('geometry-before.json', evidence), 'utf8').then(JSON.parse).catch(() => null);
      if (baseline) { assert(g.track.width > baseline.progress.track.width + 60); assert(g.track.x < baseline.progress.track.x - 30); near(g.time.right, baseline.progress.time.right, 'Time was not pushed outside to fake space', .1); assert.deepEqual(g.skin, baseline.progress.skin); for (const s of ['.preview-workspace','.preview-native-surface','.session-timeline','.playback-rail']) assert.deepEqual(layout[s], baseline.layout[s], s + ' preserved'); }
    }
    return g;
  });
  await check('Click/drag use actual extended groove; Press previews, Release commits; no identity or dirty change', async () => {
    await viewport(); await load('selected-not-active'); const ids = await shell(), initial = await timeline();
    const p = await point(2500); await mouse('mouseMoved', p); await mouse('mousePressed', p, true); await mouse('mouseReleased', p); near(Number((await timeline()).runtimeTime), Number(initial.duration) * .25, 'Click mapping', .1);
    await mouse('mousePressed', p, true); const prior = (await timeline()).runtimeTime; const end = await point(6100); await mouse('mouseMoved', end, true); assert.equal((await timeline()).runtimeTime, prior); await mouse('mouseReleased', end); near(Number((await timeline()).runtimeTime), Number(initial.duration) * .61, 'Drag mapping', .1); await sameIdentity(ids); assert.equal((await shell()).revision, ids.revision);
  });
  await check('Adjustment 001 smoke: left/middle/right hover timestamps float above the new groove with zero reflow', async () => {
    await load('hover-valid', true); const before = await geometry(), time = (await timeline()).runtimeTime;
    for (const [value, text] of [[0,'00:00'],[5000,'00:11'],[10000,'00:23']]) {
      const p = await point(value); await mouse('mouseMoved', p); await evaluate('for(const detail of ["advance75","resolve-latest"])window.dispatchEvent(new CustomEvent("dse-timeline-dev",{detail}))'); await rest();
      assert.equal((await timeline()).hoverStatus, 'valid'); assert.equal(await evaluate('document.querySelector(".timeline-hover-time").textContent'), text); assert.equal((await timeline()).runtimeTime, time); assert.deepEqual(await geometry(), before);
      const popup = await evaluate('(()=>{const e=document.querySelector(".timeline-hover-popup"),r=e.getBoundingClientRect();return {left:r.x,right:r.right,bottom:r.bottom,fixed:getComputedStyle(e.closest(".timeline-hover-overlay")).position}})()'); near(popup.bottom, before['.timeline-groove'].y - 8, 'Floating anchor'); assert(popup.left >= 8 && popup.right <= 1432.1); assert.equal(popup.fixed, 'fixed');
    }
  });
  await viewport(); await load();
  const capture = await send('Page.captureScreenshot', { format: 'png' }); await fs.writeFile(new URL(before ? 'progress-before.png' : 'progress-after.png', evidence), Buffer.from(capture.data, 'base64'));
}

try {
  await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable'); await viewport();
  if (progress) await checkProgressRow();
  else {
  await check('True DOM ownership: Timeline/Rail inside Preview, Library and Inspector are siblings', async () => {
    await load(); assert(await evaluate('(()=>{const p=document.querySelector(".preview-workspace"),w=document.querySelector(".session-workspace");return p.parentElement===w&&document.querySelector(".session-library").parentElement===w&&document.querySelector(".session-inspector").parentElement===w&&p.contains(document.querySelector(".session-timeline"))&&p.contains(document.querySelector(".playback-rail"))&&!document.querySelector(".session-shell > .playback-rail,.session-shell > .session-timeline")})()'));
  });
  await check('Wide side panels extend to workspace bottom and controls match center edges', async () => {
    await load(); const g = await geometry(), w = g['.session-workspace'], p = g['.preview-workspace'];
    near(g['.session-library'].bottom, w.bottom, 'Library bottom'); near(g['.session-inspector'].bottom, w.bottom, 'Inspector bottom'); near(w.bottom, 900, 'Workspace bottom');
    for (const s of ['.session-timeline', '.playback-rail']) { near(g[s].x, p.x, s + ' left'); near(g[s].right, p.right, s + ' right'); }
    assert(g['.playback-rail'].width < 1440); return g;
  });
  await check('Native marker remains a distinct rectangle above Timeline/Transport', async () => {
    await load(); assert.equal(await evaluate('document.querySelector("[data-native-region]").querySelectorAll(".session-timeline,.playback-rail,button,[role=slider]").length'), 0);
    const g = await geometry(); assert(g['.preview-native-surface'].bottom <= g['.session-timeline'].y); assert(g['.session-timeline'].bottom <= g['.playback-rail'].y + 1);
    assert(await evaluate('document.querySelector(".preview-workspace").contains(document.querySelector("[data-native-region]"))'));
  });
  if (!before) {
    let previous = null;
    for (const width of [1440, 1200, 1101, 1100, 761, 760, 440]) await check(`${width}px: scoped edges, full-height panels, no overflow/overlap and unchanged control sizes`, async () => {
      await viewport(width, 800); await load('playback-long-alias'); const g = await geometry(), p = g['.preview-workspace'], r = g['.playback-rail'], t = g['.session-timeline'], w = g['.session-workspace'];
      assert(g.window.scrollWidth <= width + 1); assert(g.window.scrollHeight <= 801); near(w.bottom, 800, 'Main bottom');
      assert.equal(g['.session-library'].display !== 'none', width > 1100); assert.equal(g['.session-inspector'].display !== 'none', width > 760);
      for (const s of ['.session-library', '.session-inspector']) if (g[s].display !== 'none') near(g[s].bottom, w.bottom, s + ' bottom');
      near(r.x, p.x, 'Rail left'); near(r.right, p.right, 'Rail right'); near(t.x, p.x, 'Timeline left'); near(t.right, p.right, 'Timeline right'); near(r.bottom, p.bottom, 'Rail bottom');
      assert(g['.preview-native-surface'].height > 0); assert(g['.preview-native-surface'].bottom <= t.y + 1); assert(t.bottom <= r.y + 1); assert(g.controls.every(c => c.x >= p.x - 1 && c.right <= p.right + 1), 'Runtime controls stay inside Preview');
      const buttonRects = g.controls.filter(c => c.class.includes('playback-button')); assert.equal(buttonRects.length, 3); assert(buttonRects.every(c => c.width === (width <= 520 ? 30 : 34) && c.height === 34)); assert.equal(r.height, 64); assert.equal(t.height, 35);
      const buttons = await evaluate('(()=>{const r=document.querySelector(".playback-buttons").getBoundingClientRect(),a=document.querySelector(".rail-display").getBoundingClientRect(),b=document.querySelector(".rail-status").getBoundingClientRect();return {left:a.right,middleLeft:r.left,middleRight:r.right,right:b.left}})()');
      assert(buttons.left <= buttons.middleLeft && buttons.middleRight <= buttons.right, 'Display/transport/status do not overlap');
      if (previous && width === 1200) assert(g['.timeline-groove'].width < previous['.timeline-groove'].width, 'Groove follows center width'); previous = g; return g;
    });
    await check('Splitter change resizes actual center groove and Rail with matching bounds', async () => {
      await viewport(); await load(); const before = await geometry(); await evaluate('document.querySelector("[data-layout-splitter=left]").focus()'); await key('ArrowRight', 'ArrowRight', 39); const after = await geometry();
      assert(after['.preview-workspace'].width < before['.preview-workspace'].width); assert(after['.timeline-groove'].width < before['.timeline-groove'].width); near(after['.playback-rail'].width, after['.preview-workspace'].width, 'Rail width after splitter');
    });
    await check('Existing splitter minimum keeps transport hit targets and runtime controls inside the center', async () => {
      await viewport(); await load('playback-long-alias'); const start = await center('[data-layout-splitter=left]'); await mouse('mouseMoved', start); await mouse('mousePressed', start, true); await mouse('mouseMoved', { x: 1400, y: start.y }, true); await mouse('mouseReleased', { x: 1400, y: start.y });
      const g = await geometry(), p = g['.preview-workspace']; assert(p.width < 440); const outside = g.controls.filter(c => c.x < p.x - 1 || c.right > p.right + 1); assert.equal(outside.length, 0, JSON.stringify({preview:p,outside}));
      const separation = await evaluate('(()=>{const a=document.querySelector(".playback-buttons").getBoundingClientRect(),r=document.querySelector(".rail-status").getBoundingClientRect(),t=document.querySelector(".rail-runtime-status").getBoundingClientRect();return {controlsRight:a.right,statusLeft:r.left,timeLeft:t.left,timeWidth:t.width}})()'); assert(separation.controlsRight <= separation.statusLeft); if(separation.timeWidth) assert(separation.controlsRight <= separation.timeLeft); return g;
    });
    await check('Play/Pause, Next/Previous and Output Mute keep callback/state authority after reparenting', async () => {
      await viewport(); await load(); const ids = await shell(); await click('[aria-label=Pause]'); assert.equal(await evaluate('document.querySelector(".playback-state").textContent'), 'PAUSED'); await click('[aria-label=Play]'); await sameIdentity(ids);
      await click('[aria-label=Next]'); assert.notEqual((await shell()).activeItemId, ids.activeItemId); await click('[aria-label=Previous]'); assert.equal((await shell()).activeItemId, ids.activeItemId);
      const revision = (await shell()).revision, output = (await rail()).effectiveOutput; await click('.rail-mute'); assert.equal((await rail()).outputMuted, 'true'); assert.equal((await rail()).effectiveOutput, '0'); await click('.rail-mute'); assert.equal((await rail()).effectiveOutput, output); assert.equal((await shell()).revision, revision);
    });
    await check('Trusted seek click/drag uses migrated groove bounds; Press previews and Release alone commits', async () => {
      await load('selected-not-active'); const ids = await shell(), original = await timeline(); const p = await point(3700); await mouse('mouseMoved', p); await mouse('mousePressed', p, true); const pressed = await timeline(); assert.equal(pressed.runtimeTime, original.runtimeTime); assert.equal(pressed.dragging, 'true');
      const end = await point(6100); await mouse('mouseMoved', end, true); await mouse('mouseReleased', end); const released = await timeline(); near(Number(released.runtimeTime), Number(released.duration) * .61, 'Seek uses real center groove', .1); await sameIdentity(ids);
      const clickPoint = await point(2500); await mouse('mouseMoved', clickPoint); await mouse('mousePressed', clickPoint, true); await mouse('mouseReleased', clickPoint); const clicked = await timeline(); near(Number(clicked.runtimeTime), Number(clicked.duration) * .25, 'Click seek maps actual width', .1); await sameIdentity(ids);
    });
    await check('Hover remains fixed above migrated groove and never changes Preview/columns/controls', async () => {
      await load('hover-valid', true); const before = await geometry(), p = await point(5000); await mouse('mouseMoved', p); await evaluate('for(const detail of ["advance75","resolve-latest"])window.dispatchEvent(new CustomEvent("dse-timeline-dev",{detail}))'); await rest(); assert.equal((await timeline()).hoverStatus, 'valid');
      assert.deepEqual(await geometry(), before); const placement = await evaluate('(()=>{const e=document.querySelector(".timeline-hover-popup"),h=e.closest(".timeline-hover-overlay"),r=e.getBoundingClientRect();return {bottom:r.bottom,x:r.x,width:r.width,fixed:getComputedStyle(h).position,inNative:!!h.closest("[data-native-region]")}})()'); near(placement.bottom, before['.timeline-groove'].y - 8, 'Hover vertical anchor'); near(placement.x + placement.width / 2, p.x, 'Cursor X'); assert.equal(placement.fixed, 'fixed'); assert.equal(placement.inNative, false);
    });
    await check('Adjustment 002 neighbor smoke: row speaker mute/restore, downward slider and wheel survive', async () => {
      await load('playlist-volume-default'); const s = '[data-playlist-id="playlist-a"] .playlist-volume-trigger'; await click(s); assert.equal(await evaluate(q(s) + '.dataset.masterPercent'), '0'); await click(s); assert.equal(await evaluate(q(s) + '.dataset.masterPercent'), '72');
      const p = await center(s); await mouse('mouseWheel', p, false, -120); assert.equal(await evaluate(q(s) + '.dataset.masterPercent'), '77'); assert(await evaluate('(()=>{const p=document.querySelector(".playlist-volume-popover").getBoundingClientRect(),s=' + q(s) + '.getBoundingClientRect();return p.top>s.bottom&&document.querySelector("[data-playlist-master-slider]").step==="1"})()'));
    });
    await check('Adjustment 003 neighbor smoke: both original scope selectors keep icons/popups and selected/active separation', async () => {
      await load('playback-list-loop'); const ids = await shell();
      for (const scope of ['project', 'playlist']) { const s = '[data-playback-mode-trigger=' + scope + ']'; await click(s); assert.equal(await evaluate(q('[data-playback-mode-popup]') + '.dataset.modeScope'), scope); await click('[data-playback-mode-popup] [data-choice-value=random]'); assert.equal(await evaluate(q(s) + '.value'), 'random'); await sameIdentity(ids); }
      assert(await evaluate('!!(document.querySelector(".media-panel [data-playback-mode-trigger=project]")&&document.querySelector(".playlist-panel [data-playback-mode-trigger=playlist]"))'));
    });
    for (const height of [560, 420]) await check(`Short ${height}px workspace allocates controls without viewport collapse or double scroll`, async () => {
      await viewport(1280, height); await load(); const g = await geometry(); assert(g.window.scrollHeight <= height + 1); assert(g['.preview-native-surface'].height > 0); assert(g['.preview-native-surface'].bottom <= g['.session-timeline'].y + 1); near(g['.playback-rail'].bottom, height, 'Short center bottom');
    });
    await viewport(); await load();
    const capture = await send('Page.captureScreenshot', { format: 'png' }); await fs.writeFile(new URL('preview-scoped-wide.png', evidence), Buffer.from(capture.data, 'base64'));
  }
  }
  const result = { stage: before ? 'before' : 'after', pass: checks.every(c => c.pass) && !runtimeErrors.length && !consoleWarnings.length, passed: checks.filter(c => c.pass).length, total: checks.length, checks, runtimeErrors, consoleWarnings };
  await fs.writeFile(new URL(before ? 'before.json' : 'focused.json', evidence), JSON.stringify(result, null, 2) + '\n'); console.log(JSON.stringify(result, null, 2)); if (!result.pass) process.exitCode = 1;
} finally { ws.close(); await fetch('http://127.0.0.1:9227/json/close/' + tab.id); }
