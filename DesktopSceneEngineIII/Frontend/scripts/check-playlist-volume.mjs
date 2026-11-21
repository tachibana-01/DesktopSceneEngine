import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

const evidence = new URL('../node_modules/.cache/playlist-volume-002/', import.meta.url);
await fs.mkdir(evidence, { recursive: true });
const red = process.argv.includes('--red');
const restoreRed = process.argv.includes('--restore-red');
const tab = await (await fetch('http://127.0.0.1:9227/json/new?about:blank', { method: 'PUT' })).json();
const ws = new WebSocket(tab.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { ws.addEventListener('open', resolve, { once: true }); ws.addEventListener('error', reject, { once: true }); });
let serial = 0;
const pending = new Map(), checks = [], errors = [], warnings = [];
ws.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (message.method === 'Runtime.exceptionThrown') errors.push(message.params);
  if (message.method === 'Runtime.consoleAPICalled' && ['warning', 'error'].includes(message.params.type)) warnings.push(message.params);
  if (message.method === 'Log.entryAdded' && ['warning', 'error'].includes(message.params.entry.level)) warnings.push(message.params.entry);
  const request = pending.get(message.id);
  if (request) { pending.delete(message.id); message.error ? request.reject(message.error) : request.resolve(message.result); }
});
function send(method, params = {}) { return new Promise((resolve, reject) => { const id = ++serial; pending.set(id, { resolve, reject }); ws.send(JSON.stringify({ id, method, params })); }); }
async function evaluate(expression) { const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }); assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails)); return result.result.value; }
const q = selector => 'document.querySelector(' + JSON.stringify(selector) + ')';
const trigger = id => '[data-playlist-id=' + JSON.stringify(id) + '] .playlist-volume-trigger';
const popover = '.playlist-volume-popover';
const slider = '[data-playlist-master-slider]';
async function rest() { await evaluate('new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))'); }
async function load(scenario = 'normal') {
  const url = 'http://127.0.0.1:5173/?view=session&sessionScenario=' + scenario;
  await send('Page.navigate', { url }); let ready = false;
  for (let index = 0; index < 80; index++) { if (await evaluate('location.href===' + JSON.stringify(url) + '&&document.readyState==="complete"&&!!document.querySelector(".session-shell")')) { ready = true; break; } await new Promise(r => setTimeout(r, 50)); }
  assert(ready, 'Session ready'); await evaluate('document.fonts.ready'); await rest();
  await mouse('mouseMoved', { x: 700, y: 100 });
  if (await evaluate('!!' + q(popover))) await wait(140);
}
const snapshot = () => evaluate('({...document.querySelector(".session-shell").dataset})');
const percent = id => evaluate(q(trigger(id)) + '?.dataset.masterPercent');
async function center(selector) { assert(await evaluate('!!' + q(selector)), 'Expected UI control exists: ' + selector); return evaluate('(()=>{const r=' + q(selector) + '.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()'); }
async function mouse(type, point, down = false, deltaY) { await send('Input.dispatchMouseEvent', { type, ...point, button: type === 'mouseMoved' || type === 'mouseWheel' ? down ? 'left' : 'none' : 'left', buttons: down ? 1 : 0, clickCount: ['mousePressed', 'mouseReleased'].includes(type) ? 1 : 0, ...(deltaY === undefined ? {} : { deltaX: 0, deltaY }) }); await rest(); }
async function hover(id) {
  const owner = await evaluate(q(popover) + '?.dataset.playlistId');
  // The approved downward body intentionally covers speakers below it. Leave
  // that surface first, as a user would, rather than misattributing its hits.
  if (owner && owner !== id) { await mouse('mouseMoved', { x: 700, y: 100 }); await wait(140); }
  const p = await center(trigger(id));
  assert(await evaluate('document.elementFromPoint(' + p.x + ',' + p.y + ')?.closest(".playlist-volume-trigger")===' + q(trigger(id))), 'Actual pointer hits the requested speaker');
  await mouse('mouseMoved', p);
  assert.equal(await evaluate(q(popover) + '?.dataset.playlistId'), id);
}
async function click(selector) {
  if (selector.includes('.playlist-volume-trigger')) {
    assert(await evaluate('!!' + q(selector)), 'Expected speaker exists');
    await hover(await evaluate(q(selector) + '.closest("[data-playlist-id]").dataset.playlistId'));
  }
  const p = await center(selector); await mouse('mouseMoved', p); await mouse('mousePressed', p, true); await mouse('mouseReleased', p);
}
async function setSlider(value) { await evaluate('(()=>{const e=' + q(slider) + ';Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,"value").set.call(e,' + value + ');e.dispatchEvent(new Event("input",{bubbles:true}));})()'); await rest(); }
async function check(name, run) { try { await run(); checks.push({ name, pass: true }); } catch (error) { checks.push({ name, pass: false, error: error.message }); } }
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const rail = () => evaluate('({...document.querySelector(".playback-rail").dataset})');
const revision = async () => Number((await snapshot()).revision);
const identityFields = ['selectedPlaylistId', 'selectedItemId', 'activePlaylistId', 'activeItemId'];
async function sameIdentity(before) { const after = await snapshot(); for (const field of identityFields) assert.equal(after[field], before[field], field); }
async function input(selector, value) { await evaluate('(()=>{const e=' + q(selector) + ';Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,"value").set.call(e,' + JSON.stringify(String(value)) + ');e.dispatchEvent(new Event("input",{bubbles:true}));})()'); await rest(); }
async function domClick(selector) { assert(await evaluate('!!' + q(selector)), selector); await evaluate(q(selector) + '.click()'); await rest(); }
async function command(id, action) { await domClick('[data-playlist-id=' + JSON.stringify(id) + '] .entity-more'); await domClick('[data-command=' + JSON.stringify(action) + ']'); }
const layout = () => evaluate('(()=>{const rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}};return {rows:[...document.querySelectorAll(".playlist-entity")].map(e=>[e.dataset.playlistId,rect(e)]),panels:[".playlist-panel",".session-library",".session-workspace",".preview-viewport",".session-timeline",".playback-rail"].map(s=>rect(document.querySelector(s)))};})()');

try {
  await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await check('Every Playlist row has a speaker immediately left of its existing menu', async () => {
    await load(); assert.equal(await evaluate('document.querySelectorAll(".playlist-volume-trigger").length'), 4);
    assert(await evaluate('[...document.querySelectorAll(".playlist-volume-trigger")].every(e=>e.nextElementSibling?.classList.contains("entity-more"))'));
  });
  await check('Rail active Playlist Master is removed from the DOM, Output Mute retained', async () => {
    await load(); assert.equal(await evaluate('!!' + q('.rail-master,#rail-master,[aria-label="Active Playlist Master"]')), false);
    assert(await evaluate('!!document.querySelector(".rail-mute")'));
  });
  await check('Row Master explicitly targets C without selecting or activating C', async () => {
    await load(); const before = await snapshot(); await hover('playlist-c'); await setSlider(37);
    assert.equal(await percent('playlist-c'), '37'); const after = await snapshot();
    for (const field of ['selectedPlaylistId', 'selectedItemId', 'activePlaylistId', 'activeItemId']) assert.equal(after[field], before[field]);
  });
  await check('Speaker hover immediately opens a downward overlay popover', async () => {
    await load(); await hover('playlist-a'); assert(await evaluate('!!' + q(popover)));
    assert(await evaluate(q(popover) + '.getBoundingClientRect().top>' + q(trigger('playlist-a')) + '.getBoundingClientRect().bottom'));
  });
  await check('Speaker click mutes and restores authored Master', async () => {
    await load(); await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '0');
    await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '100');
  });
  if (!red) {
    await check('72 → click 0 → restore 72; active output, Inspector and dirty share authored truth', async () => {
      await load('playlist-volume-default'); const ids = await snapshot(), rev = await revision();
      await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '0'); assert.equal((await rail()).embeddedGain, '0'); assert.equal(await evaluate(q('[aria-label="Playlist Master Volume"]') + '.value'), '0');
      await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '72'); assert(Math.abs(Number((await rail()).embeddedGain) - .72) < 1e-6); assert.equal(await revision(), rev + 2); await sameIdentity(ids);
    });
    await check('Latest nonzero from row slider and Inspector is remembered; zero never overwrites it', async () => {
      await load('playlist-volume-default'); await hover('playlist-a'); await setSlider(40); await setSlider(0); await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '40');
      await input('[aria-label="Playlist Master Volume"]', 23); await input('[aria-label="Playlist Master Volume"]', 0); await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '23');
    });
    for (const path of ['native row zero endpoint', 'row keyboard Home', 'Inspector keyboard Home']) await check(`Untouched 40 → ${path} 0 → speaker restores 40`, async () => {
      await load('playlist-volume-last-nonzero');
      const ids = await snapshot(), rev = await revision();
      assert.equal(await percent('playlist-a'), '40');
      if (path === 'native row zero endpoint') {
        await hover('playlist-a');
        const point = await evaluate('(()=>{const r=' + q(slider) + '.getBoundingClientRect();return {x:r.x+r.width/2,y:r.bottom-4}})()');
        await mouse('mouseMoved', point); await mouse('mousePressed', point, true); await mouse('mouseReleased', point);
      } else {
        if (path === 'row keyboard Home') await hover('playlist-a');
        const selector = path === 'row keyboard Home' ? slider : '[aria-label="Playlist Master Volume"]';
        await evaluate(q(selector) + '.focus()');
        for (const type of ['keyDown', 'keyUp']) await send('Input.dispatchKeyEvent', { type, key: 'Home', code: 'Home', windowsVirtualKeyCode: 36 });
        await rest();
      }
      assert.equal(await percent('playlist-a'), '0');
      await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '40');
      assert.equal(await revision(), rev + 2); await sameIdentity(ids);
      assert(Math.abs(Number((await rail()).embeddedGain) - .4) < 1e-6);
    });
    await check('Zero without transient history restores exactly 100', async () => {
      await load('playlist-volume-no-history'); assert.equal(await percent('playlist-a'), '0'); await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '100');
    });
    await check('Selected A / active B / target C: all five interactions preserve selection, active IDs and current output', async () => {
      await load('playlist-volume-selected-vs-target'); const ids = await snapshot(), output = await rail();
      await hover('playlist-c'); await sameIdentity(ids); await click(trigger('playlist-c')); await sameIdentity(ids);
      await mouse('mouseWheel', await center(trigger('playlist-c')), false, -120); await sameIdentity(ids);
      await setSlider(37); await sameIdentity(ids); await mouse('mouseWheel', await center(popover), false, 120); await sameIdentity(ids);
      assert.equal(await percent('playlist-c'), '32'); assert.equal((await rail()).embeddedGain, output.embeddedGain); assert.equal(await percent('playlist-a'), '72'); assert.equal(await percent('playlist-b'), '50');
    });
    await check('Duplicate aliases retain independent per-ID volume and restore history', async () => {
      await load('playlist-volume-duplicate-alias'); const ids = await snapshot();
      for (const [id, value] of [['playlist-a', 37], ['playlist-b', 63]]) { await hover(id); await setSlider(value); await click(trigger(id)); assert.equal(await percent(id), '0'); }
      await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '37'); assert.equal(await percent('playlist-b'), '0');
      await click(trigger('playlist-b')); assert.equal(await percent('playlist-b'), '63'); await sameIdentity(ids);
    });
    await check('Rename/reorder preserve ID and history; existing menu/Configure BGM target remains exact', async () => {
      await load('playlist-volume-duplicate-alias'); await hover('playlist-b'); await setSlider(61); await click(trigger('playlist-b'));
      await command('playlist-b', 'rename'); await input('[data-rename-value]', 'Renamed Scene'); await domClick('[data-dialog-confirm]');
      await command('playlist-b', 'move-up'); await click(trigger('playlist-b')); assert.equal(await percent('playlist-b'), '61'); assert.equal(await percent('playlist-a'), '72');
      await command('playlist-b', 'background-music'); assert.equal(await evaluate(q('[data-configure-playlist-id]') + '.dataset.configurePlaylistId'), 'playlist-b'); assert.equal(await evaluate('!!document.querySelector(".background-music-shell")'), false);
    });
    for (const [start, delta, expected] of [[50, -120, 55], [50, 120, 45], [97, -120, 100], [2, 120, 0], [0, -120, 5]]) await check(`Wheel on speaker ${start} → ${expected}, relative ±5 with clamp`, async () => {
      await load('playlist-volume-wheel'); const ids = await snapshot(); await hover('playlist-a'); await setSlider(start);
      await mouse('mouseWheel', await center(trigger('playlist-a')), false, delta); assert.equal(await percent('playlist-a'), String(expected)); await sameIdentity(ids);
      if (expected === 5) { await click(trigger('playlist-a')); await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '5'); }
    });
    await check('Vertical slider supports exact integer 0/1/37/99/100 and latest-nonzero restore', async () => {
      await load('playlist-volume-default'); const ids = await snapshot(); await hover('playlist-a');
      for (const value of [0, 1, 37, 99, 100]) { await setSlider(value); assert.equal(await percent('playlist-a'), String(value)); assert.equal(await evaluate(q('.playlist-volume-percent') + '.textContent'), value + '%'); }
      assert.equal(await evaluate(q(slider) + '.step'), '1'); assert.equal(await evaluate(q(slider) + '.getAttribute("aria-orientation")'), 'vertical');
      await setSlider(37); await setSlider(0); await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '37'); await sameIdentity(ids);
    });
    await check('Actual pointer drag reaches exact 37%, up=louder, and cannot select or activate', async () => {
      await load('playlist-volume-default'); const ids = await snapshot(); await hover('playlist-c');
      // The styled native thumb is 8px. Chromium does not expose its shadow
      // pseudo box via getComputedStyle (it returns the input's 116px height).
      const points = await evaluate('(()=>{const r=' + q(slider) + '.getBoundingClientRect();return {x:r.x+r.width/2,top:r.top+4,bottom:r.bottom-4}})()');
      await mouse('mouseMoved', { x: points.x, y: points.top });
      await mouse('mousePressed', { x: points.x, y: points.top }, true); assert.equal(await percent('playlist-c'), '100');
      await mouse('mouseMoved', { x: points.x, y: points.bottom - (points.bottom - points.top) * .37 }, true);
      await mouse('mouseReleased', { x: points.x, y: points.bottom - (points.bottom - points.top) * .37 }); assert.equal(await percent('playlist-c'), '37'); await sameIdentity(ids);
    });
    await check('Speaker and popover wheel consume without scrolling; name wheel keeps normal Playlist scrolling', async () => {
      await load('playlist-volume-bottom-row'); await evaluate('document.querySelector(".media-list").scrollTop=200'); await rest();
      const id = 'volume-extra-2'; await hover(id); const before = await evaluate('document.querySelector(".media-list").scrollTop');
      await mouse('mouseWheel', await center(trigger(id)), false, -120); assert.equal(await percent(id), '55');
      await mouse('mouseWheel', await center(popover), false, 120); assert.equal(await percent(id), '50'); assert.equal(await evaluate('document.querySelector(".media-list").scrollTop'), before);
      const p = await center('[data-playlist-id="' + id + '"] .entity-alias'); await mouse('mouseMoved', p); await mouse('mouseWheel', p, false, 120); await wait(140);
      assert((await evaluate('document.querySelector(".media-list").scrollTop')) > before); assert.equal(await percent(id), '50');
    });
    await check('Invisible corridor keeps hover, passes wheel to list, and never adjusts Master', async () => {
      await load('playlist-volume-bottom-row'); await hover('playlist-a'); const before = await percent('playlist-a');
      const p = await evaluate('(()=>{const r=' + q(trigger('playlist-a')) + '.getBoundingClientRect();return {x:r.x+r.width/2,y:r.bottom+3}})()');
      await mouse('mouseMoved', p); await wait(120); assert(await evaluate('!!' + q(popover)));
      assert.equal(await evaluate('document.elementFromPoint(' + p.x + ',' + p.y + ')?.classList.contains("playlist-volume-corridor")'), false);
      await mouse('mouseWheel', p, false, 100); await wait(140); assert.equal(await percent('playlist-a'), before); assert((await evaluate('document.querySelector(".media-list").scrollTop')) > 0);
    });
    await check('Speaker → gap → upward arrow → body stays open; shape is detached, continuous and unbordered', async () => {
      await load('playlist-volume-hover'); await hover('playlist-a');
      const g = await evaluate('(()=>{const t=' + q(trigger('playlist-a')) + '.getBoundingClientRect(),b=' + q(popover) + ',r=b.getBoundingClientRect(),a=getComputedStyle(b,"::before"),c=getComputedStyle(document.querySelector(".playlist-volume-corridor"));return {x:t.x+t.width/2,triggerBottom:t.bottom,top:r.top,bottom:r.bottom,arrowTop:r.top+parseFloat(a.top),arrowBottom:r.top+parseFloat(a.top)+parseFloat(a.height),arrowColor:a.backgroundColor,bodyColor:getComputedStyle(b).backgroundColor,arrowBorder:a.borderTopWidth,clip:a.clipPath,corridorBackground:c.backgroundColor,corridorBorder:c.borderTopWidth,corridorPointer:c.pointerEvents}})()');
      assert(g.arrowTop > g.triggerBottom); assert.equal(g.arrowBottom, g.top); assert.equal(g.arrowColor, g.bodyColor); assert.equal(g.arrowBorder, '0px'); assert(g.clip.includes('50%')); assert.equal(g.corridorBackground, 'rgba(0, 0, 0, 0)'); assert.equal(g.corridorBorder, '0px'); assert.equal(g.corridorPointer, 'none');
      for (const y of [g.triggerBottom + 3, g.arrowTop + 2, g.top + 15, g.bottom - 15]) { await mouse('mouseMoved', { x: g.x, y }); await wait(110); assert(await evaluate('!!' + q(popover))); }
    });
    await check('Leaving retention closes after short tolerance; reentry cancels pending close; only one popover', async () => {
      await load('playlist-volume-hover'); await hover('playlist-a'); await mouse('mouseMoved', { x: 700, y: 300 }); await wait(140); assert.equal(await evaluate('!!' + q(popover)), false);
      await hover('playlist-a'); await mouse('mouseMoved', { x: 700, y: 300 }); await wait(30); await hover('playlist-a'); await wait(140); assert(await evaluate('!!' + q(popover)));
      await hover('playlist-b'); assert.equal(await evaluate('document.querySelectorAll(".playlist-volume-popover").length'), 1); assert.equal(await evaluate(q(popover) + '.dataset.playlistId'), 'playlist-b');
    });
    await check('Continued pointer motion outside cannot restart the short close deadline', async () => {
      await load('playlist-volume-hover'); await hover('playlist-a');
      for (const x of [650, 700, 750, 800]) { await mouse('mouseMoved', { x, y: 300 }); await wait(30); }
      assert.equal(await evaluate('!!' + q(popover)), false);
    });
    await check('Closed/open/dragging/muted/restored leave every row, Items and MainWindow rectangle unchanged', async () => {
      await load('playlist-volume-default'); const before = await layout(); await hover('playlist-a'); assert.deepEqual(await layout(), before);
      await click(trigger('playlist-a')); assert.deepEqual(await layout(), before); await click(trigger('playlist-a')); assert.deepEqual(await layout(), before);
      const p = await center(slider); await mouse('mousePressed', p, true); await mouse('mouseMoved', { x: p.x, y: p.y + 20 }, true); assert.deepEqual(await layout(), before); await mouse('mouseReleased', { x: p.x, y: p.y + 20 });
      await mouse('mouseMoved', { x: 700, y: 300 }); await wait(140); assert.deepEqual(await layout(), before);
    });
    await check('Bottom-row popover always goes downward and renders over lower Items without list clipping', async () => {
      await load('playlist-volume-bottom-row'); await evaluate('document.querySelector(".media-list").scrollTop=document.querySelector(".media-list").scrollHeight'); await rest(); await hover('volume-extra-19');
      const g = await evaluate('(()=>{const b=' + q(popover) + '.getBoundingClientRect(),t=' + q(trigger('volume-extra-19')) + '.getBoundingClientRect(),p=document.querySelector(".media-panel").getBoundingClientRect();return {top:b.top,triggerBottom:t.bottom,panelBottom:p.bottom,bodyBottom:b.bottom,hit:!!document.elementFromPoint(b.left+8,Math.max(b.top+10,p.bottom+10))?.closest(".playlist-volume-popover")}})()');
      assert.equal(g.top, g.triggerBottom + 12); assert(g.bodyBottom > g.panelBottom); assert(g.hit); assert.equal(await evaluate('getComputedStyle(document.querySelector(".playlist-volume-overlay")).position'), 'fixed');
    });
    await check('Hover/open/close and clamped no-value wheels do not dirty Project', async () => {
      await load(); const rev = await revision(); await hover('playlist-a'); await mouse('mouseWheel', await center(trigger('playlist-a')), false, -120); assert.equal(await revision(), rev);
      await mouse('mouseMoved', { x: 700, y: 300 }); await wait(140); assert.equal(await revision(), rev);
      await load('playlist-volume-zero'); const zeroRev = await revision(); await hover('playlist-a'); await mouse('mouseWheel', await center(popover), false, 120); assert.equal(await percent('playlist-a'), '0'); assert.equal(await revision(), zeroRev);
    });
    await check('Row and Inspector are one property; Output Mute is separate and preserved', async () => {
      await load('playlist-volume-default'); await hover('playlist-a'); await setSlider(43); assert.equal(await evaluate(q('[aria-label="Playlist Master Volume"]') + '.value'), '43');
      await input('[aria-label="Playlist Master Volume"]', 0); assert.equal(await percent('playlist-a'), '0'); assert.equal(await evaluate(q(trigger('playlist-a')) + '.dataset.muted'), 'true');
      const rev = await revision(); await domClick('.rail-mute'); assert.equal((await rail()).outputMuted, 'true'); assert.equal(await revision(), rev);
      await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '43'); assert.equal((await rail()).outputMuted, 'true'); assert.equal((await rail()).effectiveOutput, '0');
      await domClick('.rail-mute'); assert(Math.abs(Number((await rail()).effectiveOutput) - .43) < 1e-6);
      for (const label of ['Previous', 'Pause', 'Next']) assert(await evaluate('!!' + q('[aria-label="' + label + '"]'))); assert(await evaluate('!!document.querySelector(".session-timeline")'));
    });
    await check('Save/Open preserves authored zero but not restore memory; no-history fallback is deterministic', async () => {
      await load('playlist-volume-default'); await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '0');
      await domClick('.session-project-trigger'); await domClick('[data-command="save"]'); assert.equal((await snapshot()).dirty, 'false');
      await domClick('.session-project-trigger'); await domClick('[data-command="open"]');
      await evaluate('(()=>{const e=document.querySelector("[data-picker-value]");e.value="C:/MockProjects/editorial.json";e.dispatchEvent(new Event("change",{bubbles:true}));})()'); await domClick('[data-dialog-confirm]');
      assert.equal(await percent('playlist-a'), '0');
      // Programmatic dialog clicks leave the physical pointer on the old
      // speaker. Cross its boundary again, as a real return from the picker
      // does, before asserting the immediate hover popover.
      await mouse('mouseMoved', { x: 700, y: 100 }); await wait(140);
      await click(trigger('playlist-a')); assert.equal(await percent('playlist-a'), '100');
    });
    for (const view of ['session', 'launcher', 'background-music']) await check(view + ' route smoke', async () => {
      const url = 'http://127.0.0.1:5173/?view=' + view; await send('Page.navigate', { url });
      const selector = view === 'session' ? '.session-shell' : view === 'launcher' ? '.launcher-shell' : '.background-music-shell'; let ready = false;
      for (let i = 0; i < 80; i++) { if (await evaluate('location.href===' + JSON.stringify(url) + '&&!!' + q(selector))) { ready = true; break; } await wait(50); } assert(ready); await rest();
    });
  }

  const result = { stage: red ? 'red' : restoreRed ? 'restore-red' : 'green', pass: checks.every(c => c.pass) && !errors.length && !warnings.length, passed: checks.filter(c => c.pass).length, total: checks.length, checks, runtimeErrors: errors, consoleWarnings: warnings };
  await fs.writeFile(new URL(red ? 'focused-red.json' : restoreRed ? 'focused-restore-red.json' : 'focused-green.json', evidence), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result, null, 2)); if (!result.pass) process.exitCode = 1;
} finally { ws.close(); await fetch('http://127.0.0.1:9227/json/close/' + tab.id); }
