import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// Isolated headless QA page only. No screenshots, native dialogs or user profile.
const evidence = new URL('../node_modules/.cache/session-phase-b/', import.meta.url);
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
  if (await evaluate(q(selector) + '.getAttribute("role") === "combobox"')) {
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
const owner = () => evaluate('({...document.querySelector(".session-inspector").dataset})');
const revision = async () => Number((await snapshot()).data.revision);
const visible = selector => evaluate('!!' + q(selector) + '?.getBoundingClientRect().width');
async function focus(selector) { await evaluate(q(selector) + '.focus()'); }
async function numeric(label, number) { await focus(control(label)); await text(control(label), String(number)); }
async function blur() { await evaluate('document.activeElement.blur()'); await rest(); }
function closeTo(actual, expected) { assert(Math.abs(Number(actual) - expected) < 1e-6, actual + ' vs ' + expected); }
try {
  await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable'); await send('Log.clear'); await send('Page.bringToFront');
  await send('Runtime.discardConsoleEntries');
  // CDP enable replays history from the page left by the previous suite/HMR.
  // Start collection before this suite's first navigation; keep every new entry.
  runtimeErrors.length = 0; warnings.length = 0;
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await send('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'no-preference'}]});
  await load('no-selection');
  assert.equal((await owner()).inspectorContext, 'none');
  assert.equal(await evaluate('document.querySelector(".inspector-empty").textContent'), 'No selection');
  assert.equal(await evaluate('document.querySelector(".session-inspector").querySelectorAll("input,select").length'), 0);
  assert.equal((await identities())[3], 'a-opening');
  passed('No Selection has no stale form even while another Item is active');

  await load('playlist-selected-active-different');
  const name = 'input[data-inspector-name]';
  assert(await visible(name), 'Production Playlist Inspector has an editable Name');
  assert.equal((await owner()).inspectorContext, 'playlist');
  assert.equal(await evaluate('document.querySelector("[data-item-count]").textContent'), '2');
  assert(await visible(control('Playlist Master Volume')));
  assert(!await visible(control('Configured Rate')));
  passed('Playlist context: editable Name, selected Item Count and Master, no Item form');
  const renameIds = await identities(); const initialRev = await revision();
  await focus(name); await text(name, '  Independent Playlist  ');
  assert.equal(await revision(), initialRev, 'Name is editingFinished, not each keystroke');
  await key('Enter','Enter',13);
  assert.equal(await evaluate(q(playlist('playlist-b') + ' .entity-alias') + '.textContent'), '  Independent Playlist  ');
  assert.equal(await revision(), initialRev + 1); assert.deepEqual(await identities(), renameIds);
  await text(name, '   '); await blur();
  assert.equal(await evaluate(q(name) + '.value'), '  Independent Playlist  ');
  assert(!await evaluate('!!document.querySelector("dialog")'), 'Inline invalid name restores silently');
  await focus(name); await text(name, 'Playlist A'); await blur();
  assert.equal(await evaluate('[...document.querySelectorAll(".playlist-entity .entity-alias")].filter(e=>e.textContent==="Playlist A").length'), 2);
  const unchangedRev = await revision(); await focus(name); await key('Enter','Enter',13); await blur();
  assert.equal(await revision(), unchangedRev);
  await rename('playlist','playlist-b','   ');
  assert.equal(await evaluate('document.querySelector("dialog h2").textContent'), 'Invalid Name'); await dismissMessage();
  passed('Inline Name shares raw alias/duplicate/no-op contract, defers until finish, invalid restores silently; menu still warns');

  await load('playlist-selected-active-different'); const beforeMaster = await identities();
  for (const percent of [0,100,41]) { await numeric('Playlist Master Volume',percent); closeTo((await owner()).runtimeVolume,1); }
  assert.deepEqual(await identities(), beforeMaster);
  assert.equal((await snapshot()).data.dirty, 'true');
  await click(playlist('playlist-a')+' .entity-select'); await numeric('Playlist Master Volume',70);
  closeTo((await owner()).runtimeVolume,.7);
  assert.equal(await evaluate(q(control('Playlist Master Volume')) + '.min'), '0');
  assert.equal(await evaluate(q(control('Playlist Master Volume')) + '.max'), '100');
  passed('Master 0–100 immediate, selected inactive isolated, selected active updates output without activation');
  await load('playlist-selected'); await click('[aria-label="Add Media"]'); await click('[data-dialog-confirm]');
  assert.equal(await evaluate('document.querySelector("[data-item-count]").textContent'),'4');
  await click(playlist('playlist-d')+' .entity-select');
  assert(!await evaluate(q(control('Playlist Master Volume'))+'.disabled'));
  passed('Selected Playlist Item Count follows additions; disabled Playlist retains editable Inspector');

  await load('image-selected');
  assert.equal((await owner()).inspectorContext,'item'); assert.equal((await owner()).ownerId,'b-image');
  assert.equal(await evaluate('document.querySelector("[data-item-type]").textContent'),'Image');
  assert(!await evaluate('!!document.querySelector("[data-item-count]")'));
  const sourceBefore = await evaluate(q(control('Source'))+'.textContent');
  await focus(name); await text(name,'  Image alias  '); await key('Enter','Enter',13);
  assert.equal(await evaluate(q(control('Source'))+'.textContent'),sourceBefore);
  assert.equal(await evaluate(q(control('Source'))+'.title'),sourceBefore);
  assert.equal(await evaluate(q(control('Source'))+'.tabIndex'),0);
  assert.equal(await evaluate('getComputedStyle('+q(control('Source'))+').userSelect'),'text');
  passed('Item priority, editable alias, readonly Type and full selectable/focusable Source are independent');
  assert(await visible(control('Image Duration')));
  for(const label of ['Configured Rate','Apply Rate','Actual Rate','Volume','Playlist Master Volume']) assert(!await visible(control(label)));
  for (const [attribute,expected] of [['min','0.1'],['max','86400'],['step','0.1']]) assert.equal(await evaluate(q(control('Image Duration'))+'.'+attribute),expected);
  const imageIds = await identities(); const imageRev = await revision();
  await numeric('Image Duration',.1); assert.equal((await owner()).authoredDuration,'0.1');
  await numeric('Image Duration',86400); assert.equal((await owner()).authoredDuration,'86400');
  await blur(); assert.equal(await value('Image Duration'),'86400.00');
  assert.equal(await revision(),imageRev+2); assert.deepEqual(await identities(),imageIds);
  passed('Image-only duration: lower/upper/step/precision, immediate authored dirty and identity isolation');
  await load('active-image-duration'); const activeImageTime=(await snapshot()).time;
  await numeric('Image Duration',22);
  assert.equal(JSON.parse((await owner()).runtimeItem).duration,10);
  assert.equal((await snapshot()).time,activeImageTime);
  assert.equal((await owner()).authoredDuration,'22');
  passed('Active Image duration authors immediately but leaves loaded snapshot, time and Rail duration unchanged');

  await load('video-selected');
  assert.equal(await evaluate('document.querySelector("[data-item-type]").textContent'),'Video');
  assert(!await visible(control('Image Duration')));
  for (const label of ['Configured Rate','Apply Rate','Actual Rate','Volume']) assert(await visible(control(label)));
  for (const [attribute,expected] of [['min','.5'],['max','2'],['step','.1']]) closeTo(await evaluate(q(control('Configured Rate'))+'.'+attribute),expected);
  const draftIds = await identities(); const draftRev=await revision();
  await numeric('Configured Rate',1.6); await key('Enter','Enter',13); await blur();
  closeTo((await owner()).authoredRate,1.2); assert.equal(await revision(),draftRev); closeTo((await owner()).actualRate,1);
  await click(control('Apply Rate'));
  closeTo((await owner()).authoredRate,1.6); closeTo((await owner()).actualRate,1);
  assert.equal(await revision(),draftRev+1); assert.deepEqual(await identities(),draftIds);
  assert.equal(await evaluate(q(control('Actual Rate'))+'.textContent'),'1.00×');
  passed('Video Rate draft/Enter/blur do not author; Apply commits nonactive configured only, Actual stays global Engine');
  await load('selected-video-active'); await click('[aria-label="Pause"]'); const pausedTime=(await snapshot()).time;
  await numeric('Configured Rate',1.5); await click(control('Apply Rate'));
  closeTo((await owner()).authoredRate,1.5);closeTo((await owner()).actualRate,1.5);
  assert.equal((await snapshot()).time,pausedTime); assert.equal((await snapshot()).playing,'PAUSED');
  passed('Active Video Apply changes actual while preserving time, pause and selection');
  await load('configured-rate-runtime-failure'); await numeric('Configured Rate',1.7); await click(control('Apply Rate'));
  closeTo((await owner()).authoredRate,1.7);closeTo((await owner()).actualRate,1);
  assert.equal((await snapshot()).data.dirty,'true'); assert(!await evaluate('!!document.querySelector("dialog")'));
  passed('Runtime Rate failure retains authored configured and dirty, old Actual, no invented dialog/rollback');

  await load('video-selected'); const gainIds=await identities();
  await numeric('Volume',0); closeTo((await owner()).runtimeVolume,1);
  await numeric('Volume',100); closeTo((await owner()).runtimeVolume,1);
  await numeric('Volume',25);closeTo((await owner()).runtimeVolume,1);assert.deepEqual(await identities(),gainIds);
  await click(item('a-opening')+' .entity-select'); await numeric('Volume',50);closeTo((await owner()).runtimeVolume,.5);
  await click(playlist('playlist-b')+' .entity-select'); await click(playlist('playlist-a')+' .entity-select');
  await numeric('Playlist Master Volume',40);closeTo((await owner()).runtimeVolume,.2);
  passed('Video gain 0–100 immediate; nonactive isolation and active Master×Gain output scopes remain separate');

  await load('selected-video-active');
  const policy=control('Visual policy'), preset=control('Aspect preset');
  assert.equal(await value('Visual policy'),'auto-fill');
  await click(policy);
  assert.deepEqual(await evaluate('[...document.querySelectorAll("dialog [role=option] span:first-child")].map(e=>e.textContent)'),['Native','Auto Fill','Custom Aspect']);
  await key('Escape','Escape',27);
  assert(!await visible(preset));
  await choose(policy,'native'); assert(!await visible(preset));
  await choose(policy,'custom'); assert(await visible(preset));
  for (const [choice,pair,stored] of [['1',[16,9],[16,9]],['2',[16,10],[8,5]],['3',[4,3],[4,3]],['4',[1,1],[1,1]]]) {
    await choose(preset,choice); assert.equal(Number(await value('Aspect numerator')),pair[0]); assert.equal(Number(await value('Aspect denominator')),pair[1]);
    const authored=JSON.parse((await owner()).authoredPresentation).mediaPresentation.customAspect;
    assert.deepEqual([authored.numerator,authored.denominator],stored);
  }
  await choose(preset,'2'); const customRev=await revision(); await choose(preset,'custom');assert.equal(await revision(),customRev);
  await choose(preset,'2'); await numeric('Aspect numerator',25);
  assert.equal(await value('Aspect preset'),'2','Numeric edits retain current local preset');
  assert.equal(Number(await value('Aspect denominator')),10);
  for(const label of ['Aspect numerator','Aspect denominator']) { assert(!await evaluate(q(control(label))+'.disabled'));assert.equal(await evaluate(q(control(label))+'.min'),'1');assert.equal(await evaluate(q(control(label))+'.max'),'10000'); }
  await choose(policy,'native');await choose(policy,'auto-fill');await choose(policy,'custom');
  assert.equal(Number(await value('Aspect numerator')),25);assert.equal(Number(await value('Aspect denominator')),10);assert.equal(await value('Aspect preset'),'2');
  await click(item('a-scene')+' .entity-select');await click(item('a-opening')+' .entity-select');
  assert.equal(Number(await value('Aspect numerator')),5);assert.equal(Number(await value('Aspect denominator')),2);assert.equal(await value('Aspect preset'),'custom');
  passed('One visual policy; conditional ratio, all presets, normalized authored ratio, retained local controls and reselect reconstruction');

  await numeric('Aspect numerator',10000); await numeric('Aspect denominator',1);
  assert.deepEqual(JSON.parse((await owner()).authoredPresentation).mediaPresentation.customAspect,{numerator:10000,denominator:1});
  await numeric('Aspect numerator',1); await numeric('Aspect denominator',10000);
  assert.deepEqual(JSON.parse((await owner()).authoredPresentation).mediaPresentation.customAspect,{numerator:1,denominator:10000});
  const ratioBoundRev=await revision();
  await numeric('Aspect numerator',0);await blur();assert.equal(await value('Aspect numerator'),'1');
  await numeric('Aspect denominator',10001);await blur();assert.equal(await value('Aspect denominator'),'10000');
  assert.equal(await revision(),ratioBoundRev);
  passed('Custom ratio accepts both 1/10000 endpoints and rejects invalid UI drafts without dirtying');

  await load('selected-item-not-active');const visualIds=await identities(); const oldRuntime=(await owner()).runtimeItem;
  await numeric('Uniform scale',.1);await numeric('Uniform scale',3);
  await numeric('Position X normalized',-1);await numeric('Position Y normalized',1);
  const layout=JSON.parse((await owner()).authoredPresentation).layout;
  assert.deepEqual(layout,{positionX:-1,positionY:1,scaleX:3,scaleY:3});
  assert.equal((await owner()).runtimeItem,oldRuntime);assert.deepEqual(await identities(),visualIds);
  assert(await evaluate(q(control('Position Y normalized'))+'.title.includes("up")'));
  for(const [label,min,max,step] of [['Uniform scale',.1,3,.1],['Position X normalized',-1,1,.05],['Position Y normalized',-1,1,.05]]) {
    for(const [key,expected] of [['min',min],['max',max],['step',step]]) closeTo(await evaluate(q(control(label))+'.'+key),expected);
  }
  passed('Uniform scale writes X/Y together, normalized X/Y bounds and positive-Y-up; nonactive visual edits do not rebind');
  await load('selected-video-active');const visualTime=(await snapshot()).time;await numeric('Uniform scale',1.4);
  closeTo(JSON.parse((await owner()).runtimeItem).presentation.layout.scaleX,1.4);assert.equal((await snapshot()).time,visualTime);
  for (const [scenario,time,hasActive] of [['visual-prepare-failure','00:08/00:23',true],['visual-bind-failure','00:08/00:23',true],['visual-seek-failure','00:00/00:23',true],['visual-rollback-failure','00:00/00:00',false],['visual-rollback-seek-failure','00:00/00:23',true]]) {
    await load(scenario);await numeric('Uniform scale',1.8);
    closeTo(JSON.parse((await owner()).authoredPresentation).layout.scaleX,1.8);
    assert.equal(Boolean((await identities())[3]),hasActive);assert.equal((await snapshot()).time,time);
    assert(!await evaluate('!!document.querySelector("dialog")'),'Visual failure remains silent: '+scenario);
    if(hasActive) {closeTo((await owner()).actualRate,1.3); assert.equal((await snapshot()).playing,'PAUSED');}
  }
  passed('Active visual rebuild preserves time/pause/rate; prepare/bind/seek/rollback failures keep authored edits and stay silent');

  await load('same-source-independent-items');
  const sameSource = await evaluate(q(control('Source'))+'.textContent');
  await numeric('Volume',22);await numeric('Uniform scale',1.8);await numeric('Configured Rate',1.9);await click(control('Apply Rate'));
  await click(item('a-opening')+' .entity-select');assert.equal(await evaluate(q(control('Source'))+'.textContent'),sameSource);
  assert.equal(await value('Volume'),'100');assert.equal(await value('Uniform scale'),'1.00');assert.equal(await value('Configured Rate'),'1.0');
  await click(item('a-opening-copy')+' .entity-select');assert.equal(await value('Volume'),'22');assert.equal(await value('Uniform scale'),'1.80');assert.equal(await value('Configured Rate'),'1.9');
  assert.equal((await identities())[3],'a-opening');
  passed('Same-source occurrences retain independent Inspector gain/rate/visual values without activation');

  await load('selected-video-active');
  async function staleDrafts() {
    await numeric('Configured Rate',1.7); await choose(control('Visual policy'),'custom');
    await choose(control('Aspect preset'),'2');await numeric('Aspect numerator',25);
    await numeric('Position Y normalized',.6);
  }
  async function dev(command) {
    await click('[aria-label="Dev Session scenarios"]');
    if(command === 'reset') await click('dialog .session-dialog-actions button:first-child');
    else await choose('[aria-label="Dev Session scenario"]',command);
    await click('dialog .session-dialog-actions button:last-child');
  }
  await staleDrafts();await dev('reset');
  assert.equal(await value('Configured Rate'),'1.0','Reset must reconstruct same-ID Inspector drafts');
  assert.equal(await value('Aspect preset'),'1');
  assert.equal(Number(await value('Aspect numerator')),16);assert.equal(Number(await value('Aspect denominator')),9);
  await click(control('Apply Rate'));closeTo((await owner()).authoredRate,1);assert.equal((await snapshot()).data.dirty,'false');
  await numeric('Position X normalized',.2);closeTo(JSON.parse((await owner()).authoredPresentation).layout.positionY,0);
  await staleDrafts();await dev('custom-aspect');
  assert.equal(await value('Configured Rate'),'1.0');assert.equal(await value('Aspect preset'),'custom');
  assert.equal(Number(await value('Aspect numerator')),8);assert.equal(Number(await value('Aspect denominator')),5);
  await numeric('Position X normalized',.4);closeTo(JSON.parse((await owner()).authoredPresentation).layout.positionY,0);
  passed('Real DEV reset and same-ID scenario switch reconstruct rate/ratio/preset/position drafts without stale writes');

  await load('selected-video-active');await choose(control('Visual policy'),'custom');await choose(control('Aspect preset'),'2');
  await numeric('Aspect numerator',25);await focus(control('Aspect preset'));
  await key(' ','Space',32);await key('Enter','Enter',13);
  assert.equal(Number(await value('Aspect numerator')),16,'Activating the already-selected preset resets its local ratio, as Qt activated does');
  assert.equal(Number(await value('Aspect denominator')),10);
  await load('visual-seek-failure');const policyRepeatRev=await revision();
  await focus(control('Visual policy'));await key(' ','Space',32);await key('Enter','Enter',13);
  assert.equal((await snapshot()).time,'00:00/00:23','Same policy activation still attempts active rebuild');
  assert.equal(await revision(),policyRepeatRev);
  passed('Keyboard activation of current preset/policy follows Qt activated, even when selected index is unchanged');

  await load('selected-video-active');await numeric('Configured Rate',1.7);
  await choose(control('Visual policy'),'custom');await choose(control('Aspect preset'),'2');
  await entityCommand('item','a-opening','play');
  assert.equal(await value('Configured Rate'),'1.0','Production refreshLibrary rebuilds Inspector after same-Item Play');
  assert.equal(await value('Aspect preset'),'custom');assert.equal(Number(await value('Aspect numerator')),8);assert.equal(Number(await value('Aspect denominator')),5);
  await numeric('Configured Rate',1.6);await rename('item','a-opening','Opening');
  assert.equal(await value('Configured Rate'),'1.0','Successful unchanged menu Rename still rebuilds Inspector');
  passed('Same-owner entity commands follow production refreshLibrary Inspector lifetime without authoring drafts');

  for(const scenario of ['long-alias','long-source']) {
    await load(scenario);
    const g=await evaluate('(()=>{const e=document.querySelector(".session-inspector");return {width:e.clientWidth,scroll:e.scrollWidth,page:document.documentElement.scrollWidth,viewport:innerWidth,source:document.querySelector(".inspector-source").textContent,title:document.querySelector(".inspector-source").title};})()');
    assert(g.scroll<=g.width+1);assert(g.page<=g.viewport+1);assert.equal(g.source,g.title);
    assert(await evaluate(q(name)+'.title === '+q(name)+'.value'));
  }
  passed('Long mixed-language alias/Source retain full access without panel/page overflow');
  await load('selected-video-active');
  await focus(control('Configured Rate')); await key('Tab','Tab',9);assert.equal(await evaluate('document.activeElement.getAttribute("aria-label")'),'Apply Rate');
  await key('Tab','Tab',9,8);assert.equal(await evaluate('document.activeElement.getAttribute("aria-label")'),'Configured Rate');
  await focus(control('Visual policy'));assert(await evaluate('getComputedStyle(document.activeElement).outlineStyle!=="none"'));
  await focus(control('Volume'));await key('ArrowLeft','ArrowLeft',37);assert.equal(await value('Volume'),'99');
  await focus(control('Position X normalized'));await evaluate('document.activeElement.select()');
  await send('Input.insertText',{text:'-'});await send('Input.insertText',{text:'0.35'});await key('Enter','Enter',13);await blur();
  closeTo(JSON.parse((await owner()).authoredPresentation).layout.positionX,-.35);
  passed('Real keyboard Tab/ShiftTab, Apply focus, slider key and negative numeric draft');
  for(const label of ['Rotation','Opacity','Visible','60 FPS']) assert(!await evaluate('!!'+q(control(label))));
  assert(!await evaluate('document.querySelector(".session-inspector").textContent.includes("60 FPS")'));
  passed('False Rotation/Opacity/Visible/FPS coverage absent');
  assert.equal(runtimeErrors.length,0); assert.equal(warnings.length,0);
  await fs.writeFile(new URL('inspector-ui-green.json',evidence),JSON.stringify({pass:true,checks,runtimeErrors,warnings},null,2));
  console.log(JSON.stringify({pass:true,checks:checks.length,runtimeErrors,warnings}));
} catch(error) {
  await fs.writeFile(new URL(process.env.INSPECTOR_RED === '1' ? 'inspector-ui-red.json':'inspector-ui-failed.json',evidence),JSON.stringify({pass:false,error:String(error),checks,runtimeErrors,warnings},null,2));
  throw error;
} finally {socket.close();}
