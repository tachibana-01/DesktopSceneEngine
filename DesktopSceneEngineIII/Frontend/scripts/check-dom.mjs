import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
// Vite + an isolated headless Chrome on loopback:9227 are the QA prerequisites.
await fs.mkdir(new URL('../node_modules/.cache/launcher-v12/', import.meta.url), { recursive: true });
const tabs = await (await fetch('http://127.0.0.1:9227/json/list')).json();
const tab = tabs.find(item => item.type === 'page');
assert(tab, 'Headless page exists');
const socket = new WebSocket(tab.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
let serial = 0;
const pending = new Map();
const exceptions = [];
const browserWarnings = [];
socket.addEventListener('message', async event => {
  const payload = JSON.parse(typeof event.data === 'string' ? event.data : await event.data.text());
  if (payload.method === 'Runtime.exceptionThrown') exceptions.push(payload.params.exceptionDetails);
  if (payload.method === 'Log.entryAdded' && ['warning', 'error'].includes(payload.params.entry.level)) browserWarnings.push(payload.params.entry);
  if (payload.method === 'Runtime.consoleAPICalled' && payload.params.type === 'error') browserWarnings.push(payload.params);
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
async function settle() {
  await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
}

const results = [];
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function rest() {
  await settle();
  await evaluate("Promise.allSettled(document.getAnimations().map(animation => animation.finished))");
  await settle();
}
async function click(selector) {
  assert(await evaluate("!!document.querySelector(" + JSON.stringify(selector) + ")"), 'Control exists: ' + selector);
  await evaluate("document.querySelector(" + JSON.stringify(selector) + ").click()");
  await rest();
}
async function load(count = 5) {
  await send('Page.navigate', { url: 'http://127.0.0.1:5173/?displays=' + count });
  for (let i=0; i<80; i++) {
    if (await evaluate("document.querySelector('.display-deck')?.dataset.displayCount === '" + count + "'")) break;
    await pause(100);
  }
  assert.equal(await evaluate("document.querySelector('.display-deck')?.dataset.displayCount"), String(count));
  await rest();
}
async function geometry() {
  return evaluate("(() => { const rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom,cx:r.x+r.width/2};}; const viewport=document.querySelector('.deck-viewport'); const selected=document.querySelector('[data-selected=\"true\"]'); const screens=[...document.querySelectorAll('.display-surface')];const action=document.querySelector('.editorial-action');return {width:innerWidth,scrollWidth:document.documentElement.scrollWidth,position:Number(document.querySelector('.display-deck').dataset.position),viewport:rect(viewport),selected:rect(selected),selectedName:selected.getAttribute('aria-label'),selectedScale:new DOMMatrix(getComputedStyle(selected).transform).a,selectedCount:document.querySelectorAll('[data-selected=\"true\"]').length,mask:getComputedStyle(viewport).maskImage,overflow:getComputedStyle(viewport).overflowX,action:action?rect(action):null,status:document.querySelector('.detail-heading .display-status').textContent.trim(),screens:screens.map(e=>({name:e.getAttribute('aria-label'),rect:rect(e),hidden:e.parentElement.getAttribute('aria-hidden')==='true',pressed:e.getAttribute('aria-pressed'),tabIndex:e.tabIndex,opacity:getComputedStyle(e).opacity})),names:[...document.querySelectorAll('button')].filter(e=>!e.closest('[aria-hidden=\"true\"]')).map(e=>e.getAttribute('aria-label')||e.textContent.trim())};})()");
}
const aspects = { 'Select DISPLAY A': 2560/1440, 'Select DISPLAY B': 1920/1080, 'Select DISPLAY C': 1920/1200,
  'Select DISPLAY D': 3440/1440, 'Select DISPLAY E': 1080/1920 };
function checkGeometry(g, count, partial = false) {
  assert(g.scrollWidth <= g.width + 1, 'No horizontal document overflow');
  assert.equal(g.selectedCount, 1, 'Exactly one focused visual instance');
  assert(Math.abs(g.selected.cx - g.viewport.cx) < 1, 'Selected display centered');
  assert(Math.abs(g.selectedScale - 1.2) < .01, 'Selected is 1.2 times its normal size');
  if(g.action) assert(g.action.height >= 44 && g.action.x >= 0 && g.action.right <= g.width, 'Primary action hit area fits');
  else assert(['UNAVAILABLE','SESSION DISCONNECTED'].includes(g.status), 'Only unavailable/disconnected omit the primary action');
  assert(g.names.every(Boolean), 'All exposed buttons have accessible names');
  const accessible = g.screens.filter(screen=>!screen.hidden);
  assert.equal(accessible.length, count, 'Exactly one accessible instance of each display');
  assert.equal(new Set(accessible.map(screen=>screen.name)).size, count, 'No duplicate accessibility labels');
  assert.equal(accessible.filter(screen=>screen.tabIndex===0).length, 1, 'Roving slide tab stop');
  for (const screen of g.screens) {
    assert(Math.abs(screen.rect.width/screen.rect.height - aspects[screen.name]) < .02, 'Fixture aspect preserved');
    assert.equal(screen.opacity, '1', 'Slides never fade out/in');
    if (screen.hidden) assert.equal(screen.tabIndex, -1, 'Hidden clones are not tab stops');
  }
  const visible = g.screens.filter(screen=>screen.rect.right > g.viewport.x && screen.rect.x < g.viewport.right)
    .sort((a,b)=>a.rect.x-b.rect.x);
  for (let i=1;i<visible.length;i++) assert(visible[i-1].rect.right < visible[i].rect.x, 'Neighbor screens do not overlap');
  assert(g.mask.includes('linear-gradient'), 'CSS gradient mask exists');
  assert.equal(g.overflow, 'clip', 'Deck clips without focus-scroll drift');
  if (partial) assert(visible.some(screen=>screen.rect.x<g.viewport.x || screen.rect.right>g.viewport.right), 'Partially visible edge display');
}
async function motion(selector, direction) {
  const sample = await evaluate("new Promise(resolve=>{const track=document.querySelector('.deck-track');const samples=[new DOMMatrix(getComputedStyle(track).transform).m41];const names=[];const observer=new MutationObserver(()=>names.push(document.querySelector('#detail-title').textContent));observer.observe(document.querySelector('.display-details'),{subtree:true,childList:true,characterData:true});document.querySelector("+JSON.stringify(selector)+").click();const started=performance.now();function frame(time){samples.push(new DOMMatrix(getComputedStyle(track).transform).m41);if(time-started<300)requestAnimationFrame(frame);else{observer.disconnect();resolve({samples,names:[...new Set(names)],position:Number(document.querySelector('.display-deck').dataset.position)});}}requestAnimationFrame(frame);})");
  const xs=sample.samples, first=xs[0], last=xs.at(-1);
  assert(Math.sign(last-first)===direction, 'Motion follows requested direction');
  assert(xs.some(x=>Math.abs(x-first)>1 && Math.abs(x-last)>1), 'Motion has intermediate frames');
  for(let i=1;i<xs.length;i++) assert(direction*(xs[i]-xs[i-1])>=-.6, 'Motion never rewinds');
  await rest();
  return {...sample, distance:Math.abs(last-first)};
}
try {
  await send('Runtime.enable'); await send('Page.enable'); await send('Log.enable');
  await send('Log.clear'); browserWarnings.length = 0;
  await send('Page.bringToFront');
  await send('Emulation.setEmulatedMedia',{features:[]});
  await send('Emulation.setDeviceMetricsOverride', {width:1280,height:900,deviceScaleFactor:1,mobile:false});
  await load();
  assert.equal(await evaluate("document.querySelector('h1').textContent"), 'Displays');
  assert.equal(await evaluate("document.querySelector('.page-identity p').textContent"), '5 connected');
  assert.equal(await evaluate("document.querySelector('#detail-title').textContent"), 'DISPLAY A');
  assert.equal(await evaluate("document.querySelector('.session-context').textContent"),'Session ready');
  assert.equal(await evaluate("document.querySelector('.editorial-action').textContent.trim()"), 'OPEN SESSION');
  await click(".display-slide:not([aria-hidden]) [data-display-id='display-b']");
  assert.equal(await evaluate("document.querySelector('#detail-title').textContent"), 'DISPLAY B');
  assert.equal(await evaluate("document.querySelector('.session-context').textContent"), 'No active session');
  assert.equal(await evaluate("document.querySelector('.editorial-action').textContent.trim()"), 'CREATE SESSION');
  assert.equal(await evaluate("document.querySelector('.detail-heading .display-status').textContent.trim()"), 'AVAILABLE');
  await click('.editorial-action');
  assert(await evaluate("document.querySelector('.launcher-operation').textContent.includes('Starting session…')"));
  assert.equal(await evaluate("document.querySelector('.editorial-action').textContent.trim()"), 'STARTING…');
  assert(await evaluate("document.querySelector('.editorial-action').disabled"));
  checkGeometry(await geometry(),5,true);
  await click("[aria-label='Go to DISPLAY A']");
  await click('.editorial-action');
  assert(await evaluate("document.querySelector('.launcher-operation').textContent.includes('Session activated.')"));
  results.push({check:'Direct display selection, A/B detail/action/lifecycle sync and mock-only feedback',pass:true});

  const jump=await motion("[aria-label='Go to DISPLAY D']",-1);
  assert.equal(jump.position,3);
  assert.deepEqual(jump.names,['DISPLAY D'],'Indicator jump never publishes intermediate display contexts');
  assert(jump.distance>600,'Direct indicator traverses multiple slides in one transition');
  await click("[aria-label='Go to DISPLAY E']");
  const forward=await motion("[aria-label='Next display']",-1);
  assert.equal(forward.position,5);
  assert.equal(await evaluate("document.querySelector('#detail-title').textContent"),'DISPLAY A');
  assert(forward.distance<400,'Last-to-first advances one local step, not an entire deck');
  const reverse=await motion("[aria-label='Previous display']",1);
  assert.equal(reverse.position,4);
  assert.equal(await evaluate("document.querySelector('#detail-title').textContent"),'DISPLAY E');
  assert(Math.abs(reverse.distance-forward.distance)<1);
  results.push({check:'Direct indicator jump + continuous forward/reverse wrap',pass:true,jump,forward,reverse});

  await evaluate("for(let i=0;i<13;i++) document.querySelector('[aria-label=\"Next display\"]').click()");
  await rest();
  assert.equal(await evaluate("Number(document.querySelector('.display-deck').dataset.position)"),17);
  assert.equal(await evaluate("document.querySelector('#detail-title').textContent"),'DISPLAY C');
  checkGeometry(await geometry(),5,true);
  results.push({check:'Rapid navigation retains exact latest position/context',pass:true});

  for (const label of ['Hide to tray','Minimize','Maximize','Restore','Close']) {
    await click('[aria-label='+JSON.stringify(label)+']');
    assert(await evaluate("document.querySelector('.mock-feedback').textContent.includes("+JSON.stringify(label+' — visual prototype only')+")"));
    assert.equal(await evaluate("document.querySelector('h1').textContent"),'Displays');
  }
  await evaluate("document.querySelector('[aria-label=\"Hide to tray\"]').focus()");
  await send('Page.bringToFront');
  await send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
  await send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
  assert(await evaluate("document.activeElement.getAttribute('aria-label')==='Minimize'&&getComputedStyle(document.activeElement).outlineStyle!=='none'"));
  results.push({check:'Accepted shell controls/mock feedback/keyboard focus preserved',pass:true});

  for (const [name,width,height] of [['wide',1600,900],['normal',1000,760],['narrow',560,700],['compact-desktop',440,680]]) {
    await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
    await rest();
    for(const target of ['A','D','E']) {
      await click("[aria-label='Go to DISPLAY "+target+"']");
      const g=await geometry(); checkGeometry(g,5,true);
      results.push({check:name+' centered '+target,pass:true,viewport:g.viewport,selected:g.selected,action:g.action});
    }
  }
  await send('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});
  for(const count of [1,2,3,5]) {
    await load(count);
    checkGeometry(await geometry(),count,count>1);
    assert.equal(await evaluate("document.querySelectorAll('.deck-indicator').length"),count===1?0:count);
    assert.equal(await evaluate("document.querySelectorAll('.deck-arrow').length"),count===1?0:2);
    if(count>1) {
      await click("[aria-label='Previous display']");
      assert.equal(await evaluate("document.querySelector('#detail-title').textContent"),'DISPLAY '+String.fromCharCode(64+count));
      checkGeometry(await geometry(),count);
      await click("[aria-label='Next display']");
      assert.equal(await evaluate("document.querySelector('#detail-title').textContent"),'DISPLAY A');
      await evaluate("document.querySelector('[data-selected=\"true\"]').focus()");
      await send('Input.dispatchKeyEvent',{type:'keyDown',key:'ArrowRight',code:'ArrowRight',windowsVirtualKeyCode:39});
      await send('Input.dispatchKeyEvent',{type:'keyUp',key:'ArrowRight',code:'ArrowRight',windowsVirtualKeyCode:39});
      await rest();
      assert.equal(await evaluate("document.querySelector('#detail-title').textContent"),'DISPLAY B');
      assert(await evaluate("document.activeElement.dataset.selected==='true'&&getComputedStyle(document.activeElement).outlineStyle!=='none'"));
      await send('Input.dispatchKeyEvent',{type:'keyDown',key:'ArrowLeft',code:'ArrowLeft',windowsVirtualKeyCode:37});
      await send('Input.dispatchKeyEvent',{type:'keyUp',key:'ArrowLeft',code:'ArrowLeft',windowsVirtualKeyCode:37});
      await rest();
      assert.equal(await evaluate("document.querySelector('#detail-title').textContent"),'DISPLAY A');
      checkGeometry(await geometry(),count);
    }
    results.push({check:count+' display fixture, cyclic navigation and keyboard/focus',pass:true});
  }
  await send('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'reduce'}]});
  await click("[aria-label='Next display']");
  assert.equal(await evaluate("getComputedStyle(document.querySelector('.deck-track')).transitionDuration"),'0s');
  checkGeometry(await geometry(),5);
  await pause(2500);
  assert.equal(await evaluate("document.querySelector('.mock-feedback').textContent"),'');
  results.push({check:'Reduced motion and transient feedback cleanup',pass:true});
  assert.equal(exceptions.length,0,'No uncaught React/runtime errors');
  assert(!browserWarnings.some(entry=>/aria-hidden|retained focus|blocked/i.test(JSON.stringify(entry))),'No hidden-focus accessibility warnings');
  const output={pass:true,checks:results,runtimeErrors:exceptions,browserWarnings,visualReview:'PENDING USER; DOM/motion geometry only, no screenshots or user-desktop automation'};
  await fs.writeFile(new URL('../node_modules/.cache/launcher-v12/deck-checks.json',import.meta.url),JSON.stringify(output,null,2));
  console.log(JSON.stringify({pass:true,checks:results.length,runtimeErrors:exceptions.length,browserWarnings,motion:{jump:jump.distance,forward:forward.distance,reverse:reverse.distance}},null,2));
} catch (error) {
  await fs.writeFile(new URL('../node_modules/.cache/launcher-v12/deck-checks-failed.json',import.meta.url),
    JSON.stringify({pass:false,error:String(error),checks:results,browserWarnings,runtimeErrors:exceptions},null,2));
  console.error(JSON.stringify({error:String(error),browserWarnings},null,2));
  throw error;
} finally {
  socket.close();
}
