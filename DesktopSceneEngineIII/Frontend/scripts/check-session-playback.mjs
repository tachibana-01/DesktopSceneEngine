import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// Isolated headless QA page only. No screenshots, native dialogs or user profile.
const evidence = new URL('../node_modules/.cache/session-phase-c/', import.meta.url);
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
  if (await evaluate(q(selector) + '.hasAttribute("data-playback-mode-trigger")')) {
    await click(selector); await click('[data-playback-mode-popup] [data-choice-value=' + JSON.stringify(value) + ']');
  } else if (await evaluate(q(selector) + '.getAttribute("role") === "combobox"')) {
    await click(selector); await click('[data-choice-value=' + JSON.stringify(value) + ']');
  } else await evaluate('(() => { const e=' + q(selector) + ';e.value=' + JSON.stringify(value) + ';e.dispatchEvent(new Event("change",{bubbles:true})); })()');
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
const control = label => '[aria-label=' + JSON.stringify(label) + ']';
const value = label => evaluate(q(control(label)) + '.value');
const rail = () => evaluate('({...document.querySelector(".playback-rail").dataset})');
const rev = async () => Number((await snapshot()).data.revision);
const visible = selector => evaluate('!!'+q(selector)+'?.getBoundingClientRect().width');
const closeTo = (actual,expected) => assert(Math.abs(Number(actual)-expected)<1e-6,actual+' vs '+expected);
async function numeric(label,number) {await evaluate(q(control(label))+'.focus()');await text(control(label),String(number));}
const masterTrigger = id => playlist(id)+' .playlist-volume-trigger';
const masterSlider = id => '.playlist-volume-popover[data-playlist-id="'+id+'"] input[data-playlist-master-slider]';
const masterPercent = id => evaluate(q(masterTrigger(id))+'.dataset.masterPercent');
async function rowMaster(id,number) {
  // UI-PIC-001: focus opens the ID-scoped portal; speaker click means mute/restore.
  await evaluate(q(masterTrigger(id))+'.focus()');await rest();
  assert(await evaluate('!!'+q(masterSlider(id))),'Master popover exists for '+id);
  await text(masterSlider(id),String(number));
}
async function fact(name) {await click('[aria-label="Dev Session scenarios"]');await click('[data-runtime-fact='+JSON.stringify(name)+']');}
async function selectPlaylist(id) {await click(playlist(id)+' .entity-select');}
async function selectItem(id) {await click(item(id)+' .entity-select');}
try {
  await send('Page.enable');await send('Runtime.enable');await send('Log.enable');await send('Log.clear');await send('Runtime.discardConsoleEntries');runtimeErrors.length=warnings.length=0;
  await send('Page.bringToFront');await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await send('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'no-preference'}]});
  await load();
  assert(await visible(control('Project sequence policy')),'Project-level sequence editor exists');
  assert.equal(await value('Project sequence policy'),'repeat-current');assert.equal(await value('Playlist Item sequence policy'),'list-loop');
  for(const label of ['Project sequence policy','Playlist Item sequence policy']) {
    await click(control(label));
    assert.deepEqual(await evaluate('[...document.querySelectorAll("[data-playback-mode-popup] .playback-mode-label")].map(e=>e.textContent)'),['List Loop','Random','Repeat Current']);
    await key('Escape','Escape',27);
  }
  const beforePolicy=await snapshot();
  await choose(control('Project sequence policy'),'list-loop');
  assert.deepEqual(await identities(),['playlist-a','a-opening','playlist-a','a-opening']);
  assert.equal((await snapshot()).time,beforePolicy.time);assert.equal((await snapshot()).playing,beforePolicy.playing);assert.equal(await rev(),Number(beforePolicy.data.revision)+1);
  const modeRev=await rev();await choose(control('Project sequence policy'),'list-loop');assert.equal(await rev(),modeRev);
  await choose(control('Playlist Item sequence policy'),'random');await selectPlaylist('playlist-b');
  assert.equal(await value('Playlist Item sequence policy'),'list-loop');
  await choose(control('Playlist Item sequence policy'),'repeat-current');await selectPlaylist('playlist-a');assert.equal(await value('Playlist Item sequence policy'),'random');
  assert.equal((await identities())[3],'a-opening');
  passed('Two modes, production defaults, independent selected Playlist ownership, authored dirty without restart/selection changes');
  await load('playback-no-selection');assert(await evaluate(q(control('Playlist Item sequence policy'))+'.disabled'));assert.equal(await value('Playlist Item sequence policy'),'');
  assert(!await evaluate(q(control('Project sequence policy'))+'.disabled'));
  passed('No selected Playlist disables only Item mode; active runtime does not supply editor policy');

  await load('playback-list-loop');const edited=await identities();const navigationRev=await rev();
  assert.equal(await evaluate('document.querySelector(".rail-scene").textContent'),'Item 2/3','Bound projection includes disabled authored scene');
  await click(control('Next'));assert.deepEqual(await identities(),[edited[0],edited[1],'playlist-b','b-opening']);
  assert(await evaluate('document.querySelector(".session-active-context").textContent.includes("Alternate Opening")'));
  assert.equal((await snapshot()).time,'00:00/00:23');
  await click(control('Previous'));assert.deepEqual(await identities(),[edited[0],edited[1],'playlist-a','a-scene']);
  await click(control('Next'));assert.deepEqual(await identities(),[edited[0],edited[1],'playlist-b','b-opening']);
  assert.equal(await rev(),navigationRev);
  passed('Next/Previous use active invocation, cross Playlists, preserve B/B3 selection, update active status and never dirty');
  await load('playback-paused');assert.equal((await snapshot()).playing,'PAUSED');const pausedTime=(await snapshot()).time;
  await click(control('Play'));assert.equal((await snapshot()).playing,'PLAYING');assert.equal((await snapshot()).time,pausedTime);
  await click(control('Pause'));await click(control('Next'));assert.equal((await snapshot()).playing,'PLAYING');assert.equal((await snapshot()).time,'00:00/01:47');
  await click(control('Pause'));await click(control('Previous'));assert.equal((await snapshot()).playing,'PLAYING');assert.equal((await identities())[3],'a-opening');
  passed('Play/Pause toggles configured state; paused Next/Previous start their target Playing at zero');
  await load();const anchor=await snapshot();await click(control('Previous'));assert.deepEqual(await snapshot(),anchor,'Fresh manual anchor Previous does not wrap');
  await click(control('Next'));await entityCommand('item','a-scene','play');const rebase=await snapshot();
  await click(control('Previous'));assert.deepEqual(await snapshot(),rebase);
  passed('Fresh and explicit Library Play anchors reset bounded Previous history in the shared runtime');

  await load('playback-repeat-project');await click(control('Next'));assert.equal((await identities())[3],'a-opening');
  await load('playback-repeat-item');for(let i=0;i<4;i++)await click(control('Next'));assert.equal((await identities())[3],'a-opening');assert.equal((await identities())[2],'playlist-a');
  await load('playback-end-project');await click(control('Next'));assert.equal((await identities())[3],'a-opening');
  await load('playback-disabled');await click(control('Next'));assert.equal((await identities())[3],'b-opening');
  await load('playback-single-item');await click(control('Next'));assert.equal((await identities())[3],'a-opening');
  passed('Project Repeat restarts Playlist, Item Repeat prevents outer progress, List Loop wraps filtered Project, single playable Item valid');
  const draws=[];
  for(let run=0;run<2;run++) {await load('playback-random');const trace=[];for(let i=0;i<3;i++){await click(control('Next'));trace.push((await identities())[3]);}draws.push(trace);}
  assert.deepEqual(draws[0],['a-scene','a-scene','b-opening']);assert.deepEqual(draws[1],draws[0]);
  passed('Random fixture uses reproducible replacement rounds with repeated targets, rather than fixed Next/shuffle');

  await load('playback-natural');const completeSelection=(await identities()).slice(0,2);const completeRev=await rev();
  await fact('complete');assert.deepEqual((await identities()).slice(0,2),completeSelection);assert.equal((await identities())[3],'b-opening');assert.equal(await rev(),completeRev);
  await load('playback-rate');assert.equal(await evaluate(q(control('Actual Rate'))+'.textContent'),'1.25×');
  assert.equal(await evaluate('document.querySelector(".rail-rate").textContent'),'1.3×');closeTo((await rail()).actualRate,1.25);
  await fact('advance');assert.equal((await snapshot()).time,'00:14/00:23');
  await click(control('Pause'));const timeBeforePause=(await snapshot()).time;await fact('advance');assert.equal((await snapshot()).time,timeBeforePause);
  await load('playback-long-time');assert.equal((await snapshot()).time,'01:01:01/02:00:00');
  passed('Completion/time facts use shared sequence and actual rate; pause freezes, time floors and supports hours without seek');

  await load('playback-no-target');const safeSelected=(await identities()).slice(0,2);await click(control('Next'));
  assert.deepEqual(await identities(),[...safeSelected,'','']);assert.equal((await rail()).runtimeConfigured,'false');
  assert.equal(await evaluate('document.querySelector(".session-active-context").textContent'),'No media loaded');
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'),'item');
  assert(!await visible('.rail-audio'));
  await load('playback-empty');const empty=await snapshot();
  for(const label of ['Previous','Play','Next']) {assert(!await evaluate(q(control(label))+'.disabled'));await click(control(label));assert.deepEqual(await snapshot(),empty);}
  assert(!await visible(control('Mute')));assert.equal((await snapshot()).time,'00:00/00:00');
  passed('NoTarget safe stop keeps selection; empty transport stays enabled/inert, No media loaded and hidden whole audio cluster');

  await load('playback-switch-failure');const failedSame=await snapshot();await click(control('Next'));assert.deepEqual(await snapshot(),failedSame);assert(!await evaluate('!!document.querySelector("dialog")'));
  await load('playback-natural-failure');await fact('complete');assert.equal((await rail()).halted,'true');assert.equal((await snapshot()).playing,'PAUSED');
  const halted=await snapshot();await click(control('Play'));assert.deepEqual(await snapshot(),halted);assert(!await evaluate('!!document.querySelector("dialog")'));
  await load('playback-cross-failure');await click(control('Next'));assert.equal((await identities())[3],'a-scene');assert.equal((await snapshot()).time,'00:00/01:47');
  assert.equal(await evaluate('document.querySelector("dialog h2").textContent'),'Unable to play media');await dismissMessage();
  await load('playback-prepare-failure');const prepareTime=(await snapshot()).time;await click(control('Next'));assert.equal((await snapshot()).time,prepareTime);
  assert(await evaluate('document.querySelector(".session-message").textContent.includes("Unable to create a playback sequence")'));await dismissMessage();
  await load('playback-cross-rollback');await click(control('Next'));assert.equal((await rail()).runtimeConfigured,'false');assert.equal((await snapshot()).time,'00:00/00:00');
  assert.equal(await evaluate('document.querySelector(".session-active-context").textContent'),'No media loaded');
  assert.equal(await evaluate('document.querySelector(".preview-source").textContent'),'No media loaded');assert(!await visible('.rail-audio'));await dismissMessage();
  passed('No failed-target skipping: same failure silent, natural halt blocks Play, cross rollback/prepare warnings exact, missing projection never shows stale current');

  // UI-PIC-001 intentionally replaces Rail active-Master access with explicit row IDs.
  await load('playback-masters');const audioIds=await identities();
  assert(!await evaluate('!!'+q(control('Active Playlist Master')+',.playback-rail input[type=range]')),'Rail Master is removed from the DOM');
  const masterRev=await rev();await rowMaster('playlist-a',70);
  assert.equal(await masterPercent('playlist-a'),'70');assert.equal(await masterPercent('playlist-b'),'100');
  assert.equal(await value('Playlist Master Volume'),'100');closeTo((await rail()).embeddedGain,.7);assert.equal(await rev(),masterRev+1);
  await numeric('Playlist Master Volume',20);assert.equal(await masterPercent('playlist-b'),'20');assert.equal(await masterPercent('playlist-a'),'70');closeTo((await rail()).embeddedGain,.7);
  assert.deepEqual(await identities(),audioIds);
  await selectPlaylist('playlist-a');assert.equal(await value('Playlist Master Volume'),'70');await numeric('Playlist Master Volume',75);assert.equal(await masterPercent('playlist-a'),'75');
  const selectedAudioIds=await identities();await rowMaster('playlist-a',70);assert.equal(await value('Playlist Master Volume'),'70');assert.deepEqual(await identities(),selectedAudioIds);
  await selectItem('a-opening');await numeric('Volume',50);closeTo((await rail()).embeddedGain,.35);
  await selectItem('a-scene');await numeric('Volume',25);closeTo((await rail()).embeddedGain,.35);
  await selectItem('a-opening');assert.equal(await value('Volume'),'50');
  const muteRev=await rev();await click(control('Mute'));assert.equal((await rail()).outputMuted,'true');closeTo((await rail()).embeddedGain,.35);closeTo((await rail()).effectiveOutput,0);
  assert.equal(await masterPercent('playlist-a'),'70');assert.equal(await value('Volume'),'50');assert.equal(await rev(),muteRev);
  await click(control('Unmute'));closeTo((await rail()).effectiveOutput,.35);assert.equal(await rev(),muteRev);
  passed('Explicit Playlist row Master/Inspector selected Master share authored property by ID; Rail Master absent; active Gain scopes output; Output Mute preserves70/50 and dirty');
  await click(control('Mute'));await projectCommand('new');await click('[data-decision-discard]');assert.equal((await rail()).outputMuted,'true');assert(!await visible('.rail-audio'));
  await click('[aria-label="Add Playlist"]');await click('[aria-label="Add Media"]');await click('[data-dialog-confirm]');
  const freshPlaylist=(await identities())[0];await entityCommand('playlist',freshPlaylist,'play');assert.equal((await rail()).outputMuted,'true');closeTo((await rail()).effectiveOutput,0);
  await click(control('Unmute'));closeTo((await rail()).effectiveOutput,1);
  passed('Mute survives same-Session New/stop/fresh Library Play and is excluded from authored document values');

  await load('playback-long-alias');
  for(const [width,height] of [[1800,1000],[1280,800],[1000,760],[680,700],[440,680],[1280,560]]) {
    await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});await rest();
    const geometry=await evaluate('(()=>{const r=document.querySelector(".playback-rail").getBoundingClientRect();return {page:document.documentElement.scrollWidth,viewport:innerWidth,height:r.height,controls:[...document.querySelectorAll(".playback-rail button,.rail-audio input")].filter(e=>e.getBoundingClientRect().width).map(e=>{const a=e.getBoundingClientRect();return {left:a.left,right:a.right};}),title:document.querySelector(".session-active-context").title,text:document.querySelector(".session-active-context").textContent};})()');
    assert(geometry.page<=geometry.viewport+1);assert.equal(geometry.height,64);assert(geometry.controls.every(r=>r.left>=0&&r.right<=width+1));assert.equal(geometry.title,geometry.text);
  }
  passed('Long aliases elide with complete titles; Rail/transport/audio remain within all six structural breakpoints');
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});await load();
  await evaluate(q(control('Project sequence policy'))+'.focus()');await key(' ','Space',32);await key('Home','Home',36);await key('Enter','Enter',13);
  assert.equal(await value('Project sequence policy'),'list-loop');assert.equal(await evaluate('document.activeElement.getAttribute("aria-label")'),'Project sequence policy');
  await evaluate(q(masterTrigger('playlist-a'))+'.focus()');await rest();await evaluate(q(masterSlider('playlist-a'))+'.focus()');
  await key('ArrowDown','ArrowDown',40);assert.equal(await masterPercent('playlist-a'),'99');
  await evaluate(q(control('Mute'))+'.focus()');await key(' ','Space',32);assert.equal(await evaluate('document.activeElement.getAttribute("aria-label")'),'Unmute');
  await key(' ','Space',32);assert.equal(await evaluate('document.activeElement.getAttribute("aria-label")'),'Mute');
  assert(await evaluate('getComputedStyle(document.activeElement).outlineStyle!=="none"'));
  assert(!await evaluate('!!'+q('.playback-rail input[type=range],'+control('Active Playlist Master'))));
  assert(!await evaluate('!!document.querySelector("[aria-label=Loop],[aria-label=Seek],[aria-label=Timeline]")'));
  passed('Policy and row Master keyboard semantics work; Rail Output Mute retains focus through keyboard toggle; Rail Master removed; no boolean Loop or Timeline/seek false coverage');
  assert.equal(runtimeErrors.length,0);assert.equal(warnings.length,0);
  await fs.writeFile(new URL('playback-ui-green.json',evidence),JSON.stringify({pass:true,checks,runtimeErrors,warnings},null,2));
  console.log(JSON.stringify({pass:true,checks:checks.length,runtimeErrors,warnings}));
} catch(error) {await fs.writeFile(new URL(process.env.PLAYBACK_RED==='1'?'playback-ui-red.json':'playback-ui-failed.json',evidence),JSON.stringify({pass:false,error:String(error),checks,runtimeErrors,warnings},null,2));throw error;} finally {socket.close();}
