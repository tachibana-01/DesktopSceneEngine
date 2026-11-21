import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// Dedicated loopback QA tab. This suite never controls the user's browser.
const evidence = new URL('../node_modules/.cache/playback-mode-003/', import.meta.url);
await fs.mkdir(evidence, { recursive: true });
const red = process.argv.includes('--red');
const edgeRed = process.argv.includes('--edge-red');
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
async function evaluate(expression) { const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }); assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails)); return result.result.value; }
const q = s => 'document.querySelector(' + JSON.stringify(s) + ')';
const trigger = scope => '[aria-label=' + JSON.stringify(scope === 'project' ? 'Project sequence policy' : 'Playlist Item sequence policy') + ']';
const popup = '[data-playback-mode-popup]', tooltip = '[data-playback-mode-tooltip]';
const item = mode => popup + ' [data-choice-value=' + JSON.stringify(mode) + ']';
const modes = ['list-loop', 'random', 'repeat-current'];
const wait = ms => new Promise(r => setTimeout(r, ms));
async function rest() { await evaluate('new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))'); }
async function mouse(type, point, button = 'none', deltaY) {
  await send('Input.dispatchMouseEvent', { type, ...point, button, buttons: type === 'mousePressed' ? button === 'right' ? 2 : 1 : 0, ...(['mousePressed', 'mouseReleased'].includes(type) ? { clickCount: 1 } : {}), ...(deltaY === undefined ? {} : { deltaX: 0, deltaY }) }); await rest();
}
async function center(s) { assert(await evaluate('!!' + q(s)), 'Expected control exists: ' + s); return evaluate('(()=>{const r=' + q(s) + '.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()'); }
async function click(s, button = 'left') { const p = await center(s); await mouse('mouseMoved', p); await mouse('mousePressed', p, button); await mouse('mouseReleased', p, button); }
async function key(key, code, number, modifiers = 0) { for (const type of ['keyDown', 'keyUp']) await send('Input.dispatchKeyEvent', { type, key, code, windowsVirtualKeyCode: number, modifiers, ...(key === 'Enter' && type === 'keyDown' ? { text: '\r' } : {}) }); await rest(); }
async function load(scenario = 'normal') {
  const url = 'http://127.0.0.1:5173/?view=session&sessionScenario=' + scenario;
  await send('Page.navigate', { url }); let ready = false;
  for (let n = 0; n < 80; n++) { if (await evaluate('location.href===' + JSON.stringify(url) + '&&document.readyState==="complete"&&!!document.querySelector(".session-shell")')) { ready = true; break; } await wait(50); }
  assert(ready, 'Session ready'); await evaluate('document.fonts.ready'); await rest(); await mouse('mouseMoved', { x: 700, y: 100 });
}
async function check(name, run) { try { await run(); checks.push({ name, pass: true }); } catch (error) { checks.push({ name, pass: false, error: error.message }); } }
async function choose(scope, mode) { await click(trigger(scope)); await click(item(mode)); }
const value = scope => evaluate(q(trigger(scope)) + '.value');
const state = () => evaluate('({...document.querySelector(".session-shell").dataset})');
const revision = async () => Number((await state()).revision);
const runtime = () => evaluate('({rail:{...document.querySelector(".playback-rail").dataset},time:document.querySelector(".playback-time").textContent,state:document.querySelector(".playback-state").textContent})');
const layout = () => evaluate('(()=>{const rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}};return [".playback-rail",".session-timeline",".preview-viewport",".session-workspace",".session-library",".media-panel",".playlist-panel"].map(s=>rect(document.querySelector(s)));})()');
async function sameIdentity(before) { const after = await state(); for (const key of ['selectedPlaylistId', 'selectedItemId', 'activePlaylistId', 'activeItemId']) assert.equal(after[key], before[key], key); }
async function language(value) { await evaluate('(()=>{for(const action of [{type:"external-write",value:' + JSON.stringify(value) + '},{type:"invalidate"}])window.dispatchEvent(new CustomEvent("dse-mock-language",{detail:action}));})()'); await wait(20); await rest(); }
async function hover(s) { await mouse('mouseMoved', { x: 700, y: 100 }); await mouse('mouseMoved', await center(s)); }

try {
  await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await check('Both existing scope selectors become pure-icon triggers without permanent labels or chevrons', async () => {
    await load();
    for (const scope of ['project', 'playlist']) { assert.equal(await evaluate(q(trigger(scope)) + '.textContent.trim()'), ''); assert.equal(await evaluate(q(trigger(scope)) + '.dataset.playbackModeTrigger'), scope); assert.equal(await evaluate(q(trigger(scope)) + '.querySelectorAll("svg").length'), 1); }
    assert.equal(await evaluate('document.querySelectorAll(".header-policy-choice").length'), 0);
  });
  await check('DSE mode SVG exists at 22px with 24 viewBox, open strokes and no font numeral', async () => {
    await load(); const icon = trigger('project') + ' svg';
    assert.equal(await evaluate(q(icon) + '?.getAttribute("viewBox")'), '0 0 24 24');
    assert.equal(await evaluate(q(icon) + '.getAttribute("width")'), '22');
    assert.equal(await evaluate(q(icon) + '.getAttribute("fill")'), 'none'); assert.equal(await evaluate(q(icon) + '.querySelectorAll("text").length'), 0);
  });
  await check('Hover exposes a delayed current-mode tooltip', async () => {
    await load(); await hover(trigger('project')); await wait(400); assert.equal(await evaluate(q(tooltip) + '?.textContent'), 'Repeat Current');
  });
  await check('Click exposes a custom non-modal three-mode popup', async () => {
    await load(); await click(trigger('project')); assert(await evaluate('!!' + q(popup)));
    assert.equal(await evaluate(q(popup) + '.querySelectorAll("[role=menuitemradio]").length'), 3); assert.equal(await evaluate('document.querySelectorAll("dialog[open]").length'), 0);
  });
  await check('Outside right click dismisses mode popup while Playlist context action continues', async () => {
    await load(); await click(trigger('project')); assert(await evaluate('!!' + q(popup)));
    await click('[data-playlist-id="playlist-b"] .entity-select', 'right'); assert.equal(await evaluate('!!' + q(popup)), false);
    assert(await evaluate('!!document.querySelector("[data-command=rename]")')); assert.equal((await state()).selectedPlaylistId, 'playlist-b');
  });
  await check('Escape dismisses mode popup and restores trigger focus without policy change', async () => {
    await load(); const before = await value('project'); await click(trigger('project')); assert(await evaluate('!!' + q(popup))); await key('Escape', 'Escape', 27);
    assert.equal(await evaluate('!!' + q(popup)), false); assert.equal(await value('project'), before); assert(await evaluate('document.activeElement===' + q(trigger('project'))));
  });
  if (!red) {
    await check('Both controls stay in their existing header scopes; Rail source/geometry stays untouched', async () => {
      await load(); assert(await evaluate(q(trigger('project')) + '.closest(".media-panel")!==null'));
      assert(await evaluate(q(trigger('playlist')) + '.closest(".playlist-panel")!==null')); assert.equal(await evaluate('!!document.querySelector(".playback-rail [data-playback-mode-trigger]")'), false);
    });
    await check('Loop/Repeat One share exact rectangular base paths; Shuffle uses two curved crossing paths', async () => {
      await load(); const icons = {};
      for (const mode of modes) { await choose('project', mode); icons[mode] = await evaluate('(()=>{const e=' + q(trigger('project') + ' svg') + ';return {paths:[...e.querySelectorAll("path")].map(p=>p.getAttribute("d")),base:[...e.querySelectorAll("[data-repeat-base]")].map(p=>p.getAttribute("d")),stroke:e.getAttribute("stroke-width"),view:e.getAttribute("viewBox"),text:e.textContent}})()'); }
      assert.deepEqual(icons['list-loop'].base, icons['repeat-current'].base); assert.equal(icons['list-loop'].base.length, 4);
      assert.equal(icons['repeat-current'].paths.length, icons['list-loop'].paths.length + 1);
      assert(icons['list-loop'].base.some(p => p.includes('V') && p.includes('h'))); assert(!icons['list-loop'].base.some(p => /[aAcCqQ]/.test(p)));
      assert.equal(icons.random.paths.filter(p => /[cC]/.test(p)).length, 2); assert.equal(icons.random.base.length, 0);
      for (const icon of Object.values(icons)) { assert.equal(icon.stroke, '1.8'); assert.equal(icon.view, '0 0 24 24'); assert.equal(icon.text, ''); }
    });
    await check('All modes use the same neutral collapsed color, no persistent frame/highlight; hit area exceeds glyph', async () => {
      await load(); const colors = [];
      for (const mode of modes) { await choose('project', mode); await mouse('mouseMoved', { x: 700, y: 100 }); await evaluate('document.activeElement.blur()');
        const v = await evaluate('(()=>{const e=' + q(trigger('project')) + ',r=e.getBoundingClientRect(),s=getComputedStyle(e),g=e.querySelector("svg").getBoundingClientRect();return {color:s.color,bg:s.backgroundColor,border:s.borderTopWidth,width:r.width,height:r.height,glyph:g.width}})()');
        colors.push(v.color); assert.equal(v.bg, 'rgba(0, 0, 0, 0)'); assert.equal(v.border, '0px'); assert(v.width >= 28 && v.width <= 32); assert(v.height >= 28 && v.height <= 32); assert(v.width > v.glyph);
      } assert.equal(new Set(colors).size, 1);
    });
    await check('Tooltip delay prevents sweep flicker; follows pointer X above pointer; leave hides', async () => {
      await load(); const p = await center(trigger('project')); await mouse('mouseMoved', p); await wait(120); assert.equal(await evaluate('!!' + q(tooltip)), false);
      await wait(260); const geometry = await evaluate('(()=>{const r=' + q(tooltip) + '.getBoundingClientRect();return {center:r.x+r.width/2,bottom:r.bottom}})()'); assert(Math.abs(geometry.center - p.x) < 1); assert(geometry.bottom <= p.y - 10);
      await mouse('mouseMoved', { x: p.x + 3, y: p.y - 2 }); const moved = await evaluate('(()=>{const r=' + q(tooltip) + '.getBoundingClientRect();return r.x+r.width/2})()'); assert(Math.abs(moved - p.x - 3) < 1);
      await mouse('mouseMoved', { x: 700, y: 100 }); assert.equal(await evaluate('!!' + q(tooltip)), false);
    });
    for (const [mode, label] of [['list-loop', 'List Loop'], ['random', 'Random'], ['repeat-current', 'Repeat Current']]) await check('Tooltip projects authoritative current ' + mode, async () => {
      await load(); await choose('project', mode); await hover(trigger('project')); await wait(400); assert.equal(await evaluate(q(tooltip) + '.textContent'), label);
    });
    await check('Popup suppresses tooltip; trigger-toggle close requires leave/reenter before tooltip reappears', async () => {
      await load(); await hover(trigger('project')); await wait(400); assert(await evaluate('!!' + q(tooltip))); await click(trigger('project')); assert.equal(await evaluate('!!' + q(tooltip)), false); await wait(400); assert.equal(await evaluate('!!' + q(tooltip)), false);
      await click(trigger('project')); await wait(400); assert.equal(await evaluate('!!' + q(popup)), false); assert.equal(await evaluate('!!' + q(tooltip)), false);
      await hover(trigger('project')); await wait(400); assert(await evaluate('!!' + q(tooltip)));
    });
    for (const scope of ['project', 'playlist']) await check(scope + ' popup is upward, sidebar-local, detached, trigger-pointing arrow and no wrapper surface', async () => {
      await load(); await click(trigger(scope));
      const v = await evaluate('(()=>{const e=' + q(popup) + ',r=e.getBoundingClientRect(),t=' + q(trigger(scope)) + '.getBoundingClientRect(),l=e.ownerDocument.querySelector(".session-library").getBoundingClientRect(),s=getComputedStyle(e),a=getComputedStyle(e,"::after");return {left:r.left,right:r.right,libraryLeft:l.left,libraryRight:l.right,arrowCenter:r.x+parseFloat(a.left)+parseFloat(a.width)/2,target:t.x+t.width/2,bottom:r.bottom,top:t.top,position:s.position,bg:s.backgroundColor,radius:s.borderRadius,shadow:s.boxShadow,border:s.borderTopWidth,arrowBg:a.backgroundColor,arrowBottom:a.bottom,arrowHeight:a.height,arrowBorder:a.borderTopWidth,clip:a.clipPath}})()');
      assert.equal(v.position, 'fixed'); assert(v.left>=v.libraryLeft&&v.right<=v.libraryRight); assert(Math.abs(v.arrowCenter - v.target) < 1); assert.equal(v.top - v.bottom, 12); assert.equal(v.arrowBottom, '-6px'); assert.equal(v.arrowHeight, '6px'); assert.equal(v.bg, v.arrowBg); assert.equal(v.border, '0px'); assert.equal(v.arrowBorder, '0px'); assert(v.clip.includes('50% 100%'));
      await key('Escape', 'Escape', 27); await hover('[data-playlist-id="playlist-a"] .playlist-volume-trigger');
      const volume = await evaluate('(()=>{const s=getComputedStyle(document.querySelector(".playlist-volume-popover"));return {bg:s.backgroundColor,radius:s.borderRadius,shadow:s.boxShadow}})()'); assert.equal(v.bg, volume.bg); assert.equal(v.radius, volume.radius); assert.equal(v.shadow, volume.shadow);
    });
    await check('Only three rows, stable reserved check column, compact content-driven width and current/hover distinction', async () => {
      await load(); await click(trigger('project'));
      const geometry = () => evaluate('(()=>{const e=' + q(popup) + ',r=e.getBoundingClientRect(),rows=[...e.querySelectorAll("[role=menuitemradio]")];return {width:r.width,children:e.children.length,rows:rows.map(b=>{const l=b.querySelector(".playback-mode-label").getBoundingClientRect(),c=b.querySelector(".playback-mode-check").getBoundingClientRect();return {labelLeft:l.left,labelRight:l.right,checkLeft:c.left,checkWidth:c.width,checked:b.getAttribute("aria-checked"),text:b.querySelector(".playback-mode-check").textContent,bg:getComputedStyle(b).backgroundColor}})}})()');
      const before = await geometry(); assert.equal(before.children, 3); assert.equal(before.rows.filter(r => r.text === '✓').length, 1); assert(before.rows.every(r => r.checkWidth >= 16 && r.checkWidth <= 18 && r.checkLeft - r.labelRight >= 8 && r.checkLeft - r.labelRight <= 12));
      assert.equal(new Set(before.rows.map(r => r.checkLeft)).size, 1); assert(before.width < 220); await mouse('mouseMoved', await center(item('random')));
      const hovered = await geometry(); assert.equal(hovered.rows[1].checked, 'false'); assert.equal(hovered.rows[1].text, ''); assert.notEqual(hovered.rows[1].bg, hovered.rows[0].bg);
      await click(item('random')); await click(trigger('project')); const after = await geometry(); assert.equal(after.width, before.width); assert.deepEqual(after.rows.map(r => [r.labelLeft, r.checkLeft]), before.rows.map(r => [r.labelLeft, r.checkLeft]));
    });
    for (const scope of ['project', 'playlist']) for (const mode of modes) await check(scope + ': choose ' + mode + ' applies immediately, closes, preserves identity/runtime and authors only a real change', async () => {
      await load('playback-list-loop'); const ids = await state(), rt = await runtime(), rev = await revision(), previous = await value(scope), other = await value(scope === 'project' ? 'playlist' : 'project');
      await choose(scope, mode); assert.equal(await evaluate('!!' + q(popup)), false); assert.equal(await value(scope), mode); assert.equal(await evaluate(q(trigger(scope) + ' svg') + '.dataset.playbackModeIcon'), mode);
      assert.equal(await revision(), rev + (previous === mode ? 0 : 1)); await sameIdentity(ids); assert.deepEqual(await runtime(), rt); assert.equal(await value(scope === 'project' ? 'playlist' : 'project'), other);
    });
    await check('Selected B policy stays distinct from active A; switching A/B preserves each authored value', async () => {
      await load('playback-list-loop'); assert.equal((await state()).selectedPlaylistId, 'playlist-b'); assert.equal((await state()).activePlaylistId, 'playlist-a');
      await choose('playlist', 'random'); await click('[data-playlist-id="playlist-a"] .entity-select'); assert.equal(await value('playlist'), 'list-loop');
      await choose('playlist', 'repeat-current'); await click('[data-playlist-id="playlist-b"] .entity-select'); assert.equal(await value('playlist'), 'random'); assert.equal((await state()).activePlaylistId, 'playlist-a');
    });
    await check('No selected Playlist disables only its trigger without deriving mode from active runtime', async () => {
      await load('playback-no-selection'); assert.equal(await value('playlist'), ''); assert(await evaluate(q(trigger('playlist')) + '.disabled')); assert.equal(await evaluate(q(trigger('project')) + '.disabled'), false);
      await click(trigger('playlist')); await wait(400); assert.equal(await evaluate('!!' + q(popup)), false); assert.equal(await evaluate('!!' + q(tooltip)), false);
    });
    await check('Outside left click dismisses without swallowing target selection/focus or stealing it back', async () => {
      await load(); await click(trigger('project')); await click('[data-playlist-id="playlist-b"] .entity-select'); assert.equal(await evaluate('!!' + q(popup)), false); assert.equal((await state()).selectedPlaylistId, 'playlist-b');
      assert(await evaluate('document.activeElement===document.querySelector("[data-playlist-id=playlist-b] .entity-select")'));
    });
    await check('Outside right click on ordinary Web surface closes without preventing its event', async () => {
      await load(); await evaluate('window.__modeContext=null;document.querySelector(".rail-runtime-status").addEventListener("contextmenu",e=>{window.__modeContext={defaultPrevented:e.defaultPrevented,button:e.button};e.preventDefault()},{once:true})');
      await click(trigger('project')); await click('.rail-runtime-status', 'right'); assert.equal(await evaluate('!!' + q(popup)), false); assert.deepEqual(await evaluate('window.__modeContext'), { defaultPrevented: false, button: 2 });
    });
    for (const [name, code, number] of [['Enter', 'Enter', 13], [' ', 'Space', 32]]) await check('Keyboard ' + code + ' opens at current mode; arrows wrap; Enter selects and returns useful focus', async () => {
      await load(); await evaluate(q(trigger('project')) + '.focus()'); await key(name, code, number); assert.equal(await evaluate('document.activeElement.dataset.choiceValue'), 'repeat-current');
      await key('ArrowDown', 'ArrowDown', 40); assert.equal(await evaluate('document.activeElement.dataset.choiceValue'), 'list-loop'); await key('ArrowUp', 'ArrowUp', 38); assert.equal(await evaluate('document.activeElement.dataset.choiceValue'), 'repeat-current');
      await key('ArrowUp', 'ArrowUp', 38); const rev = await revision(); await key('Enter', 'Enter', 13); assert.equal(await value('project'), 'random'); assert.equal(await revision(), rev + 1); assert.equal(await evaluate('!!' + q(popup)), false); assert(await evaluate('document.activeElement===' + q(trigger('project'))));
    });
    await check('Current-item keyboard reselection closes without dirty; closed Escape remains unhandled by selector', async () => {
      await load(); const rev = await revision(); await evaluate(q(trigger('project')) + '.focus()'); await key('Enter', 'Enter', 13); await key('Enter', 'Enter', 13); assert.equal(await revision(), rev);
      await evaluate('window.__modeEscape=null;window.addEventListener("keydown",e=>{if(e.key==="Escape")window.__modeEscape=e.defaultPrevented},{once:true})'); await key('Escape', 'Escape', 27); assert.equal(await evaluate('window.__modeEscape'), false);
    });
    await check('Focus departure / Tab / window blur dismiss temporary popup without modal trapping', async () => {
      await load(); await click(trigger('project')); await evaluate('document.querySelector(".session-project-trigger").focus()'); await rest(); assert.equal(await evaluate('!!' + q(popup)), false);
      await click(trigger('project')); await key('Tab', 'Tab', 9); assert.equal(await evaluate('!!' + q(popup)), false); assert.equal(await evaluate('document.activeElement===' + q(trigger('project'))), false);
      await click(trigger('project')); await evaluate('window.dispatchEvent(new Event("blur"))'); await rest(); assert.equal(await evaluate('!!' + q(popup)), false);
    });
    await check('Two trigger instances transfer ownership without simultaneous menus/tooltips', async () => {
      await load(); await click(trigger('project')); await click(trigger('playlist')); assert.equal(await evaluate('document.querySelectorAll(' + JSON.stringify(popup) + ').length'), 1); assert.equal(await evaluate(q(popup) + '.dataset.modeScope'), 'playlist'); assert.equal(await evaluate('!!' + q(tooltip)), false);
      for (let i = 0; i < 4; i++) { await click(trigger('playlist')); assert.equal(await evaluate('!!' + q(popup)), false); await click(trigger('playlist')); assert.equal(await evaluate('document.querySelectorAll(' + JSON.stringify(popup) + ').length'), 1); }
    });
    await check('Either scope popup suppresses both mode tooltips, including hover over the other trigger', async () => {
      await load(); await click(trigger('project')); await hover(trigger('playlist')); await wait(400);
      assert.equal(await evaluate('!!' + q(tooltip)), false); assert(await evaluate('!!' + q(popup)));
      await key('Escape', 'Escape', 27); await wait(400); assert.equal(await evaluate('!!' + q(tooltip)), false);
      await hover(trigger('playlist')); await wait(400); assert.equal(await evaluate(q(tooltip) + '.textContent'), 'List Loop');
    });
    await check('Committed Chinese localizes tooltip/menu immediately; policy identity/dirty/runtime stay unchanged', async () => {
      await load(); const before = await state(), rt = await runtime(), rev = await revision(); await click(trigger('project')); await language('zh_CN');
      assert.deepEqual(await evaluate('[...document.querySelectorAll("[data-playback-mode-popup] .playback-mode-label")].map(e=>e.textContent)'), ['列表循环', '随机播放', '单曲循环']); assert.equal(await revision(), rev); await sameIdentity(before);
      const translatedRuntime = await runtime(); assert.deepEqual(translatedRuntime.rail, rt.rail); assert.equal(translatedRuntime.time, rt.time); assert.equal(translatedRuntime.state, '正在播放');
      await key('Escape', 'Escape', 27); const zh = '[data-playback-mode-trigger=project]'; await hover(zh); await wait(400); assert.equal(await evaluate(q(tooltip) + '.textContent'), '单曲循环');
      await language('en'); assert.equal(await evaluate(q(tooltip) + '.textContent'), 'Repeat Current'); assert.equal(await value('project'), 'repeat-current');
    });
    await check('English and Chinese widths follow content; selection keeps width/check alignment stable', async () => {
      await load(); await click(trigger('project')); const en = await evaluate(q(popup) + '.getBoundingClientRect().width'); await language('zh_CN'); const zh = await evaluate(q(popup) + '.getBoundingClientRect().width'); assert(en > zh); assert(zh >= 115 && zh < 190);
      await click(item('random')); await click('[data-playback-mode-trigger=project]'); assert.equal(await evaluate(q(popup) + '.getBoundingClientRect().width'), zh);
    });
    for (const [width, height] of [[1440, 900], [1280, 800], [1180, 760], [1101, 700]]) await check(`Open/close at ${width}×${height} preserves exact Rail/Timeline/Preview/library rectangles and anchor on resize`, async () => {
      await send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false }); await load(); const before = await layout(); await click(trigger('project')); assert.deepEqual(await layout(), before); await key('Escape', 'Escape', 27); assert.deepEqual(await layout(), before);
      await click(trigger('playlist')); assert.deepEqual(await layout(), before); await send('Emulation.setDeviceMetricsOverride', { width: width + 20, height, deviceScaleFactor: 1, mobile: false }); await rest();
      assert(await evaluate('!!' + q(popup)));
      const dx = await evaluate('(()=>{const e=' + q(popup) + ',p=e.getBoundingClientRect(),t=' + q(trigger('playlist')) + '.getBoundingClientRect(),a=getComputedStyle(e,"::after");return p.x+parseFloat(a.left)+parseFloat(a.width)/2-(t.x+t.width/2)})()'); assert(Math.abs(dx) < 1);
    });
    for (const width of [1000, 680, 440]) await check(`Resize to existing hidden-Library breakpoint ${width} dismisses an open popup without an orphan overlay`, async () => {
      await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false }); await load(); await click(trigger('playlist'));
      await send('Emulation.setDeviceMetricsOverride', { width, height: 700, deviceScaleFactor: 1, mobile: false }); await rest();
      assert.equal(await evaluate('getComputedStyle(document.querySelector(".session-library")).display'), 'none');
      assert.equal(await evaluate('!!' + q(popup)), false); assert.equal(await evaluate(q(trigger('playlist')) + '.getBoundingClientRect().width'), 0);
      const before = await layout(); await key('Escape', 'Escape', 27); assert.deepEqual(await layout(), before);
    });
    await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
    await check('Root document replacement removes an open popup without retained listeners/errors', async () => {
      await load(); await click(trigger('project')); await evaluate('document.querySelector(".session-project-trigger").focus()'); await key('Enter', 'Enter', 13); await click('[data-command=new]'); assert.equal(await evaluate('!!' + q(popup)), false); assert.equal((await state()).projectPath, '');
    });
    await check('Ctrl+N continues the original application command and dismisses its now-stale temporary mode popup', async () => {
      await load(); await click(trigger('project')); await key('n', 'KeyN', 78, 2); assert.equal((await state()).projectPath, ''); assert.equal(await evaluate('!!' + q(popup)), false);
    });
    for (const view of ['session', 'launcher', 'background-music']) await check(view + ' route smoke', async () => {
      const url = 'http://127.0.0.1:5173/?view=' + view; await send('Page.navigate', { url }); let ready = false;
      const selector = view === 'session' ? '.session-shell' : view === 'launcher' ? '.launcher-shell' : '.background-music-shell';
      for (let n = 0; n < 80; n++) { if (await evaluate('location.href===' + JSON.stringify(url) + '&&!!' + q(selector))) { ready = true; break; } await wait(50); } assert(ready); await rest();
    });
  }
  const result = { stage: red ? 'red' : edgeRed ? 'edge-red' : 'green', pass: checks.every(c => c.pass) && !runtimeErrors.length && !consoleWarnings.length, passed: checks.filter(c => c.pass).length, total: checks.length, checks, runtimeErrors, consoleWarnings };
  await fs.writeFile(new URL(red ? 'focused-red.json' : edgeRed ? 'focused-edge-red.json' : 'focused-green.json', evidence), JSON.stringify(result, null, 2) + '\n'); console.log(JSON.stringify(result, null, 2)); if (!result.pass) process.exitCode = 1;
} finally { ws.close(); await fetch('http://127.0.0.1:9227/json/close/' + tab.id); }
