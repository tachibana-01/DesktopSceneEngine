import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import { previewGeometry } from '../src/sessionPreviewGeometry.ts';

// Isolated headless QA page only. No screenshots, native dialogs or user profile.
const evidence = new URL('../node_modules/.cache/session-phase-e/', import.meta.url);
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
const rail = () => evaluate('({...document.querySelector(".playback-rail").dataset})');
const rev = async () => Number((await snapshot()).data.revision);
const visible = selector => evaluate('!!'+q(selector)+'?.getBoundingClientRect().width');
const closeTo = (actual,expected) => assert(Math.abs(Number(actual)-expected)<1e-6,actual+' vs '+expected);
async function numeric(label,number) {await evaluate(q(control(label))+'.focus()');await text(control(label),String(number));}
async function fact(name) {await click('[aria-label="Dev Session scenarios"]');await click('[data-runtime-fact='+JSON.stringify(name)+']');}
async function selectPlaylist(id) {await click(playlist(id)+' .entity-select');}
async function selectItem(id) {await click(item(id)+' .entity-select');}
const preview=()=>evaluate('({...document.querySelector(".preview-phase-e").dataset})');
async function bounds(){return evaluate('(()=>{const r=s=>{const a=document.querySelector(s).getBoundingClientRect();return {x:a.x,y:a.y,width:a.width,height:a.height,right:a.right,bottom:a.bottom};};return {viewport:r(".preview-viewport"),frame:r(".preview-chrome-frame"),native:r(".preview-native-surface"),header:r(".preview-context-e"),available:document.querySelector(".preview-chrome-frame").dataset.previewVisible};})()');}
async function openView(){await click('[aria-label="Preview View preferences"]');}
async function displayFact(action){await click('[aria-label="Dev Session scenarios"]');await click('dialog details:last-of-type summary');await click('[data-presentation-dev='+JSON.stringify(action)+']');await click('dialog .session-dialog-actions button:last-child');}
const canvas=()=>evaluate('document.querySelector("[data-canvas-aspect]").dataset.canvasAspect');
try {
 await send('Page.enable');await send('Runtime.enable');await send('Log.enable');await send('Log.clear');await send('Runtime.discardConsoleEntries');runtimeErrors.length=warnings.length=0;
 await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});await load('normal');
 assert(await evaluate('!!document.querySelector("[data-canvas-aspect]")'),'Phase E authored Canvas output exists');
 passed('Authored Canvas output exists');
 for(const [scenario,aspect]of [['canvas-16-9-display-16-9',[16,9]],['canvas-16-9-display-16-10',[16,9]],['canvas-16-10',[16,10]],['canvas-4-3',[4,3]],['canvas-1-1',[1,1]],['canvas-ultrawide',[32,9]],['canvas-portrait',[9,16]]]){
  await load(scenario);assert.equal(await canvas(),aspect.join(':'));const b=await bounds(),g=previewGeometry(b.viewport,aspect);assert.equal(b.available,'true');
  for(const name of ['frame','native'])for(const field of ['x','y','width','height'])assert(Math.abs(b[name][field]-(g[name][field]+(field==='x'?b.viewport.x:field==='y'?b.viewport.y:0)))<.01,scenario+' '+name+' '+field);
  assert(b.native.y>=b.header.bottom);assert.equal(await evaluate('document.querySelector(".preview-native-surface").children.length'),0);
 }
 passed('Authored Canvas ratios and mismatchedDisplay apply exact measured integer fit/margins with clean native rectangle');
 for(const scenario of ['preview-tiny','preview-zero']){await load(scenario);assert.equal((await bounds()).available,'false');assert.equal((await bounds()).native.width,0);assert(await evaluate('document.querySelector(".preview-caption").textContent.includes("UNAVAILABLE")'));}
 passed('Zero/tiny available geometry hides the surface rather than stretching a broken frame');
 await load('display-primary');let p=await preview();assert.equal(p.boundId,'mock-persistent-display-a');assert(await evaluate('document.querySelector(".bound-display").textContent.includes("Display 1 — Studio Display — 2560×1440 (Primary)")'));
 await load('display-secondary');assert.equal((await preview()).boundId,'mock-persistent-display-b');assert(await evaluate('document.querySelector(".bound-display").textContent.includes("Display 2")'));assert(!await evaluate('document.querySelector(".bound-display").textContent.includes("Primary")'));
 await load('display-long-name');assert.equal(await evaluate('document.querySelector(".bound-display").title'),await evaluate('document.querySelector(".bound-display").textContent'));assert((await preview()).boundId.length>200);
 assert(!await evaluate('!!'+q('[aria-label="Change Display"],[aria-label="Refresh displays"]')));
 passed('Exact persistent binding, index/friendly/dimensions/Primary/fulllabeltooltip and long identity; no Display selector/Refresh command');
 for(const [scenario,text]of [['display-unavailable','Selected display unavailable'],['enumeration-failed','Unable to enumerate displays']]){await load(scenario);const c=await canvas(),r=await rev(),id=(await preview()).boundId;assert.equal(await evaluate('document.querySelector(".bound-display").textContent'),text);assert(await evaluate(q(control('Identify bound Display'))+'.disabled'));assert(await evaluate(q(control('Deploy'))+'.disabled'));await displayFact('return');assert.equal((await preview()).boundId,id);assert.equal(await canvas(),c);assert.equal(await rev(),r);assert(!await evaluate(q(control('Identify bound Display'))+'.disabled'));}
 passed('Unavailable and enumeration failure are distinct; exact return restores capability without rebinding/Canvas/dirty mutation');
 await load('canvas-16-9-display-16-10');await projectCommand('new');assert.equal(await canvas(),'8:5');assert.equal((await snapshot()).data.dirty,'false');
 await load('display-unavailable');await projectCommand('new');assert.equal(await canvas(),'16:9');
 passed('New seeds reduced exact Display ratio; unresolvedNew keeps safeCanvasdefault, while loadedCanvas remains independent');
 for(const scenario of ['identify-ready','identify-failure','identify-resolution-loss']){await load(scenario);const before=await snapshot(),id=(await preview()).boundId,c=await canvas(),g=await bounds();await click(control('Identify bound Display'));p=await preview();assert.equal(p.identifyRequests,'1');assert.equal(p.boundId,id);assert.equal(await canvas(),c);assert.deepEqual((await snapshot()).data,before.data);assert.equal(p.identifyStatus,scenario==='identify-ready'?'shown':scenario==='identify-failure'?'api-failure':'unresolved');assert(!await evaluate('!!document.querySelector("dialog[open]")'));assert.deepEqual((await bounds()).native,g.native,'Identify result chrome does not move Preview');}
 passed('Identify request/result refresh is nonmutating, no identitypromotion/newerror popup or Preview displacement');
 await load('normal');const beforeDeploy=await snapshot(),deployIds=await identities(),deployGeometry=await bounds();assert.equal((await preview()).presentation,'preview-only');await click(control('Deploy'));assert.equal((await preview()).presentation,'deployed');assert.equal((await preview()).retained,'true');assert.deepEqual(await identities(),deployIds);assert.deepEqual((await snapshot()).data,beforeDeploy.data);assert.deepEqual((await bounds()).native,deployGeometry.native);await click(control('Undeploy'));assert.equal((await preview()).presentation,'preview-only');
 await load('deploy-empty');assert.equal((await rail()).runtimeConfigured,'false');assert(!await evaluate(q(control('Deploy'))+'.disabled'));await click(control('Deploy'));assert.equal((await preview()).presentation,'deployed');assert.equal((await bounds()).available,'true');
 passed('Preview-only/Deploy/Undeploy are independent of playback, selection, dirty and editor Preview; no-media Deploy valid');
 await load('deployed');const retainedId=(await preview()).boundId,retainedCanvas=await canvas(),retainedRev=await rev();await displayFact('disappear');assert.equal((await preview()).presentation,'cleanup-required');assert.equal((await preview()).retained,'true');assert(!await evaluate(q(control('Undeploy'))+'.disabled'));assert.equal((await preview()).boundId,retainedId);assert.equal(await canvas(),retainedCanvas);assert.equal(await rev(),retainedRev);await displayFact('return');assert.equal((await preview()).presentation,'deployed');assert.equal((await preview()).boundId,retainedId);
 passed('Deployed exactDisplay loss retains desire/Undeploy, return reconciles samebinding without fallback');
 await load('deployed');await displayFact('enumeration-failure');assert.equal((await preview()).displayAvailability,'enumeration-failed');assert.equal((await preview()).retained,'true');await click(control('Undeploy'));assert.equal((await preview()).retained,'false');
 passed('Enumeration failure while retained still permits ownership cleanup');
 await load('deploy-failure');const failureRev=await rev();await click(control('Deploy'));assert.equal((await preview()).lastDeploymentResult,'failure');assert.equal((await preview()).retained,'false');assert(await evaluate('!!document.querySelector("[aria-label=Deploy]")'));assert.equal(await rev(),failureRev);assert(!await evaluate('!!document.querySelector("dialog[open]")'));
 await load('deploy-partial-failure');await click(control('Deploy'));assert.equal((await preview()).lastDeploymentResult,'failure');assert.equal((await preview()).retained,'true');assert.equal((await preview()).hostOwned,'true');assert.equal((await preview()).targetRegistered,'false');assert(!await evaluate(q(control('Undeploy'))+'.disabled'));
 passed('Initial failure can remain Preview-only or retain partialhost; label follows ownership without invented toast');
 await load('undeploy-failure');await click(control('Undeploy'));assert.equal((await preview()).desired,'false');assert.equal((await preview()).retained,'true');assert.equal((await preview()).lastDeploymentResult,'failure');assert(!await evaluate(q(control('Undeploy'))+'.disabled'));await displayFact('allow-cleanup');await click(control('Undeploy'));assert.equal((await preview()).retained,'false');
 passed('Cleanup failure never fakes success and retains Undeploy retry eligibility');
 await load('highlight-off');await openView();const off=await bounds(),highlightRev=await rev(),contentBefore=await evaluate('document.querySelector(".preview-native-surface").outerHTML');
 assert.equal(await evaluate('getComputedStyle(document.querySelector(".preview-chrome-frame")).backgroundColor'),'rgba(0, 0, 0, 0)');
 assert.equal(await evaluate('getComputedStyle(document.querySelector(".preview-chrome-frame"),"::after").content'),'none');
 await click(control('Highlight Canvas Boundary'));assert.equal((await preview()).highlight,'true');assert.deepEqual((await bounds()).native,off.native);assert.deepEqual((await bounds()).frame,off.frame);
 assert.equal(await evaluate('getComputedStyle(document.querySelector(".preview-chrome-frame")).backgroundColor'),'rgba(0, 0, 0, 0)');
 assert.deepEqual(await evaluate('(()=>{const s=getComputedStyle(document.querySelector(".preview-chrome-frame"),"::after");return [s.outlineWidth,s.outlineColor,s.outlineOffset,s.pointerEvents]})()'),['2px','rgb(210, 115, 104)','0px','none']);
 assert.equal(await evaluate('document.querySelector(".preview-native-surface").outerHTML'),contentBefore);assert.equal(await rev(),highlightRev);await click(control('Highlight Canvas Boundary'));assert.equal((await preview()).highlight,'false');
 assert.equal(await evaluate('getComputedStyle(document.querySelector(".preview-chrome-frame"),"::after").content'),'none');
 passed('Highlightchecked/ringpaint changes without native/frame geometry, content, deployed state or authored dirty');
 await load('background-cover');await openView();const bgNative=await bounds(),bgRev=await rev();await click(control('Reveal Wallpaper'));assert.equal((await preview()).background,'reveal');assert.equal((await preview()).backgroundPersisted,'reveal');assert(!await evaluate(q(control('Cover Entire'))+'.checked'));assert(await evaluate(q(control('Reveal Wallpaper'))+'.checked'));assert.deepEqual((await bounds()).native,bgNative.native);assert.equal(await rev(),bgRev);assert.equal((await preview()).presentation,'preview-only');
 await load('display-secondary');await openView();assert(await evaluate(q(control('Reveal Wallpaper'))+'.checked'));await click(control('Cover Entire'));assert.equal((await preview()).boundId,'mock-persistent-display-b');
 passed('Backgroundradio modes mutuallyexclusive, perDisplay, enabled Preview-only and independent from editor Preview/dirty');
 await load('background-save-failure');await openView();const failBg=await rev();await click(control('Reveal Wallpaper'));assert.equal((await preview()).background,'reveal');assert.equal((await preview()).backgroundPersisted,'missing');assert.equal((await preview()).backgroundWrites,'1');assert(await evaluate('document.querySelector("dialog[open] .session-message").textContent.includes("This session keeps the selected mode")'));assert.equal(await evaluate('document.querySelector(".window-control--hide").disabled'),true);await click('[data-message-ok]');await click(control('Reveal Wallpaper'));assert.equal((await preview()).backgroundWrites,'1');assert(!await evaluate('!!document.querySelector("dialog[open]")'));assert.equal(await rev(),failBg);
 passed('Preference save failure keepslive/oldpersisted, explicitwarningack and same-modeno redundantwrite/warning');
 await load('background-reveal');await displayFact('external-background');assert.equal((await preview()).background,'reveal');assert.equal((await preview()).backgroundPersisted,'cover');
 for(const command of ['save','save-as','new','open']){if(command==='save-as'||command==='open')continue;await projectCommand(command);assert.equal((await preview()).background,'reveal');}
 passed('External writes do not live-reload; Save/New preserve live mode (full document lifecycle covered by modelchecks)');
 await load('normal');await openView();await evaluate(q(control('Highlight Canvas Boundary'))+'.focus()');await key(' ','Space',32);assert.equal((await preview()).highlight,'true');await key('Escape','Escape',27);assert(!await evaluate('!!document.querySelector(".preview-view-options")'));assert.equal(await evaluate('document.activeElement.getAttribute("aria-label")'),'Preview View preferences');await openView();await evaluate(q(control('Cover Entire'))+'.focus()');await key('ArrowRight','ArrowRight',39);assert.equal((await preview()).background,'reveal');
 passed('View checkbox/radio keyboard and Escape/focus restoration work without new shortcuts');
 for(const [width,height]of [[1800,1000],[1280,800],[1000,760],[680,700],[440,680],[1280,560]]){await load('display-long-name');await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});await rest();const g=await bounds();assert(g.native.width>0&&g.native.height>0);assert(g.native.y>=g.header.bottom);assert(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'));assert(await evaluate('[...document.querySelectorAll(".preview-actions button")].every(e=>{const r=e.getBoundingClientRect();return r.x>=0&&r.right<=innerWidth+1})'));await openView();const v=await bounds();assert(await evaluate('document.querySelector(".preview-view-options").getBoundingClientRect().bottom<=document.querySelector(".preview-viewport").getBoundingClientRect().top+.1'));assert.equal(await evaluate('document.querySelectorAll(".preview-native-surface button,.preview-native-surface input,.preview-native-surface [title]").length'),0);}
 passed('Six viewport sizes/longmetadata keep controls outside clean native rectangle; View DOM region never overlays it');
 assert.equal(runtimeErrors.length,0);assert.equal(warnings.length,0);
 await fs.writeFile(new URL('presentation-ui-green.json',evidence),JSON.stringify({pass:true,checks,runtimeErrors,warnings},null,2));console.log(JSON.stringify({pass:true,checks:checks.length,runtimeErrors:runtimeErrors.length,warnings:warnings.length}));
}catch(error){await fs.writeFile(new URL('presentation-ui-failed.json',evidence),JSON.stringify({pass:false,error:String(error),checks,runtimeErrors,warnings},null,2));throw error;}finally{socket.close();}
