import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// Isolated headless QA page only. No screenshots, native dialogs or user profile.
const evidence = new URL('../node_modules/.cache/session-phase-a/', import.meta.url);
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
  const payload = JSON.parse(event.data);
  if (payload.method === 'Runtime.exceptionThrown') runtimeErrors.push(payload.params.exceptionDetails);
  if (payload.method === 'Log.entryAdded' && ['warning', 'error'].includes(payload.params.entry.level)) warnings.push(payload.params.entry);
  if (payload.method === 'Runtime.consoleAPICalled' && ['warning', 'error'].includes(payload.params.type)) warnings.push(payload.params);
  const request = pending.get(payload.id);
  if (request) { pending.delete(payload.id); payload.error ? request.reject(payload.error) : request.resolve(payload.result); }
});
function send(method, params = {}) {
  return new Promise((resolve, reject) => { const id = ++serial; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); });
}
async function evaluate(expression) {
  const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
async function rest() {
  await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
  await evaluate('Promise.allSettled(document.getAnimations().map(animation => animation.finished))');
}
async function load(scenario = 'normal') {
  await send('Page.navigate', { url: 'http://127.0.0.1:5173/?view=session&sessionScenario=' + scenario });
  for(let i=0;i<70;i++) {
    if(await evaluate('document.readyState === "complete" && !!document.querySelector(".session-shell")')) break;
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  await rest();
}
async function click(selector) {
  assert(await evaluate('!!document.querySelector(' + JSON.stringify(selector) + ')'), 'Control exists: ' + selector);
  await evaluate('(() => { const control=document.querySelector(' + JSON.stringify(selector) + '); control.focus(); control.click(); })()');
  await rest();
}
const passed = check => checks.push({ check, pass: true });
const q = selector => 'document.querySelector(' + JSON.stringify(selector) + ')';
const playlist = id => '.playlist-entity[data-playlist-id="' + id + '"]';
const item = id => '.item-entity[data-item-id="' + id + '"]';
async function identities() {
  return evaluate('(() => { const s=document.querySelector(".session-shell").dataset; return [s.selectedPlaylistId,s.selectedItemId,s.activePlaylistId,s.activeItemId]; })()');
}
async function snapshot() {
  return evaluate('(() => { const s=document.querySelector(".session-shell");return {data:{...s.dataset},preview:document.querySelector(".preview-source")?.textContent,time:document.querySelector(".playback-time").textContent,playing:document.querySelector(".playback-state").textContent,playlists:[...document.querySelectorAll(".playlist-entity .entity-alias")].map(e=>e.textContent),items:[...document.querySelectorAll(".item-entity .entity-alias")].map(e=>e.textContent)}; })()');
}
async function key(key, code, number, modifiers = 0) {
  await send('Input.dispatchKeyEvent', { type: 'keyDown', key, code, windowsVirtualKeyCode: number, modifiers, ...(key === 'Enter' ? {text:'\r'} : {}) });
  await send('Input.dispatchKeyEvent', { type: 'keyUp', key, code, windowsVirtualKeyCode: number, modifiers });
  await rest();
}
async function choose(selector, value) {
  await evaluate('(() => { const e=' + q(selector) + ';e.value=' + JSON.stringify(value) + ';e.dispatchEvent(new Event("change",{bubbles:true})); })()');
  await rest();
}
async function text(selector, value) {
  await evaluate('(() => { const e=' + q(selector) + ';Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,"value").set.call(e,' + JSON.stringify(value) + ');e.dispatchEvent(new Event("input",{bubbles:true})); })()');
  await rest();
}
async function projectCommand(command) { await click('.session-project-trigger'); await click('[data-command="' + command + '"]'); }
async function entityCommand(kind, id, command) {
  await click((kind === 'playlist' ? playlist(id) : item(id)) + ' .entity-more');
  await click('[data-command="' + command + '"]');
}
async function dismissMessage() { await click('[data-message-ok]'); }
async function rename(kind, id, value) {
  await entityCommand(kind, id, 'rename'); await text('[data-rename-value]', value); await click('[data-dialog-confirm]');
}
try {
  await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable'); await send('Log.clear');
  await send('Page.bringToFront'); await send('Emulation.setEmulatedMedia', { features: [] });
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await load();
  assert(await evaluate('document.querySelectorAll(".playlist-entity[data-playlist-id]").length >= 2'), 'Project contains independent Playlist entities');
  assert(await evaluate('!!document.querySelector(".session-project-trigger")'), 'Document commands have an integrated shell entry');
  passed('Project / Playlist / Item hierarchy and document command entry');
  const beforeSelection = await snapshot();
  await click(playlist('playlist-b') + ' .entity-select');
  assert.deepEqual(await identities(), ['playlist-b','','playlist-a','a-opening']);
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'), 'playlist');
  assert.equal(await evaluate('document.querySelectorAll(".item-entity").length'), 2);
  await click(item('b-image') + ' .entity-select');
  assert.deepEqual(await identities(), ['playlist-b','b-image','playlist-a','a-opening']);
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'), 'item');
  await click(playlist('playlist-b') + ' .entity-select');
  assert.equal((await identities())[1], 'b-image', 'Repeat current row click does not emit currentItemChanged');
  assert.equal((await snapshot()).data.revision, beforeSelection.data.revision);
  assert.equal((await snapshot()).time, beforeSelection.time);
  assert.equal((await snapshot()).preview, beforeSelection.preview);
  passed('Playlist/Item click selects only; current Playlist repeat click preserves selected Item; Inspector owner correct');
  await entityCommand('item', 'b-image', 'play');
  assert.deepEqual(await identities(), ['playlist-b','b-image','playlist-b','b-image']);
  assert.equal((await snapshot()).playing, 'PLAYING');
  assert.equal((await snapshot()).time, '00:00/00:10');
  await load();
  await evaluate(q(playlist('playlist-b') + ' .entity-select') + '.dispatchEvent(new MouseEvent("dblclick",{bubbles:true}))');
  await rest();
  assert.deepEqual(await identities(), ['playlist-b','b-opening','playlist-b','b-opening']);
  passed('Explicit Item Play and Playlist double click activate target, synchronize identities and start at zero');
  await load();
  await click(playlist('playlist-c') + ' .entity-more');
  assert(await evaluate(q('[data-command="play"]') + '.disabled'));
  await key('Escape','Escape',27);
  await evaluate(q(playlist('playlist-c') + ' .entity-select') + '.dispatchEvent(new MouseEvent("dblclick",{bubbles:true}))');
  await rest(); assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Empty Playlist');
  await dismissMessage();
  await evaluate(q(playlist('playlist-d') + ' .entity-select') + '.dispatchEvent(new MouseEvent("dblclick",{bubbles:true}))');
  await rest(); assert.equal((await identities())[3], 'a-opening');
  assert(!await evaluate('!!document.querySelector("dialog")'));
  await click(playlist('playlist-d') + ' .entity-select');
  await click(item('d-scene') + ' .entity-more');
  assert(await evaluate(q('[data-command="play"]') + '.disabled'));
  assert(await evaluate(q('[data-command="availability"]') + '.disabled'));
  await key('Escape','Escape',27);
  passed('Empty context Play disabled, double click info; unavailable double click inert; parent-disabled Item actions gated');

  await load();
  const startRevision = Number((await snapshot()).data.revision);
  await click('[aria-label="Add Playlist"]');
  assert.equal(await evaluate('document.querySelector(".playlist-entity[data-selected=true] .entity-alias").textContent'), 'Playlist E');
  const addedId = (await identities())[0];
  assert.deepEqual((await identities()).slice(1), ['', 'playlist-a','a-opening']);
  await click('[aria-label="Add Media"]'); await click('[data-dialog-confirm]');
  assert.equal(await evaluate('document.querySelectorAll(".item-entity").length'), 1);
  assert.equal((await identities())[1], '', 'Adding does not auto-select Item');
  assert.equal((await identities())[3], 'a-opening', 'Adding does not auto-activate');
  await click('[aria-label="Add Media"]'); await click('[data-dialog-confirm]');
  const ids = await evaluate('[...document.querySelectorAll(".item-entity")].map(e=>e.dataset.itemId)');
  assert.equal(ids.length, 2); assert.notEqual(ids[0], ids[1]);
  await rename('item', ids[0], '  Independent copy  ');
  assert.deepEqual(await evaluate('[...document.querySelectorAll(".item-entity .entity-alias")].map(e=>e.textContent)'), ['  Independent copy  ','Opening']);
  assert(await evaluate('document.querySelector(".identity-property--source").textContent.includes("C:/MockMedia/Opening.mp4")'));
  assert.equal(Number((await snapshot()).data.revision), startRevision + 4);
  assert.equal((await snapshot()).data.dirty, 'true');
  await rename('playlist', addedId, 'Playlist A');
  assert.equal(await evaluate('[...document.querySelectorAll(".playlist-entity .entity-alias")].filter(e=>e.textContent==="Playlist A").length'), 2);
  const renamedRevision = (await snapshot()).data.revision;
  await rename('playlist', addedId, 'Playlist A');
  assert.equal((await snapshot()).data.revision, renamedRevision, 'Exact unchanged rename does not dirty again');
  await rename('playlist', addedId, '   ');
  assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Invalid Name');
  assert.equal((await snapshot()).data.revision, renamedRevision); await dismissMessage();
  passed('Add/independent same-source Items; alias/source separation; whitespace preservation/duplicate aliases/noop/invalid rename; authored dirty');

  await load(); await rename('playlist','playlist-a','Playlist BKTXHSOGHKKG');
  const originalPlaylistRows = await evaluate('[...document.querySelectorAll(".playlist-entity")].map(e=>[e.dataset.playlistId,e.querySelector(".entity-alias").textContent])');
  await projectCommand('save');
  await projectCommand('open'); await choose('[data-picker-value]', 'C:/MockProjects/editorial.json'); await click('[data-dialog-confirm]');
  await click('[aria-label="Add Playlist"]');
  assert.equal(await evaluate('document.querySelector(".playlist-entity[data-selected=true] .entity-alias").textContent'), 'Playlist BKTXHSOGHKKH');
  const reopenedPlaylistRows = await evaluate('[...document.querySelectorAll(".playlist-entity")].map(e=>[e.dataset.playlistId,e.querySelector(".entity-alias").textContent])');
  assert.deepEqual(reopenedPlaylistRows.slice(0, -1), originalPlaylistRows);
  assert(!originalPlaylistRows.some(([id, alias]) => id === reopenedPlaylistRows.at(-1)[0] || alias === reopenedPlaylistRows.at(-1)[1]));
  passed('UI Rename → Save → Open → Add retains native uint64 default-name precision');

  await load('active-playlist-disable');
  await entityCommand('playlist','playlist-a','availability');
  assert.deepEqual(await identities(), ['playlist-a','a-opening','','']);
  assert.equal((await snapshot()).playing, 'NO MEDIA');
  assert.equal(await evaluate(q(playlist('playlist-a')) + '.dataset.enabled'), 'false');
  await entityCommand('playlist','playlist-a','availability');
  assert.equal((await identities())[3], '', 'Enable never restarts');
  await load('active-item-disable');
  await entityCommand('item','a-opening','availability');
  assert.deepEqual(await identities(), ['playlist-a','a-opening','','']);
  assert.equal(await evaluate(q(item('a-opening')) + '.dataset.enabled'), 'false');
  passed('Active Playlist/Item Disable stops without successor; selection retained; Enable does not restart');

  await load('delete-active-item');
  await entityCommand('item','a-opening','delete');
  assert(await evaluate('document.activeElement.hasAttribute("data-dialog-cancel")'), 'Delete defaults Cancel');
  await key('Enter','Enter',13);
  assert.equal((await identities())[3], 'a-opening');
  await entityCommand('item','a-opening','delete'); await click('[data-dialog-confirm]');
  assert.deepEqual(await identities(), ['playlist-a','','playlist-a','a-scene']);
  assert.equal((await snapshot()).time, '00:00/01:47');
  await entityCommand('item','a-scene','delete'); await click('[data-dialog-confirm]');
  assert.equal((await identities())[3], '', 'Disabled remaining Item is not a successor');
  await load('successor-failure');
  await entityCommand('item','a-opening','delete'); await click('[data-dialog-confirm]');
  assert.equal((await identities())[3], '');
  assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Unable to synchronize playback');
  await dismissMessage();
  passed('Item delete default Cancel, eligible successor at start, no-successor stop, failed successor stops with warning');
  await load('delete-active-playlist');
  await entityCommand('playlist','playlist-a','delete'); await click('[data-dialog-confirm]');
  assert.deepEqual(await identities(), ['playlist-b','','','']);
  await entityCommand('playlist','playlist-d','delete'); await click('[data-dialog-confirm]');
  assert.equal((await identities())[0], 'playlist-c', 'Deleting last selected Playlist selects previous');
  await entityCommand('playlist','playlist-c','delete'); await click('[data-dialog-confirm]');
  await entityCommand('playlist','playlist-b','delete'); await click('[data-dialog-confirm]');
  assert.deepEqual(await identities(), ['','','','']);
  assert.equal(await evaluate('document.querySelectorAll(".playlist-entity").length'), 0);
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'), 'none');
  assert(await evaluate('document.querySelector(".library-empty").textContent.includes("No playlists")'));
  await click('[aria-label="Add Playlist"]');
  assert.equal(await evaluate('document.querySelector(".playlist-entity .entity-alias").textContent'), 'Playlist E', 'Delete does not recycle suffix');
  passed('Active Playlist delete stops, next/previous replacement, final deletion legal empty, Add remains accessible');

  await load('empty-project');
  assert.equal(await evaluate('document.querySelector(".session-project-name").textContent'), 'Untitled');
  assert.deepEqual(await identities(), ['','','','']);
  assert.equal((await snapshot()).data.dirty, 'false');
  await click('[aria-label="Add Media"]'); await click('[data-dialog-confirm]');
  assert.deepEqual(await identities(), ['','','','']);
  assert.equal((await snapshot()).data.dirty, 'false', 'Valid media without selected Playlist does not invent a Playlist');
  passed('Real startup/New-style empty Project, No Selection, Add Media cannot invent parent');
  await load('path-backed-clean');
  assert.equal(await evaluate('document.querySelector(".session-project-name").textContent'), 'editorial.json');
  assert.deepEqual(await identities(), ['','','','']);
  const cleanBefore = await snapshot();
  await projectCommand('save');
  assert.deepEqual(await snapshot(), cleanBefore, 'Clean Save preserves runtime/selection/revision');
  passed('Path-backed clean identity and Save preserves authored/runtime state');

  await load('dirty-untitled');
  const dirtyBefore = await snapshot();
  await projectCommand('new');
  assert(await evaluate('document.activeElement.hasAttribute("data-decision-save")'), 'Dirty decision defaults Save');
  await click('[data-decision-cancel]'); assert.deepEqual(await snapshot(), dirtyBefore);
  await projectCommand('new'); await key('Escape','Escape',27); assert.deepEqual(await snapshot(), dirtyBefore);
  await projectCommand('new'); await click('[data-decision-save]');
  assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Save Project As');
  await click('[data-dialog-cancel]'); assert.deepEqual(await snapshot(), dirtyBefore);
  await projectCommand('new'); await click('[data-decision-discard]');
  assert.deepEqual(await identities(), ['','','','']);
  assert.equal(await evaluate('document.querySelectorAll(".playlist-entity").length'), 0);
  assert.equal((await snapshot()).data.dirty, 'false');
  assert.equal((await snapshot()).data.projectPath, '');
  assert.equal(Number((await snapshot()).data.revision), Number(dirtyBefore.data.revision) + 1);
  passed('Dirty New Cancel/Escape/SaveAs Cancel abort; Discard creates empty clean Untitled');
  await load('dirty-untitled');
  await projectCommand('save');
  await choose('[data-picker-value]', 'C:/MockProjects/editorial-copy.json'); await click('[data-dialog-confirm]');
  assert.equal((await snapshot()).data.projectPath, 'C:/MockProjects/editorial-copy.json');
  assert.equal((await snapshot()).data.dirty, 'false');
  assert.deepEqual(await identities(), ['playlist-a','a-opening','playlist-a','a-opening']);
  await load(); await rename('playlist','playlist-a','New alias');
  const preSaveAs = await snapshot();
  await projectCommand('save-as'); await click('[data-dialog-cancel]'); assert.deepEqual(await snapshot(), preSaveAs);
  await projectCommand('save-as'); await choose('[data-picker-value]', 'C:/MockProjects/other-display.json'); await click('[data-dialog-confirm]');
  assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Save Project Failed');
  assert.deepEqual(await snapshot(), preSaveAs); await dismissMessage();
  await projectCommand('save-as'); await choose('[data-picker-value]', 'C:/MockProjects/editorial-copy.json'); await click('[data-dialog-confirm]');
  assert.equal((await snapshot()).data.dirty, 'false');
  assert.equal((await snapshot()).data.revision, preSaveAs.data.revision);
  assert.deepEqual(await identities(), ['playlist-a','a-opening','playlist-a','a-opening']);
  passed('Untitled Save follows SaveAs; SaveAs cancel/fail atomic; success adopts identity/clean without stopping');

  for (const command of ['new','open','close']) {
    await load('save-failure'); const previous = await snapshot();
    await projectCommand(command);
    if (command === 'open') await click('[data-dialog-confirm]');
    await click('[data-decision-save]');
    assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Save Project Failed');
    assert.deepEqual(await snapshot(), previous, 'Failed Save blocks ' + command); await dismissMessage();
  }
  passed('Save failure blocks New/Open/Close destructive continuation and preserves current state');
  for (const scenario of ['open-failure','wrong-owner']) {
    await load(scenario); await rename('playlist','playlist-a','Dirty current'); const previous = await snapshot();
    await projectCommand('open'); await click('[data-dialog-confirm]');
    assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Open Project Failed');
    assert(!await evaluate('!!document.querySelector("[data-decision-save]")'), 'Candidate validation happens before dirty decision');
    assert.deepEqual(await snapshot(), previous); await dismissMessage();
  }
  passed('Invalid candidate/wrong owner fails before dirty decision; no early replacement');
  await load('dirty-untitled'); const beforeOpen = await snapshot();
  await projectCommand('open'); await click('[data-dialog-cancel]'); assert.deepEqual(await snapshot(), beforeOpen);
  await projectCommand('open'); await click('[data-dialog-confirm]'); await click('[data-decision-cancel]'); assert.deepEqual(await snapshot(), beforeOpen);
  await projectCommand('open'); await click('[data-dialog-confirm]'); await click('[data-decision-discard]');
  assert.equal((await snapshot()).data.projectPath, 'C:/MockProjects/studio.json');
  assert.equal((await snapshot()).data.dirty, 'false'); assert.deepEqual(await identities(), ['','','','']);
  assert.deepEqual((await snapshot()).playlists, ['Studio']);
  passed('Open picker/dirty Cancel preserve; valid Discard commits only candidate, clean and four IDs empty');
  await load(); await rename('playlist','playlist-a','Saved current edit');
  await projectCommand('open'); await choose('[data-picker-value]', 'C:/MockProjects/editorial.json'); await click('[data-dialog-confirm]');
  await click('[data-decision-save]');
  assert.equal((await snapshot()).data.dirty, 'false'); assert.deepEqual(await identities(), ['','','','']);
  assert.equal((await snapshot()).playlists[0], 'Saved current edit', 'Open after same-path Save reloads fresh snapshot');
  passed('UI same-path Save → Open reload does not publish stale candidate');
  for (const scenario of ['open-reload-failure','open-reload-owner']) {
    await load(scenario); await projectCommand('open'); await click('[data-dialog-confirm]'); await click('[data-decision-save]');
    assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Open Project Failed');
    assert.equal((await snapshot()).data.projectPath, 'C:/MockProjects/editorial.json');
    assert.equal((await snapshot()).data.dirty, 'false', 'Legal prior Save effects remain');
    assert.deepEqual(await identities(), ['playlist-a','a-opening','playlist-a','a-opening']);
    await dismissMessage();
  }
  passed('Reload error/owner change aborts candidate swap while preserving already-successful Save');

  for (const entry of ['caption','project']) {
    await load('dirty-untitled'); const previous = await snapshot();
    if (entry === 'caption') await click('[aria-label="Close"]'); else await projectCommand('close');
    await click('[data-decision-cancel]'); assert.deepEqual(await snapshot(), previous);
    if (entry === 'caption') await click('[aria-label="Close"]'); else await projectCommand('close');
    await click('[data-decision-save]'); await click('[data-dialog-cancel]'); assert.deepEqual(await snapshot(), previous);
    if (entry === 'caption') await click('[aria-label="Close"]'); else await projectCommand('close');
    await click('[data-decision-discard]');
    assert.equal((await snapshot()).data.sessionClosed, 'true');
    assert(await evaluate('document.querySelector(".session-closed").textContent.includes("Session closed.")'));
  }
  passed('Caption Close and Project Close share dirty decision; Cancel/SaveAs Cancel preserve; Discard returns mock closed result');
  await load('dirty-untitled'); await click('[aria-label="Close"]'); await click('[data-decision-save]'); await click('[data-dialog-confirm]');
  assert.equal((await snapshot()).data.sessionClosed, 'true'); assert.equal((await snapshot()).data.dirty, 'false');
  await load('path-backed-clean'); await click('[aria-label="Close"]');
  assert.equal((await snapshot()).data.sessionClosed, 'true'); assert(!await evaluate('!!document.querySelector("dialog")'));
  passed('Dirty Close Save completes only after successful save; clean Close needs no decision');

  await load();
  for (const property of ['Rotation','Opacity','Visible','Loop','Volume','Scale','Position X','Position Y']) {
    assert(!await evaluate('!!document.querySelector(' + JSON.stringify('[aria-label="' + property + '"]') + ')'), 'Out-of-scope/misleading property removed: ' + property);
  }
  await entityCommand('playlist','playlist-a','background-music');
  assert(await evaluate('document.querySelector(".session-message").textContent.includes("Playlist A")'));
  assert.equal(await evaluate('document.querySelector("[data-configure-playlist-id]").dataset.configurePlaylistId'), 'playlist-a');
  assert(!await evaluate('!!document.querySelector(".bgm-shell")')); await dismissMessage();
  const beforeHide = await snapshot(); await click('[aria-label="Hide to tray"]');
  assert.deepEqual(await identities(), ['playlist-a','a-opening','playlist-a','a-opening']);
  assert.equal((await snapshot()).data.revision, beforeHide.data.revision);
  assert.equal((await snapshot()).data.sessionClosed, 'false');
  passed('Limited identity Inspector; misleading properties absent; scoped BGM entry only; Hide remains separate from Close/Stop');
  await load('path-backed-clean'); await evaluate('document.activeElement.blur()');
  await key('n','KeyN',78,2);
  assert.equal((await snapshot()).data.projectPath, '');
  await key('o','KeyO',79,2); assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Open Project'); await key('Escape','Escape',27);
  await key('s','KeyS',83,2); assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Save Project As'); await key('Escape','Escape',27);
  await key('S','KeyS',83,10); assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Save Project As'); await key('Escape','Escape',27);
  passed('Four document keyboard shortcuts use same contracts and Escape safely cancels');
  for (const [view, selector] of [['session', '.session-shell'], ['launcher', '.launcher-shell'], ['background-music', '.background-music-shell']]) {
    await send('Page.navigate', { url: 'http://127.0.0.1:5173/?view=' + view });
    for (let i = 0; i < 70; i++) {
      if (await evaluate('document.readyState === "complete" && !!' + q(selector))) break;
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    assert(await evaluate('!!' + q(selector)), 'Runtime route mounts: ' + view);
    await rest();
  }
  assert.equal(runtimeErrors.length, 0); assert.equal(warnings.length, 0);
  passed('Session/Launcher/Background Music routes mount without runtime errors or browser/console warnings');
  await fs.writeFile(new URL('semantic-checks.json', evidence), JSON.stringify({ pass: true, checks, runtimeErrors, warnings }, null, 2));
  console.log(JSON.stringify({ pass: true, checks: checks.length, runtimeErrors, warnings }));
} catch (error) {
  await fs.writeFile(new URL(process.env.SESSION_PHASE_A_RED === '1' ? 'semantic-red.json' : 'semantic-failed.json', evidence), JSON.stringify({ pass: false, error: String(error), checks, runtimeErrors, warnings }, null, 2));
  throw error;
} finally { socket.close(); }
