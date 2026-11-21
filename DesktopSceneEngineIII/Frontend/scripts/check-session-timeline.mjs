import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// Isolated headless QA page only. No screenshots, native dialogs or user profile.
const evidence = new URL('../node_modules/.cache/session-phase-d/', import.meta.url);
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
  await send('Page.navigate', { url: 'http://127.0.0.1:5173/?view=session&timelineClock=manual&sessionScenario=' + scenario });
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
const rail = () => evaluate('({...document.querySelector(".playback-rail").dataset})');
const rev = async () => Number((await snapshot()).data.revision);
const visible = selector => evaluate('!!'+q(selector)+'?.getBoundingClientRect().width');
const closeTo = (actual,expected) => assert(Math.abs(Number(actual)-expected)<1e-6,actual+' vs '+expected);
async function numeric(label,number) {await evaluate(q(control(label))+'.focus()');await text(control(label),String(number));}
async function fact(name) {await click('[aria-label="Dev Session scenarios"]');await click('[data-runtime-fact='+JSON.stringify(name)+']');}
async function selectPlaylist(id) {await click(playlist(id)+' .entity-select');}
async function selectItem(id) {await click(item(id)+' .entity-select');}
const timeline = () => evaluate('({...document.querySelector(".session-timeline").dataset})');
async function dev(action) { await evaluate('window.dispatchEvent(new CustomEvent("dse-timeline-dev",{detail:'+JSON.stringify(action)+'}))'); await rest(); }
async function point(value) {return evaluate('(()=>{const r=document.querySelector(".timeline-groove").getBoundingClientRect();return {x:Math.round(r.left)+(Math.round(r.width)-1)*'+value+'/10000,y:r.top+1};})()');}
async function mouse(type,p,down=false) {await send('Input.dispatchMouseEvent',{type,x:p.x,y:p.y,button:type==='mouseMoved'&&!down?'none':'left',buttons:down?1:0,clickCount:type==='mouseMoved'?0:1});await rest();}
async function hover(value) {await mouse('mouseMoved',await point(value));}
async function press(value) {const p=await point(value);await mouse('mouseMoved',p);await mouse('mousePressed',p,true);}
async function moveDrag(value) {await mouse('mouseMoved',await point(value),true);}
async function release(value) {await mouse('mouseReleased',await point(value));}
async function seek(value) {await press(value);await moveDrag(value);await release(value);}
async function leave() {await mouse('mouseMoved',{x:10,y:80});}
try {
  await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable'); await send('Log.clear'); await send('Runtime.discardConsoleEntries'); runtimeErrors.length=warnings.length=0;
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await load('playback-list-loop');
  assert(await evaluate('!!document.querySelector(".session-timeline [role=slider]")'),'Phase D active runtime Timeline exists');
  passed('Active runtime Timeline exists');
  const lifecycle=[];
  await load('timeline-valid-video');await press(3000);await moveDrag(6000);await numeric('Position X normalized',.2);await rest();lifecycle.push((await timeline()).dragging==='false');await release(6000);
  await load('timeline-valid-video');await press(3000);await moveDrag(6000);await entityCommand('item','a-disabled','delete');await click('[data-dialog-confirm]');lifecycle.push((await timeline()).dragging==='false');await release(6000);
  await load('timeline-valid-video');await seek(0);await hover(2500);await dev('advance75');await dev('resolve-latest');const anchorFrame=await timeline();await click(control('Previous'));lifecycle.push((await timeline()).generation===anchorFrame.generation&&(await timeline()).frameId===anchorFrame.frameId);
  await load('timeline-valid-video');await hover(2500);await dev('advance75');await dev('resolve-latest');const replayFrame=await timeline();await entityCommand('item','a-opening','play');lifecycle.push((await timeline()).generation===replayFrame.generation&&(await timeline()).frameId===replayFrame.frameId);
  await load('visual-prepare-failure');await press(3000);await moveDrag(6000);await numeric('Position X normalized',.2);lifecycle.push((await timeline()).dragging==='true');await release(6000);assert(Math.abs(Number((await timeline()).runtimeTime)-13.8)<.1);
  await load('playback-repeat-item');await press(3000);await dev('complete');lifecycle.push((await timeline()).dragging==='true');await release(3000);assert(Math.abs(Number((await timeline()).runtimeTime)-6.9)<.1);
  assert.deepEqual(lifecycle,[true,true,true,true,true,true],'Rebuild cancellation, successful-switch-only hover invalidation, prepare-failure and natural same-ID snapshot parity');
  passed('Production projection rebuild cancellation and successful-switch-only hover invalidation, including no-op Previous and same-source manual Play');
  const timerProbe=await send('Page.addScriptToEvaluateOnNewDocument',{source:'window.timelineFixtureTimers=new Map();let fixtureTimer=900000;const nativeTimeout=window.setTimeout.bind(window),nativeClear=window.clearTimeout.bind(window);window.setTimeout=(callback,delay,...args)=>{if(delay===500){const id=++fixtureTimer;window.timelineFixtureTimers.set(id,()=>callback(...args));return id;}return nativeTimeout(callback,delay,...args);};window.clearTimeout=id=>{if(!window.timelineFixtureTimers.delete(id))nativeClear(id);};'});
  await load('drag-stale-item');await send('Page.removeScriptToEvaluateOnNewDocument',{identifier:timerProbe.identifier});await press(2500);
  assert.equal(await evaluate('window.timelineFixtureTimers.size'),1);await click('[aria-label="Dev Session scenarios"]');await click('dialog .session-dialog-actions button');
  assert.equal(await evaluate('window.timelineFixtureTimers.size'),0,'Reset same scenario cancels the prior armed runtime-fact timer');
  await evaluate('for(const callback of window.timelineFixtureTimers.values())callback();window.timelineFixtureTimers.clear()');await rest();assert.equal((await identities())[3],'a-opening');await release(2500);
  passed('Reset same scenario fences a pending DEV runtime-fact timer, so a previous drag cannot mutate a fresh fixture');
  for(const [scenario,enabled,duration] of [['timeline-valid-video',true,23],['timeline-valid-image',true,10],['timeline-no-media',false,0],['timeline-zero-duration',false,0],['timeline-invalid-duration',false,0]]){
    await load(scenario);const t=await timeline();assert.equal(t.enabled,String(enabled));assert.equal(Number(t.duration),duration);
    assert.equal(await evaluate(q(control('Playback position'))+'.getAttribute("aria-disabled")'),String(!enabled));
    assert.equal(await evaluate(q(control('Playback position'))+'.tabIndex'),enabled?0:-1);
    if(!enabled){assert.equal(await evaluate('document.querySelector(".timeline-time").textContent'),'00:00 / 00:00');await seek(5000);assert.equal((await timeline()).dragging,'false');}
  }
  passed('Video/Image enable; no media/zero/nonfinite disable, zero projection and no drag');
  await load('selected-not-active');await evaluate('window.timelinePointerLog=[];for(const type of ["pointerdown","pointermove","pointerup","gotpointercapture","lostpointercapture","pointercancel"])document.addEventListener(type,e=>window.timelinePointerLog.push([type,e.buttons,e.target.className]),true)');const ids=await identities(),revision=await rev();let t=await timeline();assert.equal(t.itemId,'a-scene');assert.equal(Number(t.duration),107);assert.equal(Number(t.runtimeTime),8);
  await press(2500);assert.equal((await timeline()).dragging,'true');assert.equal(Number((await timeline()).runtimeTime),8);
  await moveDrag(7000);t=await timeline();assert.equal(Number(t.runtimeTime),8);assert(Math.abs(Number(t.previewTime)-74.9)<.2,JSON.stringify({t,events:await evaluate('window.timelinePointerLog')}));assert.equal((await rail()).activeItemId,'a-scene');assert.equal(await rev(),revision);
  const preview=Number(t.previewTime);await release(7000);t=await timeline();closeTo(t.runtimeTime,preview);assert.equal(t.dragging,'false');assert.deepEqual(await identities(),ids);assert.equal(await rev(),revision);
  assert.equal(await evaluate('document.querySelector(".timeline-time").textContent.replaceAll(" ","")'),(await snapshot()).time);
  passed('Press snapshots active A/A2; Move preview only; Release commits one shared runtime fact without selection/dirty change');
  await load('selected-not-active');
  assert.deepEqual(await evaluate('[".timeline-control",".timeline-groove",".timeline-progress",".timeline-thumb"].map(selector=>getComputedStyle(document.querySelector(selector)).cursor)'),['default','default','default','default']);
  const slider=q(control('Playback position'));
  await evaluate(slider+'.addEventListener("pointerdown",event=>window.timelinePointerId=event.pointerId,{once:true})');
  await press(3000);await moveDrag(7500);
  assert.equal(await evaluate(slider+'.hasPointerCapture(window.timelinePointerId)'),true);
  await evaluate('window.dispatchEvent(new Event("blur"))');await rest();
  assert.equal(await evaluate(slider+'.hasPointerCapture(window.timelinePointerId)'),false);
  assert.equal((await timeline()).dragging,'false');
  await release(7500);
  assert.equal(Number((await timeline()).runtimeTime),8,'Blur cancels the draft without seeking');
  await seek(5500);
  assert(Math.abs(Number((await timeline()).runtimeTime)-58.85)<.2,'Next drag works immediately');
  passed('DEV Timeline shares the normal cursor and cancels capture on window blur without committing');
  await load('seek-paused');const preserved=await rail(),seekRevision=await rev(),seekIds=await identities();await seek(5000);const afterSeek=await rail();
  for(const field of ['activePlaylistId','activeItemId','actualRate','embeddedGain','outputMuted','effectiveOutput'])assert.equal(afterSeek[field],preserved[field]);
  // UI-PIC-001 removes Rail Master; read the same authored value from its explicit-ID row without changing Inspector selection.
  assert.equal((await snapshot()).playing,'PAUSED');assert.equal(await rev(),seekRevision);assert.equal(await value('Volume'),'50');
  assert.equal(await evaluate(q(playlist(preserved.activePlaylistId)+' .playlist-volume-trigger')+'.dataset.masterPercent'),'70');assert.deepEqual(await identities(),seekIds);
  assert(Math.abs(Number((await timeline()).runtimeTime)-11.5)<.1);
  passed('Ordinary seek preserves Paused,1.25actual,70%Master,50%Gain,Mute and authored state');
  await load('seek-failure');await seek(5000);assert.equal(Number((await timeline()).runtimeTime),8);assert(!await evaluate('!!document.querySelector("dialog[open]")'));
  passed('Seek failure silently refreshes actual position, without invented feedback');
  for(const action of ['external-next','complete','duration']) {
    await load('selected-not-active');const selected=(await identities()).slice(0,2),r=await rev();await press(3000);await moveDrag(10000);await dev(action);const realTime=(await timeline()).runtimeTime,activeIds=(await identities()).slice(2);
    await release(10000);assert.deepEqual((await identities()).slice(2),activeIds,'No accidental second Next after stale '+action);assert.equal((await timeline()).runtimeTime,realTime);assert.equal((await timeline()).dragging,'false');assert.deepEqual((await identities()).slice(0,2),selected);assert.equal(await rev(),r);
  }
  passed('External Next/natural completion/duration change reject stale Release, including exact end');
  for(const scenario of ['playback-prepare-failure','playback-cross-failure']) {
    await load(scenario);await press(3000);await moveDrag(10000);await dev('complete');assert.equal((await timeline()).dragging,'false','Cross start cancels before failed preparation/bind');
    const failedTime=(await timeline()).runtimeTime,failedItem=(await identities())[3];await dismissMessage();await release(10000);assert.equal((await timeline()).runtimeTime,failedTime);assert.equal((await identities())[3],failedItem);assert(!await evaluate('!!document.querySelector("dialog[open]")'));
  }
  passed('Failed natural cross-Playlist start cancels old drag even when rollback retains Item/duration, without a second Release intent');
  await load('right-edge-9999');await dev('9999');assert.equal((await identities())[3],'a-scene');assert(Number((await timeline()).runtimeTime)<107);assert(Number((await timeline()).runtimeTime)>106.98);
  await load('right-edge-10000');const edgeSelection=(await identities()).slice(0,2),edgeRev=await rev();await dev('10000');assert.deepEqual(await identities(),[...edgeSelection,'playlist-b','b-opening']);assert.equal(await rev(),edgeRev);
  await load('playback-repeat-item');await dev('10000');assert.equal((await identities())[3],'a-opening');assert.equal(Number((await timeline()).runtimeTime),0);
  await load('playback-no-target');await dev('10000');assert.equal((await rail()).runtimeConfigured,'false');assert.equal((await timeline()).enabled,'false');
  passed('Exact9999 seeks;10000 reuses real Phase C Next with filtering/cross/Repeat/safe-stop and stable selection');
  await load('timeline-valid-video');const keyRev=await rev();await evaluate(q(control('Playback position'))+'.focus()');await key('End','End',35);
  assert.equal(await evaluate('document.querySelector("[role=slider]").getAttribute("aria-valuenow")'),'10000');assert.equal(Number((await timeline()).runtimeTime),8);await key('Enter','Enter',13);assert.equal((await identities())[3],'a-opening');assert.equal(Number((await timeline()).runtimeTime),8);assert.equal(await rev(),keyRev);
  await fact('advance');assert.equal(await evaluate('document.querySelector("[role=slider]").getAttribute("aria-valuenow")'),String(Math.round(Math.fround(13/23)*10000)));
  passed('Accessible slider keyboard follows production thumb-only changes; Enter/End never synthesize seek/Next; runtime refresh restores thumb');

  await load('hover-valid');await hover(2500);t=await timeline();assert.equal(t.hoverActive,'true');assert.equal(t.hoverStatus,'loading');assert.equal(t.hoverItem,'a-opening');assert.equal(Number(t.submissions),0);await dev('advance74');assert.equal(Number((await timeline()).submissions),0);
  await hover(7500);await dev('advance1');t=await timeline();assert.equal(Number(t.submissions),1,'First-event timer not restarted by movement');assert.equal(t.hoverStatus,'loading');
  await dev('resolve-latest');t=await timeline();assert.equal(t.hoverStatus,'valid');assert.equal(t.frameBucket,'15000');assert.equal(await evaluate('document.querySelector(".timeline-hover-time").textContent'),'00:17');
  passed('75ms fake-clock first-event debounce, raw timestamp rather than representativebucket, Loading→Valid');
  const validFrame=t.frameId;await hover(7600);assert.equal(Number((await timeline()).submissions),1);await dev('advance75');assert.equal(Number((await timeline()).submissions),1);
  await hover(4500);assert.equal((await timeline()).frameId,validFrame);assert.equal((await timeline()).hoverStatus,'valid');await dev('advance75');assert.equal(Number((await timeline()).submissions),2);await dev('resolve-latest');assert.notEqual((await timeline()).frameId,validFrame);
  passed('Same bucket suppresses redundant submissions; new bucket retains same-source valid frame until replacement');
  await load('hover-out-of-order');await hover(2500);await dev('advance75');await hover(7500);await dev('advance75');await dev('resolve-latest');const latestFrame=(await timeline()).frameId;await dev('resolve-oldest');assert.equal((await timeline()).frameId,latestFrame);assert.equal(Number((await timeline()).staleRejected),1);
  passed('New result B wins; late result A cannot replace it');
  await load('hover-unavailable');await hover(5000);assert.equal((await timeline()).hoverStatus,'loading');await dev('advance75');await dev('resolve-latest');assert.equal((await timeline()).hoverStatus,'unavailable');assert.equal(await evaluate('document.querySelector(".timeline-thumbnail").textContent'),'Preview unavailable');
  passed('Unavailable is explicit and distinct from Loading/Valid');
  for(const invalidation of ['leave','drag','hide','invalidate','external-next']){
    await load('hover-source-change');await hover(2500);await dev('advance75');
    if(invalidation==='leave')await leave();else if(invalidation==='drag')await press(4000);else await dev(invalidation);
    assert.equal((await timeline()).hoverActive,'false');await dev('resolve-oldest');assert.equal((await timeline()).hoverActive,'false');assert.equal((await timeline()).frameId,'');assert.equal(Number((await timeline()).staleRejected),1);
    if(invalidation==='drag'){await moveDrag(6000);assert.equal((await timeline()).hoverActive,'false');await release(6000);assert.equal((await timeline()).hoverActive,'false');}
  }
  passed('Leave/Press/hide/generation/source change reject pending result and never reopen hover; drag suppresses hover');
  await load('hover-generation-change');await hover(2500);await dev('advance75');const generation=Number((await timeline()).generation);await click(control('Next'));assert.equal((await identities())[3],'a-opening');assert(Number((await timeline()).generation)>generation);await dev('resolve-oldest');assert.equal((await timeline()).frameId,'');
  passed('Same-Item Repeat Next invalidates generation independently of source equality');
  await load('selected-not-active');await hover(5000);assert.equal((await timeline()).hoverItem,'a-scene');assert((await timeline()).hoverSource.endsWith('Scene-A.mp4'));await dev('advance75');await dev('resolve-latest');assert.equal((await timeline()).hoverStatus,'valid');assert.equal((await identities())[1],'b-third');
  passed('Hover thumbnail source is active A/A2 while Inspector remains selected B/B3');
  await load('timeline-valid-image');await hover(2000);await dev('advance75');assert.equal((await timeline()).hoverStatus,'valid');assert.equal(Number((await timeline()).imageReads),1);assert.equal(Number((await timeline()).submissions),0);const imageFrame=(await timeline()).frameId;await hover(8000);await dev('advance75');assert.equal((await timeline()).frameId,imageFrame);assert.equal(Number((await timeline()).imageReads),1);
  passed('Image uses debounced synchronous provider mock and retains one frame across synthetic time');
  await load('hover-valid');await hover(3000);await dev('advance75');await dev('resolve-latest');const beforeComposition=await timeline();await numeric('Position X normalized',.2);await evaluate(q(control('Position X normalized'))+'.blur()');await rest();const afterComposition=await timeline();
  assert.equal(afterComposition.frameId,beforeComposition.frameId);assert.equal(afterComposition.generation,beforeComposition.generation);assert.equal(afterComposition.submissions,beforeComposition.submissions);assert(Number(afterComposition.presentationGeneration)>Number(beforeComposition.presentationGeneration));
  passed('Inspector presentation recomposes same mock frame without new decode identity/submission');
  for(const [width,height]of [[1800,1000],[1280,800],[1000,760],[680,700],[440,680],[1280,560]]){
    await load('hover-valid');await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});await rest();
    const stableRects=()=>evaluate('(()=>{const r=s=>{const a=document.querySelector(s).getBoundingClientRect();return {x:a.x,y:a.y,width:a.width,height:a.height}};return [".session-workspace",".session-library",".preview-viewport",".preview-native-surface",".session-inspector",".session-timeline",".timeline-groove",".playback-rail"].map(r);})()');
    const beforeHover=await stableRects();await hover(5000);await dev('advance75');await dev('resolve-latest');
    const g=await evaluate('(()=>{const r=s=>{const a=document.querySelector(s).getBoundingClientRect();return {x:a.x,y:a.y,right:a.right,bottom:a.bottom,width:a.width,height:a.height}};return {popup:r(".timeline-hover-popup"),workspace:r(".session-workspace"),timeline:r(".session-timeline"),groove:r(".timeline-groove"),rail:r(".playback-rail"),page:document.documentElement.scrollWidth};})()');
    assert(g.page<=width+1);assert(g.groove.width>40);assert(g.popup.y<g.workspace.bottom,'Floating popup may overlap future native Preview '+width);assert(Math.abs(g.popup.bottom-(g.groove.y-8))<1);assert(g.popup.y>=8);assert(g.popup.x>=8&&g.popup.right<=width-8+1);assert.equal(g.rail.height,64);assert.deepEqual(await stableRects(),beforeHover,'Floating hover never changes Preview/columns/Timeline/Rail');
    await send('Emulation.setDeviceMetricsOverride',{width:width-20,height,deviceScaleFactor:1,mobile:false});await rest();assert.equal((await timeline()).hoverStatus,'valid');
  }
  passed('Six responsive sizes preserve all layout rectangles,64px Rail and floating groove-anchored overlay; native airspace remains deferred');
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await load('hover-valid');for(let i=0;i<80;i++){await hover((i*719)%9900);await dev('advance75');if(i%2)await dev('resolve-latest');if(i%7===0){await leave();await dev('resolve-oldest');}}
  await leave();await dev('resolve-latest');assert.equal((await timeline()).hoverActive,'false');
  passed('Sustained rapid hover/leave/out-of-order completion stays stable without reopening stale presentation');
  assert.equal(runtimeErrors.length,0);assert.equal(warnings.length,0);
  await fs.writeFile(new URL('timeline-ui-green.json',evidence),JSON.stringify({pass:true,checks,runtimeErrors,warnings},null,2));
  console.log(JSON.stringify({pass:true,checks:checks.length,runtimeErrors:runtimeErrors.length,warnings:warnings.length}));
} catch(error) {
  await fs.writeFile(new URL('timeline-ui-failed.json',evidence),JSON.stringify({pass:false,error:String(error),checks,runtimeErrors,warnings},null,2));throw error;
} finally {socket.close();}
