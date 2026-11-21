import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// DOM and keyboard QA in an isolated headless Chrome; never a user's browser.
const evidence = new URL('../node_modules/.cache/launcher-v12/', import.meta.url);
await fs.mkdir(evidence, { recursive: true });
const tabs = await (await fetch('http://127.0.0.1:9227/json/list')).json();
const tab = tabs.find(item => item.type === 'page');
assert(tab, 'Isolated QA page exists');
const socket = new WebSocket(tab.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  socket.addEventListener('open', resolve, { once: true });
  socket.addEventListener('error', reject, { once: true });
});
let serial = 0;
const pending = new Map(), checks = [], runtimeErrors = [], warnings = [];
socket.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (message.method === 'Runtime.exceptionThrown') runtimeErrors.push(message.params.exceptionDetails);
  if (message.method === 'Log.entryAdded' && ['warning', 'error'].includes(message.params.entry.level)) warnings.push(message.params.entry);
  if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') warnings.push(message.params);
  const request = pending.get(message.id);
  if (request) {
    pending.delete(message.id);
    message.error ? request.reject(message.error) : request.resolve(message.result);
  }
});
function send(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++serial;
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
  });
}
async function evaluate(expression) {
  const response = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  assert(!response.exceptionDetails, JSON.stringify(response.exceptionDetails));
  return response.result.value;
}
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function waitFor(expression, label = expression) {
  for (let attempt = 0; attempt < 70; attempt++) {
    if (await evaluate(expression)) return;
    await delay(50);
  }
  assert.fail('Timed out: ' + label);
}
async function rest() {
  await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
  await evaluate('Promise.allSettled(document.getAnimations().map(animation => animation.finished))');
}
async function click(selector, settle = true) {
  assert(await evaluate('!!document.querySelector(' + JSON.stringify(selector) + ')'), 'Control exists: ' + selector);
  // HTMLElement.click alone omits the pointer's normal focus transfer.
  await evaluate('(() => { const control=document.querySelector(' + JSON.stringify(selector) + '); control.focus(); control.click(); })()');
  if (settle) await rest();
}
async function key(key, code, number, shiftKey = false) {
  await send('Input.dispatchKeyEvent', { type: 'keyDown', key, code, windowsVirtualKeyCode: number,
    modifiers: shiftKey ? 8 : 0, ...(key === 'Enter' ? { text: '\r' } : {}) });
  await send('Input.dispatchKeyEvent', { type: 'keyUp', key, code, windowsVirtualKeyCode: number, modifiers: shiftKey ? 8 : 0 });
  await rest();
}
async function choose(selector, value) {
  await evaluate('(() => { const select=document.querySelector(' + JSON.stringify(selector) + '); select.value=' + JSON.stringify(value) + '; select.dispatchEvent(new Event("change",{bubbles:true})); })()');
  await rest();
}
async function load(scenario = 'normal', extra = '') {
  await send('Page.navigate', { url: 'http://127.0.0.1:5173/?view=launcher&launcherScenario=' + scenario + extra });
  await waitFor('document.readyState === "complete" && !!document.querySelector(".launcher-shell")');
  await rest();
}
async function status(expected) {
  assert.equal(await evaluate('document.querySelector(".detail-heading .display-status")?.textContent.trim()'), expected);
  assert.equal(await evaluate('document.querySelector(".is-focused .display-caption .display-status")?.textContent.trim()'), expected, 'Deck and Detail agree');
}
async function action(label, disabled) {
  assert.equal(await evaluate('document.querySelector(".editorial-action")?.textContent.trim()'), label);
  assert.equal(await evaluate('document.querySelector(".editorial-action")?.disabled'), disabled);
}
const passed = check => checks.push({ check, pass: true });
try {
  await send('Runtime.enable'); await send('Page.enable'); await send('Log.enable'); await send('Log.clear');
  await send('Page.bringToFront');
  await send('Emulation.setEmulatedMedia', { features: [] });
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await load();
  assert(await evaluate('!!document.querySelector("button[aria-label=Settings]")'), 'Settings is a semantic shell action');
  await status('SESSION ACTIVE'); await action('OPEN SESSION', false);
  await click('.editorial-action');
  assert(await evaluate('document.querySelector(".launcher-operation").textContent.includes("Session activated.")'));
  assert.equal(new URL(await evaluate('location.href')).searchParams.get('view'), 'launcher', 'Open stays mock only');
  passed('Normal display/session model; Open produces feedback without navigation');

  await click('[aria-label="Settings"]');
  assert(await evaluate('document.querySelector("dialog").open && document.activeElement.id === "launcher-language"'), 'Dialog opens with language focus');
  await choose('#launcher-language', 'zh-CN');
  assert.equal(await evaluate('document.querySelector(".launcher-shell").dataset.language'), 'en');
  await click('[data-settings-cancel]');
  assert.equal(await evaluate('document.querySelector(".launcher-shell").dataset.language'), 'en');
  assert.equal(await evaluate('document.activeElement.getAttribute("aria-label")'), 'Settings', 'Cancel restores opener focus');
  await click('[aria-label="Settings"]');
  assert.equal(await evaluate('document.querySelector("#launcher-language").value'), 'en', 'Cancel discards staging');
  await choose('#launcher-language', 'zh-CN');
  await click('[data-settings-close]');
  await click('[aria-label="Settings"]');
  assert.equal(await evaluate('document.querySelector("#launcher-language").value'), 'en', 'Close discards staging');
  await choose('#launcher-language', 'zh-CN');
  await click('[data-settings-apply]', false);
  await waitFor('!document.querySelector("dialog")');
  assert.equal(await evaluate('document.querySelector(".launcher-shell").dataset.language'), 'zh-CN');
  assert.equal(await evaluate('document.querySelector("h1").textContent'), '显示器');
  passed('Settings stage / Cancel / close discard; Apply commits partial shell language');

  await load('settings-failure');
  await click('[aria-label="Settings"]');
  await choose('#launcher-language', 'zh-CN');
  await click('[data-settings-apply]', false);
  await waitFor('document.querySelector(".settings-result")?.textContent.includes("Unable to save settings.")');
  assert.equal(await evaluate('document.querySelector(".launcher-shell").dataset.language'), 'en');
  assert.equal(await evaluate('document.querySelector("#launcher-language").value'), 'zh-CN');
  assert(await evaluate('document.querySelector(".settings-effective").textContent.includes("English")'));
  await evaluate('document.querySelector("[data-settings-apply]").focus()');
  await key('Tab', 'Tab', 9);
  assert(await evaluate('document.activeElement === document.querySelector("[data-settings-close]")'), 'Modal traps forward Tab');
  await key('Tab', 'Tab', 9, true);
  assert(await evaluate('document.activeElement === document.querySelector("[data-settings-apply]")'), 'Modal traps reverse Tab');
  await key('Escape', 'Escape', 27);
  assert(!await evaluate('!!document.querySelector("dialog")'));
  passed('Settings failure preserves staged/effective values; modal keyboard containment and Escape');

  await load();
  await click('[aria-label="Refresh displays"]', false);
  assert(await evaluate('document.querySelector(".launcher-shell").dataset.refreshState === "refreshing"'));
  assert(await evaluate('document.querySelector(".launcher-refresh").disabled'));
  await waitFor('document.querySelector(".launcher-shell").dataset.refreshState === "success"');
  assert.equal(await evaluate('getComputedStyle(document.querySelector(".launcher-refresh svg")).animationName'), 'none');
  passed('Refresh pending/disabled, finite rotation and success');

  await load('refresh-failure');
  await click('[aria-label="Refresh displays"]', false);
  await waitFor('document.querySelector(".launcher-shell").dataset.refreshState === "enumerationFailure"');
  assert(await evaluate('document.querySelector(".launcher-empty").textContent.includes("Unable to load displays.")'));
  assert(!await evaluate('document.body.textContent.includes("0 connected")'));
  await click('[data-refresh-retry]', false);
  assert(await evaluate('document.querySelector(".launcher-shell").dataset.refreshState === "refreshing"'));
  assert(!await evaluate('!!document.querySelector(".display-deck")'), 'Failed enumeration remains gated until retry succeeds');
  await waitFor('document.querySelector(".launcher-shell").dataset.refreshState === "success"');
  await waitFor('document.querySelector(".display-deck")?.dataset.displayCount === "5"');
  passed('Enumeration failure distinct from empty; Retry shares Refresh and restores Deck');
  await load('no-displays');
  assert.equal(await evaluate('document.querySelector(".page-identity p").textContent'), '0 connected');
  assert.equal(await evaluate('document.querySelector(".launcher-empty h2").textContent'), 'No available displays');
  assert(!await evaluate('!!document.querySelector(".display-deck, [data-refresh-retry]")'));
  passed('Successful zero-display result renders true empty state');

  await load('display-disappearance');
  await click('[aria-label="Go to DISPLAY D"]');
  await click('[aria-label="Refresh displays"]', false);
  await waitFor('document.querySelector(".display-deck")?.dataset.displayCount === "4"');
  assert.equal(await evaluate('document.querySelector("#detail-title").textContent'), 'DISPLAY E');
  assert.equal(await evaluate('document.querySelectorAll(".deck-indicator").length'), 4);
  assert.equal(await evaluate('document.querySelector(".page-identity p").textContent'), '4 connected');
  assert(!await evaluate('!!document.querySelector("[data-display-id=display-d]")'));
  await load('display-disappearance', '&displays=1');
  await click('[aria-label="Refresh displays"]', false);
  await waitFor('document.querySelector(".launcher-empty h2")?.textContent === "No available displays"');
  assert.equal(await evaluate('document.querySelector(".page-identity p").textContent'), '0 connected');
  passed('Selected display disappearance reflows selection/count/indicators/detail; last removal empties');

  await load();
  assert(!await evaluate('!!document.querySelector("button button")'), 'No nested buttons');
  assert(await evaluate('(() => { const name=document.querySelector(".display-identify"); const r=name.getBoundingClientRect(); return name.contains(document.elementFromPoint(r.x+r.width/2,r.y+r.height/2)); })()'), 'Selected name is the real pointer hit target');
  assert(await evaluate('(() => { const v=document.querySelector(".deck-viewport").getBoundingClientRect(); const names=[...document.querySelectorAll(".display-slide:not([aria-hidden]):not(.is-focused) .display-name")].filter(name=>{const r=name.getBoundingClientRect();return r.x>=v.x+24&&r.right<=v.right-24}); return names.length>0&&names.every(name=>{const r=name.getBoundingClientRect();return document.elementFromPoint(r.x+r.width/2,r.y+r.height/2)?.closest(".display-surface")===name.closest(".display-surface")}); })()'), 'Visible nonselected name hits selection surface only');
  await click('.display-slide:not([aria-hidden]) [data-display-id="display-d"]');
  await status('IDENTIFICATION REQUIRED'); await action('CREATE SESSION', true);
  assert.equal(await evaluate('document.querySelectorAll(".display-identify").length'), 1, 'Only selected name is Identify');
  await click('[data-selected="true"]');
  await status('IDENTIFICATION REQUIRED');
  assert.equal(await evaluate('document.querySelector(".display-identify").dataset.identifyState'), 'idle', 'Body does not identify');
  await evaluate('document.querySelector(".display-identify").focus()');
  await key('Enter', 'Enter', 13);
  assert.equal(await evaluate('document.querySelector(".display-identify").dataset.identifyState'), 'identifying');
  await action('CREATE SESSION', true);
  await waitFor('document.querySelector(".display-identify")?.dataset.identifyState === "success"');
  await status('AVAILABLE'); await action('CREATE SESSION', false);
  assert(await evaluate('document.querySelector(".launcher-operation").textContent.includes("Display identification shown.")'));
  passed('Nonselected selection vs selected body vs keyboard Name Identify; identity gating unlocks after success');
  await load('identify-failure');
  await click('.display-identify', false);
  await waitFor('document.querySelector(".display-identify")?.dataset.identifyState === "failure"');
  await status('IDENTIFICATION REQUIRED'); await action('CREATE SESSION', true);
  assert(await evaluate('document.querySelector(".launcher-operation").textContent.includes("Unable to identify this display.")'));
  passed('Identify failure stays gated and exposes restrained feedback');

  await load();
  await click('[aria-label="Go to DISPLAY B"]');
  await evaluate('for(let i=0;i<6;i++)document.querySelector(".editorial-action").click()');
  await status('STARTING…'); await action('STARTING…', true);
  await click('[aria-label="Go to DISPLAY A"]');
  await click('[aria-label="Refresh displays"]', false);
  await waitFor('document.querySelector(".launcher-shell").dataset.refreshState === "success"');
  await click('[aria-label="Go to DISPLAY B"]');
  await waitFor('document.querySelector(".detail-heading .display-status").textContent.trim() === "SESSION ACTIVE"');
  await status('SESSION ACTIVE'); await action('OPEN SESSION', false);
  passed('Create is single-flight, visibly Starting, then Active; navigation/Refresh preserves lifecycle');
  await load();
  await click('[aria-label="Go to DISPLAY B"]');
  await click('.editorial-action', false);
  await choose('#launcher-scenario', 'normal');
  await delay(1800);
  await click('[aria-label="Go to DISPLAY B"]');
  await status('AVAILABLE');
  passed('Dev reset cancels pending lifecycle completions without stale state writes');
  await load('starting');
  await status('STARTING…'); await action('STARTING…', true);
  await waitFor('document.querySelector(".detail-heading .display-status").textContent.trim() === "SESSION ACTIVE"');
  passed('Starting scenario completes to Active');

  await load('closing');
  await status('SESSION ACTIVE');
  await click('[data-dev-closing]', false);
  await status('CLOSING…'); await action('CLOSING…', true);
  assert(await evaluate('document.querySelector(".window-control--hide").disabled'));
  await waitFor('document.querySelector(".detail-heading .display-status").textContent.trim() === "AVAILABLE"');
  await action('CREATE SESSION', false);
  passed('Dev Active → Closing blocks Open/Hide then returns Available; no product Close Session action');
  await load('disconnected');
  await status('SESSION DISCONNECTED');
  assert(!await evaluate('!!document.querySelector(".editorial-action")'));
  assert(await evaluate('document.querySelector(".session-context").textContent.includes("Recovery is handled by the application.")'));
  assert(!await evaluate('[...document.querySelectorAll("button")].some(e=>/RECONNECT|RECOVER|CREATE SESSION|OPEN SESSION/.test(e.textContent))'));
  passed('Disconnected is explanatory without Create/Open/Reconnect/Recover');
  await load();
  await click('[aria-label="Go to DISPLAY E"]');
  await status('UNAVAILABLE');
  assert(!await evaluate('!!document.querySelector(".editorial-action")'));
  assert(await evaluate('document.querySelector(".display-identify").disabled'));
  passed('Unavailable has no executable primary action');
  await load('hide-blocked');
  assert(await evaluate('document.querySelector(".window-control--hide").disabled'));
  await click('[aria-label="Hide to tray"]');
  assert.equal(await evaluate('document.querySelector(".mock-feedback").textContent'), '');
  assert(await evaluate('document.querySelector("#tooltip-hide").textContent.includes("temporarily unavailable")'));
  passed('Hide ineligible is a semantic disabled control without feedback/action');

  await load();
  await evaluate('document.activeElement.blur()');
  const reached = [];
  for (let i = 0; i < 18; i++) {
    await key('Tab', 'Tab', 9);
    reached.push(await evaluate('document.activeElement.getAttribute("aria-label") || document.activeElement.textContent.trim()'));
  }
  for (const label of ['Settings', 'Refresh displays', 'Identify DISPLAY A', 'Previous display', 'Go to DISPLAY B', 'Next display', 'OPEN SESSION']) assert(reached.includes(label), 'Keyboard reaches ' + label);
  passed('Shell actions / name Identify / navigation / indicators / primary action keyboard reachable');
  for (const [width, height] of [[1600,900],[1000,760],[560,700],[440,680],[360,680]]) {
    await send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
    await rest();
    const layout = await evaluate('(() => { const r=e=>{const x=e.getBoundingClientRect();return {left:x.left,right:x.right,top:x.top,bottom:x.bottom,width:x.width,height:x.height}}; return {width:innerWidth,scroll:document.documentElement.scrollWidth,identity:r(document.querySelector(".application-identity")),settings:r(document.querySelector("[aria-label=Settings]")),refresh:r(document.querySelector(".launcher-refresh")),caption:r(document.querySelector(".window-controls")),labels:[...document.querySelectorAll(".title-bar button")].map(e=>e.getAttribute("aria-label"))} })()');
    assert(layout.scroll <= width + 1, 'No document overflow at ' + width);
    assert(layout.identity.right <= layout.settings.left && layout.settings.right <= layout.refresh.left && layout.refresh.right <= layout.caption.left, 'Left identity/settings/refresh, flexible gap, right captions at ' + width);
    assert(layout.caption.right <= width && layout.caption.bottom <= 64, 'Caption controls fit at ' + width);
    await click('[aria-label="Settings"]');
    assert(await evaluate('document.querySelector("dialog").getBoundingClientRect().width <= innerWidth-24'));
    await key('Escape', 'Escape', 27);
  }
  passed('Top shell relationship and lightweight dialog fit wide/normal/narrow/360px desktop');
  assert.equal(runtimeErrors.length, 0, 'No runtime exceptions');
  assert.equal(warnings.length, 0, 'No browser or hidden-focus warnings');
  await fs.writeFile(new URL('semantic-checks.json', evidence), JSON.stringify({ pass: true, checks, runtimeErrors, warnings, visualReview: 'Pending user UX trial; DOM/keyboard only' }, null, 2));
  console.log(JSON.stringify({ pass: true, checks: checks.length, runtimeErrors, warnings }));
} catch (error) {
  const name = process.env.LAUNCHER_RED === '1' ? 'semantic-red.json' : 'semantic-failed.json';
  await fs.writeFile(new URL(name, evidence), JSON.stringify({ pass: false, error: String(error), checks, runtimeErrors, warnings }, null, 2));
  throw error;
} finally { socket.close(); }
