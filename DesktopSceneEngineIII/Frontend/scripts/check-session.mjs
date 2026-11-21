import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// Same isolated loopback QA browser as check:dom; never a user's browser profile.
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
const pending = new Map();
const runtimeErrors = [], browserWarnings = [], checks = [];
socket.addEventListener('message', event => {
  const payload = JSON.parse(event.data);
  if (payload.method === 'Runtime.exceptionThrown') runtimeErrors.push(payload.params.exceptionDetails);
  if (payload.method === 'Log.entryAdded' && ['warning', 'error'].includes(payload.params.entry.level)) browserWarnings.push(payload.params.entry);
  if (payload.method === 'Runtime.consoleAPICalled' && ['warning', 'error'].includes(payload.params.type)) browserWarnings.push(payload.params);
  const request = pending.get(payload.id);
  if (request) {
    pending.delete(payload.id);
    payload.error ? request.reject(payload.error) : request.resolve(payload.result);
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
  const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
async function rest() {
  await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
  await evaluate('Promise.allSettled(document.getAnimations().map(animation => animation.finished))');
}
async function navigate(query, expected) {
  await send('Page.navigate', { url: 'http://127.0.0.1:5173/' + query });
  await evaluate('document.readyState === "complete" ? Promise.resolve() : new Promise(resolve => addEventListener("load", resolve, {once:true}))');
  await rest();
  assert(await evaluate('!!document.querySelector(' + JSON.stringify(expected) + ')'), 'Expected view renders: ' + expected);
}
async function click(selector) {
  assert(await evaluate('!!document.querySelector(' + JSON.stringify(selector) + ')'), 'Control exists: ' + selector);
  await evaluate('document.querySelector(' + JSON.stringify(selector) + ').click()');
  await rest();
}
async function key(key, code, virtualKey, modifiers = 0) {
  await send('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:virtualKey,modifiers,...(key === 'Enter' ? {text:'\r'} : {})});
  await send('Input.dispatchKeyEvent',{type:'keyUp',key,code,windowsVirtualKeyCode:virtualKey,modifiers});
  await rest();
}
async function geometry() {
  return evaluate(`(() => {
    const rect = selector => {const e=document.querySelector(selector),r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom,display:getComputedStyle(e).display};};
    const preview=document.querySelector('.preview-placeholder'), style=getComputedStyle(preview);
    return {width:innerWidth,height:innerHeight,scrollWidth:document.documentElement.scrollWidth,scrollHeight:document.documentElement.scrollHeight,
      title:rect('.session-shell .title-bar'),grid:rect('.session-workspace'),left:rect('.session-library'),center:rect('.preview-workspace'),
      preview:rect('.preview-placeholder'),inspector:rect('.session-inspector'),rail:rect('.playback-rail'),
      radius:style.borderRadius,clip:style.clipPath,mask:style.maskImage,shadow:style.boxShadow,
      overlayControls:preview.querySelectorAll('button,input,select,a').length,
      fieldRects:[...document.querySelectorAll('.identity-property:not(.identity-property--source) .property-value')].filter(e=>e.getBoundingClientRect().width).map(e=>{const r=e.getBoundingClientRect();return {right:r.right,height:r.height};}),
      unnamed:[...document.querySelectorAll('button,input')].filter(e=>e.getBoundingClientRect().width && !(e.getAttribute('aria-label')||e.textContent.trim())).length};
  })()`);
}
try {
  await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable');
  await send('Log.clear'); browserWarnings.length = 0;
  await send('Page.bringToFront');
  await send('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'no-preference'}]});
  await send('Emulation.setDeviceMetricsOverride', {width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await navigate('?view=session', '.session-shell');
  assert.equal(await evaluate('document.querySelectorAll(".playlist-entity").length'),0,'Production startup is an empty Project');
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'),'none');
  checks.push({check:'Default Session startup is legal empty Project, No Selection',pass:true});
  await navigate('?view=session&sessionScenario=normal', '.session-shell');
  assert.equal(await evaluate('document.querySelectorAll(".title-bar").length'),1,'One integrated top shell');
  assert.equal(await evaluate('document.querySelectorAll(".playlist-entity").length'),4);
  assert.equal(await evaluate('document.querySelectorAll(".item-entity").length'),3);
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'),'item');
  checks.push({check:'Loaded fixture: one integrated shell, simultaneous Playlists/Items and identity Inspector',pass:true});
  await evaluate('document.querySelector(".playlist-entity[data-playlist-id=playlist-b] .entity-select").focus()');
  await key('Enter','Enter',13);
  assert.equal(await evaluate('document.querySelector(".session-shell").dataset.selectedPlaylistId'),'playlist-b');
  assert.equal(await evaluate('document.querySelector(".session-shell").dataset.activePlaylistId'),'playlist-a');
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'),'playlist');
  assert(await evaluate('getComputedStyle(document.activeElement).outlineStyle!=="none"'));
  await key('F10','F10',121,8);
  assert(await evaluate('!!document.querySelector("dialog[open] [role=menu]")'),'Shift F10 opens selected entity commands');
  await key('End','End',35);
  assert.equal(await evaluate('document.activeElement.dataset.command'),'delete');
  await key('ArrowDown','ArrowDown',40);
  assert.equal(await evaluate('document.activeElement.dataset.command'),'play');
  await key('Escape','Escape',27);
  assert(await evaluate('document.activeElement.closest(".playlist-entity").dataset.playlistId==="playlist-b"'),'Escape restores entity focus');
  await key('Tab','Tab',9);
  // UI-PIC-001 inserts the persistent speaker immediately before the existing menu.
  assert.equal(await evaluate('document.activeElement.className'),'playlist-volume-trigger');
  assert.equal(await evaluate('document.activeElement.closest(".playlist-entity").dataset.playlistId'),'playlist-b');
  await key('Tab','Tab',9);
  assert.equal(await evaluate('document.activeElement.className'),'entity-more');
  assert.equal(await evaluate('getComputedStyle(document.activeElement).opacity'),'1','Focused overflow command remains visible');
  checks.push({check:'Keyboard selection is editing only; visible focus, Shift F10, menu arrow wrap and Escape restoration',pass:true});
  await click('.item-entity[data-item-id=b-image] .entity-select');
  assert.equal(await evaluate('document.querySelector(".session-inspector").dataset.inspectorContext'),'item');
  for(const label of ['Hide to tray','Minimize','Maximize','Restore']) {
    await click('[aria-label='+JSON.stringify(label)+']');
    assert(await evaluate('document.querySelector(".session-feedback").textContent.includes("visual prototype only")'));
    assert.equal(await evaluate('document.querySelector(".session-shell").dataset.sessionClosed'),'false');
  }
  await evaluate(`document.querySelector('[aria-label="Hide to tray"]').focus()`);
  await key('Tab','Tab',9);
  assert(await evaluate('document.activeElement.getAttribute("aria-label")==="Minimize" && getComputedStyle(document.activeElement).outlineStyle!=="none"'));
  checks.push({check:'Mock caption controls preserved; caption keyboard focus visible; Close checked in document suite',pass:true});
  let previousCenterWidth;
  for(const [name,width,height,leftVisible,rightVisible] of [
    ['wide',1800,1000,true,true],['normal',1280,800,true,true],['narrow',1000,760,false,true],
    ['compact',680,700,false,false],['small-desktop',440,680,false,false],['short',1280,560,true,true],
  ]) {
    await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false}); await rest();
    const g=await geometry();
    assert(g.scrollWidth<=width+1,'No horizontal page overflow: '+name);
    assert(g.scrollHeight<=height+1,'Shell fits the window with internal panel scroll: '+name);
    assert.equal(g.left.display!=='none',leftVisible);
    assert.equal(g.inspector.display!=='none',rightVisible);
    assert(g.center.width>=Math.min(400,width-2),'Preview retains usable width: '+name);
    assert(g.preview.width>=Math.min(320,width-64),'Preview surface retains useful width: '+name);
    assert(Math.abs(g.preview.width/g.preview.height-16/9)<.02,'Rectangular 16:9 placeholder');
    assert(g.preview.x>=g.center.x && g.preview.right<=g.center.right+1 && g.preview.bottom<=g.rail.y,'Preview stays in its workspace');
    assert(Math.abs(g.rail.x-g.center.x)<=1 && Math.abs(g.rail.right-g.center.right)<=1 && g.rail.height>=48 && g.rail.height<=64,'Rail follows its Preview workspace');
    assert(Math.abs(g.rail.bottom-g.grid.bottom)<=1,'Preview transport reaches workspace bottom');
    if(leftVisible) assert(Math.abs(g.left.bottom-g.grid.bottom)<=1,'Library extends to workspace bottom');
    if(rightVisible) assert(Math.abs(g.inspector.bottom-g.grid.bottom)<=1,'Inspector extends to workspace bottom');
    assert(g.title.height<=64 && g.grid.y>=g.title.bottom-1,'Single title shell above workspace');
    assert.equal(g.radius,'0px');assert.equal(g.clip,'none');assert.equal(g.mask,'none');assert.equal(g.shadow,'none');assert.equal(g.overlayControls,0);
    assert.equal(g.unnamed,0,'Controls have meaningful accessible names');
    if(rightVisible) {
      assert(g.fieldRects.every(r=>Math.abs(r.right-g.fieldRects[0].right)<1),'Inspector value column aligns');
      assert(g.fieldRects.every(r=>r.height===32),'Inspector control row height is consistent');
    }
    if(name==='wide') previousCenterWidth=g.center.width;
    if(name==='normal') assert(previousCenterWidth-g.center.width>400,'Most additional window width enters Preview');
    if(name==='narrow') await evaluate('document.querySelector(".inspector-context-heading button").focus()');
    if(name==='compact') {
      assert(await evaluate('!document.activeElement.closest(".session-inspector")'),'Collapsed panel cannot retain hidden keyboard focus');
      await key('Tab','Tab',9);
      assert(await evaluate('document.activeElement.getBoundingClientRect().width>0'),'Tab reaches a visible control after auto-collapse');
    }
    checks.push({check:name+' layout, unchanged Preview geometry and identity alignment',pass:true,geometry:g});
  }
  await send('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'reduce'}]});
  assert.equal(await evaluate('getComputedStyle(document.querySelector(".playback-button")).transitionDuration'),'0s');
  checks.push({check:'Reduced motion respected',pass:true});

  await navigate('?view=launcher','.launcher-shell');
  assert.equal(await evaluate('document.querySelector("h1").textContent'),'Displays');
  assert.equal(await evaluate('document.querySelectorAll(".deck-indicator").length'),5);
  await click('[aria-label="Go to DISPLAY D"]');
  assert.equal(await evaluate('document.querySelector("#detail-title").textContent'),'DISPLAY D');
  checks.push({check:'Explicit Launcher route remains reviewable and interactive',pass:true});
  await navigate('','.launcher-shell');
  checks.push({check:'Default entry preserves Launcher V1.2',pass:true});
  assert.equal(runtimeErrors.length,0,'No runtime errors');
  assert.equal(browserWarnings.length,0,'No browser warnings/errors');
  await fs.writeFile(new URL('dom-checks.json',evidence),JSON.stringify({pass:true,checks,runtimeErrors,browserWarnings,visualReview:'PENDING HUMAN; DOM/state/geometry checks only'},null,2));
  console.log(JSON.stringify({pass:true,checks:checks.length,runtimeErrors:runtimeErrors.length,browserWarnings:browserWarnings.length}));
} catch(error) {
  await fs.writeFile(new URL('dom-checks-failed.json',evidence),JSON.stringify({pass:false,error:String(error),checks,runtimeErrors,browserWarnings},null,2));
  throw error;
} finally { socket.close(); }
