import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// Dedicated QA browser (9227). Only this suite's own page is closed.
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
async function evaluate(expression) {
  const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
const q = selector => 'document.querySelector(' + JSON.stringify(selector) + ')';
async function rest() { await evaluate('new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))'); }
async function load(scenario = 'normal', query = '') {
  const url = 'http://127.0.0.1:5173/?view=session&sessionScenario=' + scenario + query;
  await send('Page.navigate', { url });
  let ready = false;
  for (let index = 0; index < 80; index++) {
    if (await evaluate('location.href===' + JSON.stringify(url) + '&&document.readyState==="complete"&&!!document.querySelector(".session-shell")')) { ready = true; break; }
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  assert(ready, 'Session route loaded'); await rest();
}
async function click(selector) { assert(await evaluate('!!' + q(selector)), selector + ' exists'); await evaluate(q(selector) + '.click()'); await rest(); }
async function text(selector, value) {
  await evaluate('(()=>{const e=' + q(selector) + ';Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,"value").set.call(e,' + JSON.stringify(value) + ');e.dispatchEvent(new Event("input",{bubbles:true}));})()'); await rest();
}
async function command(id, action, entity = 'playlist') { await click('[data-' + entity + '-id=' + JSON.stringify(id) + '] .entity-more'); await click('[data-command=' + JSON.stringify(action) + ']'); }
async function rename(id, value) { await command(id, 'rename'); await text('[data-rename-value]', value); await click('[data-dialog-confirm]'); }
async function language(value) {
  await evaluate('(()=>{for(const detail of [{type:"external-write",value:' + JSON.stringify(value) + '},{type:"invalidate"}])window.dispatchEvent(new CustomEvent("dse-mock-language",{detail}));})()');
  await rest(); await rest();
}
async function snapshot() { return evaluate('({shell:{...document.querySelector(".session-shell").dataset},inspector:{...document.querySelector(".session-inspector").dataset},preview:{...document.querySelector(".preview-phase-e").dataset},layout:{...document.querySelector(".session-workspace").dataset},runtime:document.querySelector(".playback-rail").textContent})'); }
async function check(finding, name, run) {
  try { await run(); checks.push({ finding, name, pass: true }); }
  catch (error) { checks.push({ finding, name, pass: false, error: error.message }); }
}
await send('Runtime.enable'); await send('Log.enable'); await send('Page.enable');
await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 1, mobile: false });

for (const id of ['playlist-a', 'playlist-b']) await check('F01', 'Duplicate aliases retain exact Configure target ' + id, async () => {
  await load(); await rename('playlist-a', 'Playlist'); await rename('playlist-b', 'Playlist');
  await command(id, 'background-music');
  assert.equal(await evaluate('document.querySelector("[data-configure-playlist-id]")?.dataset.configurePlaylistId ?? null'), id, 'Configure intent retains PlaylistId, not alias');
  assert.equal(await evaluate('!!' + q('.background-music-shell')), false, 'Configure remains entry/context only');
});
await check('F01', 'Rename changes presentation alias without changing Configure identity', async () => {
  await load(); await rename('playlist-b', 'Renamed'); await command('playlist-b', 'background-music');
  assert.equal(await evaluate('document.querySelector("[data-configure-playlist-id]")?.dataset.configurePlaylistId ?? null'), 'playlist-b');
  assert.match(await evaluate('document.querySelector("dialog").textContent'), /Renamed/);
});
await check('F01', 'Move preserves Configure identity', async () => {
  await load(); await command('playlist-b', 'move-up'); await command('playlist-b', 'background-music');
  assert.equal(await evaluate('document.querySelector("[data-configure-playlist-id]")?.dataset.configurePlaylistId ?? null'), 'playlist-b');
});
await check('F01', 'Context target differs from active Playlist and drives Configure', async () => {
  await load('selected-playlist-differs'); await command('playlist-b', 'background-music');
  assert.equal(await evaluate(q('.session-shell') + '.dataset.activePlaylistId'), 'playlist-a');
  assert.equal(await evaluate('document.querySelector("[data-configure-playlist-id]")?.dataset.configurePlaylistId ?? null'), 'playlist-b');
});

await check('F02', 'Committed EN to ZH rebuilds focused Inspector and restores authored Rate', async () => {
  await load(); await evaluate('window.gapRate=document.querySelector(".inspector-rate-entry input");window.gapRate.focus()');
  await text('.inspector-rate-entry input', '1.7'); await language('zh_CN');
  assert.equal(await evaluate(q('.inspector-rate-entry input') + '.value'), '1.0');
  assert.equal(await evaluate('window.gapRate.isConnected'), false, 'old editor is destroyed');
  assert.equal(await evaluate('document.activeElement===window.gapRate'), false);
});
await check('F02', 'Apply after retranslation cannot resurrect discarded Rate draft', async () => {
  await load(); await text('.inspector-rate-entry input', '1.7'); await language('zh_CN'); await click('.inspector-apply');
  assert.equal(await evaluate(q('.session-inspector') + '.dataset.authoredRate'), '1');
  assert.equal(await evaluate(q('.session-shell') + '.dataset.dirty'), 'false');
});
await check('F02', 'Language preserves authored/document/selection/runtime/Canvas/deployment/layout facts', async () => {
  await load('custom-layout-left-wide'); const before = await snapshot();
  await text('.inspector-rate-entry input', '1.7'); await language('zh_CN'); const after = await snapshot();
  for (const field of ['selectedPlaylistId', 'selectedItemId', 'activePlaylistId', 'activeItemId', 'revision', 'cleanRevision', 'dirty', 'projectPath']) assert.equal(after.shell[field], before.shell[field], field);
  assert.deepEqual(after.inspector, before.inspector); assert.deepEqual(after.preview, before.preview);
  for (const field of ['layoutLeft', 'layoutRight']) assert.equal(after.layout[field], before.layout[field]);
});
await check('F02', 'Unrelated rerender and uncommitted Language draft preserve local Rate draft', async () => {
  await load(); await text('.inspector-rate-entry input', '1.7');
  await evaluate('window.dispatchEvent(new CustomEvent("dse-mock-language",{detail:{type:"draft",value:"zh_CN"}}))'); await rest();
  await click('[aria-label="Preview View preferences"]');
  assert.equal(await evaluate(q('.inspector-rate-entry input') + '.value'), '1.7');
  assert.equal(await evaluate(q('.session-shell') + '.dataset.language'), 'en');
});
await check('F02', 'Same committed effective Language does not rebuild Inspector', async () => {
  await load(); await text('.inspector-rate-entry input', '1.7'); await language('en');
  assert.equal(await evaluate(q('.inspector-rate-entry input') + '.value'), '1.7');
});
await check('F02', 'Invalid local numeric draft is rebuilt from authored value on Language change', async () => {
  await load(); await text('.inspector-rate-entry input', ''); await language('zh_CN');
  assert.equal(await evaluate(q('.inspector-rate-entry input') + '.value'), '1.0');
});
await check('F02', 'Other Inspector local text draft is discarded without authoring on rebuild', async () => {
  await load(); await evaluate('window.gapName=document.querySelector("[data-inspector-name]");window.gapName.focus()');
  await text('[data-inspector-name]', 'Uncommitted name'); await language('zh_CN');
  assert.equal(await evaluate(q('[data-inspector-name]') + '.value'), 'Opening');
  assert.equal(await evaluate('window.gapName.isConnected'), false);
  assert.equal(await evaluate(q('.session-shell') + '.dataset.dirty'), 'false');
});

async function failBackground() { await load('background-save-failure'); await click('[aria-label="Preview View preferences"]'); await click('[aria-label="Reveal Wallpaper"]'); }
await check('F04', 'Persistence failure creates app modal and retains live/persisted/dirty values', async () => {
  await failBackground();
  assert.equal(await evaluate('document.querySelector("dialog[open] h2")?.textContent ?? null'), 'Desktop Background');
  assert.equal(await evaluate(q('.preview-phase-e') + '.dataset.background'), 'reveal');
  assert.equal(await evaluate(q('.preview-phase-e') + '.dataset.backgroundPersisted'), 'missing');
  assert.equal(await evaluate(q('.session-shell') + '.dataset.dirty'), 'false');
});
await check('F04', 'Pending Background warning blocks Hide at projection and actual request', async () => {
  await failBackground(); assert.equal(await evaluate(q('.window-control--hide') + '.disabled'), true);
  await click('.window-control--hide'); assert.equal(await evaluate(q('.window-control--hide') + '.dataset.hidden'), 'false');
});
await check('F04', 'View collapse or unrelated preference action cannot erase pending warning', async () => {
  await failBackground(); await click('[aria-label="Preview View preferences"]');
  if (await evaluate('!!' + q('[aria-label="Highlight Canvas Boundary"]'))) await click('[aria-label="Highlight Canvas Boundary"]');
  assert.equal(await evaluate('document.querySelector("dialog[open] h2")?.textContent ?? null'), 'Desktop Background');
});
await check('F04', 'Escape cannot silently dismiss mandatory Background acknowledgement', async () => {
  await failBackground();
  await send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
  await send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 }); await rest();
  assert.equal(await evaluate('document.querySelector("dialog[open] h2")?.textContent ?? null'), 'Desktop Background');
});
await check('F04', 'Acknowledgement restores Hide; same live mode does not retry failed persistence', async () => {
  await failBackground(); await click('[data-message-ok]'); assert(!await evaluate('!!document.querySelector("dialog[open]")'));
  assert.equal(await evaluate(q('.window-control--hide') + '.disabled'), false);
  await click('[aria-label="Reveal Wallpaper"]');
  assert.equal(await evaluate(q('.preview-phase-e') + '.dataset.backgroundWrites'), '1');
  assert.equal(await evaluate(q('.preview-phase-e') + '.dataset.background'), 'reveal');
});

await check('F05', 'Authoritative ClosePending disables Deploy in actual Preview surface', async () => {
  await load('hide-close-pending');
  assert.equal(await evaluate(q('.window-control--hide') + '.disabled'), true);
  assert.equal(await evaluate(q('.deployment-action') + '.disabled'), true, 'same Session ClosePending gates deployment');
});
await check('F05', 'Ready normal Session retains existing Deploy then Undeploy behavior', async () => {
  await load(); await click('.deployment-action');
  assert.equal(await evaluate(q('.preview-phase-e') + '.dataset.presentation'), 'deployed');
  await click('.deployment-action'); assert.equal(await evaluate(q('.preview-phase-e') + '.dataset.presentation'), 'preview-only');
});
for (const scenario of ['gap-close-pending-deployed', 'gap-ending-ready', 'gap-ending-deployed']) await check('F05', 'Actual surface gates lifecycle scenario ' + scenario, async () => {
  await load(scenario); assert.equal(await evaluate(q('.deployment-action') + '.disabled'), true);
  const before = await evaluate('({...document.querySelector(".preview-phase-e").dataset})');
  await evaluate('window.dispatchEvent(new CustomEvent("dse-mock-presentation",{detail:{scope:"display",action:{type:"toggle-deployment"}}}))'); await rest();
  const after = await evaluate('({...document.querySelector(".preview-phase-e").dataset})');
  assert.equal(after.lastDeploymentResult, 'rejected');
  for (const field of ['presentation', 'desired', 'retained', 'hostOwned', 'targetRegistered']) assert.equal(after[field], before[field]);
});
await check('F05', 'Same-turn stale rendered Deploy intent is rejected by fresh lifecycle authority', async () => {
  await load(); assert.equal(await evaluate(q('.deployment-action') + '.disabled'), false);
  await evaluate('window.dispatchEvent(new CustomEvent("dse-mock-session-role",{detail:{type:"close-begin"}}));window.dispatchEvent(new CustomEvent("dse-mock-presentation",{detail:{scope:"display",action:{type:"deploy"}}}))'); await rest();
  assert.equal(await evaluate(q('.session-shell') + '.dataset.closePending'), 'true');
  assert.equal(await evaluate(q('.deployment-action') + '.disabled'), true);
  assert.equal(await evaluate(q('.preview-phase-e') + '.dataset.lastDeploymentResult'), 'rejected');
  assert.equal(await evaluate(q('.preview-phase-e') + '.dataset.presentation'), 'preview-only');
});
for (const operation of ['add', 'remove']) await check('F03', 'Actual ' + operation + ' rollback-seek preserves authored edit and reports old runtime at start', async () => {
  await load(operation === 'add' ? 'gap-add-rollback-seek' : 'gap-remove-rollback-seek');
  const before = await evaluate('({...document.querySelector(".session-shell").dataset})');
  if (operation === 'add') { await click('[aria-label="Add Media"]'); await click('[data-dialog-confirm]'); }
  else { await command('a-scene', 'delete', 'item'); await click('[data-dialog-confirm]'); }
  assert.equal(await evaluate('document.querySelector("dialog[open] h2").textContent'), 'Unable to synchronize playback');
  assert.match(await evaluate(q('.session-message') + '.textContent'), operation === 'add' ? /Newly added media/ : /Playlist changes/);
  assert.equal(await evaluate(q('.session-timeline') + '.dataset.runtimeTime'), '0');
  assert.equal(await evaluate(q('.rail-scene') + '.textContent'), 'Item 1/3', 'old projection scene set remains');
  assert.equal(await evaluate(q('.playback-state') + '.textContent'), 'PAUSED');
  assert.equal(await evaluate(q('.playback-rail') + '.dataset.actualRate'), '1.7');
  assert.equal(await evaluate(q('.playback-rail') + '.dataset.outputMuted'), 'true');
  assert(Math.abs(Number(await evaluate(q('.playback-rail') + '.dataset.embeddedGain')) - .35) < .000001);
  const after = await evaluate('({...document.querySelector(".session-shell").dataset})');
  assert.equal(after.activePlaylistId, before.activePlaylistId); assert.equal(after.activeItemId, before.activeItemId); assert.equal(after.dirty, 'true');
  assert.equal(await evaluate('document.querySelectorAll("[data-item-id] .entity-select").length'), operation === 'add' ? 4 : 2);
  if (operation === 'remove') assert.equal(await evaluate('!!' + q('[data-item-id="a-scene"]')), false);
});
await check('F06', 'Browser renders exact combined crop/Scale/Position for active scene despite different selection', async () => {
  await load('gap-hover-geometry', '&timelineClock=manual');
  await evaluate('(()=>{const e=document.querySelector(".timeline-control"),r=e.getBoundingClientRect();e.dispatchEvent(new PointerEvent("pointermove",{bubbles:true,clientX:r.left+r.width*.4,clientY:r.top+10,buttons:0}));for(let i=0;i<5;i++)window.dispatchEvent(new CustomEvent("dse-timeline-dev",{detail:"advance75"}));})()'); await rest();
  assert.equal(await evaluate(q('.session-timeline') + '.dataset.hoverStatus'), 'valid');
  assert.equal(await evaluate(q('.session-timeline') + '.dataset.hoverItem'), 'a-opening');
  assert.equal(await evaluate(q('.session-shell') + '.dataset.selectedItemId'), 'b-image');
  const actual = await evaluate('(()=>{const r=document.querySelector("[data-hover-destination]"),v=document.querySelector(".timeline-thumbnail svg").viewBox.baseVal;return {x:+r.getAttribute("x")/v.width,y:+r.getAttribute("y")/v.height,width:+r.getAttribute("width")/v.width,height:+r.getAttribute("height")/v.height}})()');
  for (const [field, expected] of Object.entries({x:.35,y:.0875,width:.7,height:.525})) assert(Math.abs(actual[field]-expected)<.000001, field+' full normalized transform');
  assert.match(await evaluate(q('[data-hover-source-frame]') + '.dataset.sourceIdentity'), /a-opening/);
  assert.equal(await evaluate('document.querySelectorAll("[data-source-marker]").length'), 5);
  assert.equal(await evaluate('document.querySelector(".timeline-thumbnail svg").style.overflow'), 'hidden');
  const matrix = await evaluate(q('[data-hover-source-frame]') + '.getAttribute("transform")');
  await text('[aria-label="Uniform scale"]', '0.5');
  assert.equal(await evaluate(q('[data-hover-source-frame]') + '.getAttribute("transform")'), matrix, 'selected scene edit does not compose active thumbnail');
});

const routes = [];
for (const [view, selector] of [['session', '.session-shell'], ['launcher', '.launcher-shell'], ['background-music', '.background-music-shell']]) {
  await send('Page.navigate', { url: 'http://127.0.0.1:5173/?view=' + view });
  let ready = false;
  for (let index = 0; index < 80; index++) {
    if (await evaluate('document.readyState==="complete"&&!!' + q(selector) + '&&new URLSearchParams(location.search).get("view")===' + JSON.stringify(view))) { ready = true; break; }
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  await rest(); routes.push({ view, ready });
}
const result = { suite: 'MainWindow semantic gap focused browser checks', pass: checks.every(check => check.pass) && routes.every(route => route.ready) && !errors.length && !warnings.length, routes,
  groups: Object.fromEntries([...new Set(checks.map(check => check.finding))].map(id => { const group = checks.filter(check => check.finding === id); return [id, { passed: group.filter(check => check.pass).length, total: group.length }]; })), checks, runtimeErrors: errors, consoleWarnings: warnings };
const cache = new URL('../node_modules/.cache/semantic-gap-closure/', import.meta.url);
await fs.mkdir(cache, { recursive: true });
await fs.writeFile(new URL('ui-' + (process.argv.includes('--red') ? 'red' : result.pass ? 'green' : 'failed') + '.json', cache), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify(result, null, 2));
ws.close(); await fetch('http://127.0.0.1:9227/json/close/' + tab.id);
if (!result.pass) process.exitCode = 1;
