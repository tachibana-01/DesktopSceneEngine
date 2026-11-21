import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

// Same isolated loopback QA browser as check:dom; never a user's browser profile.
const evidence = new URL('../node_modules/.cache/background-music-v1/', import.meta.url);
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
  if (payload.method === 'Runtime.consoleAPICalled' && payload.params.type === 'error') browserWarnings.push(payload.params);
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
  const url = 'http://127.0.0.1:5173/' + query;
  await send('Page.navigate', { url });
  const rendered = 'location.href===' + JSON.stringify(url) + '&&document.readyState==="complete"&&!!document.querySelector(' + JSON.stringify(expected) + ')';
  let ready = false;
  for (let attempt = 0; attempt < 80 && !ready; attempt++) {
    ready = await evaluate(rendered);
    if (!ready) await new Promise(resolve => setTimeout(resolve, 50));
  }
  assert(ready, 'Expected view renders: ' + expected);
  await rest();
}
async function click(selector) {
  assert(await evaluate('!!document.querySelector(' + JSON.stringify(selector) + ')'), 'Control exists: ' + selector);
  await evaluate('document.querySelector(' + JSON.stringify(selector) + ').click()');
  await rest();
}
async function input(label, value) {
  await evaluate(`(() => {
    const input=document.querySelector('input[aria-label=${JSON.stringify(label)}]');
    if(!input) throw new Error('Input not found: '+${JSON.stringify(label)});
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,${JSON.stringify(String(value))});
    input.dispatchEvent(new Event('input',{bubbles:true}));
    input.dispatchEvent(new Event('change',{bubbles:true}));
  })()`);
  await rest();
}
async function key(key, code, virtualKey) {
  await send('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:virtualKey});
  await send('Input.dispatchKeyEvent',{type:'keyUp',key,code,windowsVirtualKeyCode:virtualKey});
  await rest();
}

const q = selector => 'document.querySelector(' + JSON.stringify(selector) + ')';
const scope = name => '[data-scope="' + name + '"]';
const row = (name, id) => scope(name) + ' .bgm-track-row[data-track-id="' + id + '"]';
const currentName = name => evaluate(q(scope(name) + ' .bgm-current-name') + '.textContent');
const count = name => evaluate('document.querySelectorAll(' + JSON.stringify(scope(name) + ' .bgm-track-row') + ').length');
const volume = name => evaluate(q(scope(name) + ' input[type=range]') + '.value');
const apply = name => click(scope(name) + ' .bgm-use');
const remove = name => click(scope(name) + ' .bgm-remove');
const add = name => click(scope(name) + ' .bgm-add');
async function project(value) {
  await evaluate(`(() => {const selector=${q('#trial-project')}; selector.value=${JSON.stringify(value)}; selector.dispatchEvent(new Event('change',{bubbles:true}));})()`);
  await rest();
}
async function snapshot(name) {
  return evaluate(`(() => {const column=${q(scope(name))}; return {current:column.querySelector('.bgm-current-name').textContent,
    volume:column.querySelector('input').value,rows:[...column.querySelectorAll('.bgm-track-row')].map(row=>({id:row.dataset.trackId,selected:row.getAttribute('aria-pressed'),current:row.getAttribute('aria-current')}))};})()`);
}
async function layout() {
  return evaluate(`(() => {
    const rect=element=>{const r=element.getBoundingClientRect();return {x:r.x,y:r.y,right:r.right,bottom:r.bottom,width:r.width,height:r.height};};
    return {width:innerWidth,scrollWidth:document.documentElement.scrollWidth,scrollHeight:document.documentElement.scrollHeight,height:innerHeight,
      scopes:[...document.querySelectorAll('.bgm-scope')].map(column=>({rect:rect(column),header:rect(column.querySelector('.bgm-scope-header')),
        current:rect(column.querySelector('.bgm-current')),volume:rect(column.querySelector('.bgm-volume')),list:rect(column.querySelector('.bgm-track-list')),
        selection:rect(column.querySelector('.bgm-selection')),actions:rect(column.querySelector('.bgm-actions')),add:rect(column.querySelector('.bgm-add')),
        scrollHeight:column.querySelector('.bgm-track-list').scrollHeight,clientHeight:column.querySelector('.bgm-track-list').clientHeight,
        opacity:getComputedStyle(column).opacity,overflow:getComputedStyle(column.querySelector('.bgm-track-list')).overflowY}))};
  })()`);
}
try {
  await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable'); await send('Log.clear'); browserWarnings.length=0;
  await send('Page.bringToFront');
  await send('Emulation.setEmulatedMedia',{features:[]});
  await send('Emulation.setDeviceMetricsOverride',{width:1200,height:760,deviceScaleFactor:1,mobile:false});
  await navigate('?view=background-music','.background-music-shell');
  assert.equal(await evaluate('document.querySelectorAll(".bgm-scope").length'),2);
  assert.equal(await evaluate('document.querySelectorAll(".title-bar").length'),1);
  assert.equal(await currentName('playlist'),'ambience-night.flac');
  assert.equal(await currentName('project'),'distant-piano.flac');
  assert.equal(await volume('playlist'),'72'); assert.equal(await volume('project'),'42');
  checks.push({check:'Music route, one shell, two symmetric scope regions and initial context',pass:true});

  await click(row('playlist','playlist-city'));
  assert.equal(await currentName('playlist'),'ambience-night.flac','Single click selects only');
  assert.equal(await evaluate(q(row('playlist','playlist-city'))+'.getAttribute("aria-pressed")'),'true');
  await evaluate(q(row('playlist','playlist-city'))+'.dispatchEvent(new MouseEvent("dblclick",{bubbles:true}))');
  assert.equal(await currentName('playlist'),'ambience-night.flac','Double click does not apply');
  await apply('playlist');
  assert.equal(await currentName('playlist'),'city-rain.mp3');
  assert.equal(await evaluate(q(row('playlist','playlist-city'))+'.getAttribute("aria-current")'),'true');
  assert.equal(await evaluate(q(scope('playlist')+' .bgm-use')+'.disabled'),true);
  assert.equal(await currentName('project'),'distant-piano.flac');
  checks.push({check:'Selected differs from Current; explicit Apply updates only its scope',pass:true});

  await input('Playlist music volume',15); await input('Project music volume',63);
  assert.equal(await volume('playlist'),'15');assert.equal(await volume('project'),'63');
  await evaluate(q(scope('playlist')+' input')+'.focus()');
  await key('End','End',35); assert.equal(await volume('playlist'),'100');
  await key('ArrowLeft','ArrowLeft',37); assert.equal(await volume('playlist'),'99');
  await key('Home','Home',36); assert.equal(await volume('playlist'),'0');
  assert.equal(await volume('project'),'63');
  assert.equal(await evaluate(q(scope('playlist')+' .bgm-volume output')+'.textContent'),'0%');
  checks.push({check:'Independent volumes, 0–100 boundaries and real keyboard arrows',pass:true});

  const beforeAdd=await count('playlist');
  await add('playlist'); assert.equal(await count('playlist'),beforeAdd+1);
  assert(await evaluate(q(scope('playlist')+' .bgm-track-row[aria-pressed=true]')+'.textContent.includes("New-Track-01.flac")'));
  assert.equal(await currentName('playlist'),'city-rain.mp3','Add selects but never applies');
  await remove('playlist'); assert.equal(await count('playlist'),beforeAdd);
  assert.equal(await currentName('playlist'),'city-rain.mp3','Removing non-current preserves assignment');
  await click(row('playlist','playlist-city')); await remove('playlist');
  assert.equal(await currentName('playlist'),'No background music');
  assert(!await evaluate('!!'+q(row('playlist','playlist-city'))));
  assert.equal(await currentName('project'),'distant-piano.flac');
  checks.push({check:'Add/select, Remove non-current, Remove current clears assignment deterministically',pass:true});

  const playlistSnapshot=await snapshot('playlist');
  await click(row('project','alpha-soft')); await apply('project'); await input('Project music volume',23); await add('project');
  const alphaSnapshot=await snapshot('project');
  await project('beta');
  assert.equal(await currentName('project'),'No background music');assert.equal(await volume('project'),'58');
  assert.equal(await evaluate('document.querySelectorAll('+JSON.stringify(scope('project')+' .bgm-track-row[aria-pressed=true]')+').length'),0,'First keyboard tab stop is not a selected track');
  assert.equal(await evaluate('document.querySelectorAll('+JSON.stringify(scope('project')+' .bgm-track-row[tabindex="0"]')+').length'),1);
  await click(row('project','beta-morning'));await apply('project');await input('Project music volume',65);await add('project');
  const betaSnapshot=await snapshot('project');
  await project('alpha');assert.deepEqual(await snapshot('project'),alphaSnapshot);
  await project('beta');assert.deepEqual(await snapshot('project'),betaSnapshot);
  assert.deepEqual(await snapshot('playlist'),playlistSnapshot);
  checks.push({check:'Alpha/Beta retain separate collection, assignment, selection and volume; Playlist untouched',pass:true});

  const availableLayout=await layout();
  await project('none');
  assert.equal(await evaluate(q(scope('project')+' .bgm-scope-context')+'.textContent'),'NO PROJECT SELECTED');
  assert(await evaluate('Array.from('+q(scope('project'))+'.querySelectorAll("button,input")).every(e=>e.disabled)'));
  const unavailableLayout=await layout();
  assert.deepEqual(unavailableLayout.scopes[1].rect,availableLayout.scopes[1].rect,'Unavailable project keeps geometry');
  assert.equal(unavailableLayout.scopes[1].opacity,'1');
  await add('playlist');assert.equal(await count('playlist'),playlistSnapshot.rows.length+1);
  await project('alpha');assert.deepEqual(await snapshot('project'),alphaSnapshot);
  checks.push({check:'No Project retains readable column, disables controls and leaves Playlist usable',pass:true});

  while(await count('project')) {
    await click(scope('project')+' .bgm-track-row'); await remove('project');
  }
  assert.equal(await currentName('project'),'No background music');
  assert(await evaluate(q(scope('project')+' .bgm-empty')+'.textContent.includes("No music added")'));
  assert.equal(await evaluate(q(scope('project')+' .bgm-remove')+'.disabled'),true);
  await add('project');assert.equal(await count('project'),1);
  assert.equal(await currentName('project'),'No background music');
  await apply('project');assert((await currentName('project')).startsWith('New-Track-'));
  checks.push({check:'Empty list, no selection, disabled actions and Add → Apply recovery',pass:true});

  await navigate('?view=background-music&tracks=long','.background-music-shell');
  assert(await count('playlist')>=24);assert(await count('project')>=24);
  const beforeScroll=await layout();
  assert(beforeScroll.scopes.every(s=>s.scrollHeight>s.clientHeight&&s.overflow==='auto'));
  await evaluate(q(scope('playlist')+' .bgm-track-list')+'.scrollTop=450');await rest();
  assert.equal(await evaluate(q(scope('project')+' .bgm-track-list')+'.scrollTop'),0);
  const afterScroll=await layout();
  for(let i=0;i<2;i++) for(const element of ['header','current','volume','selection','actions','add']) {
    assert.deepEqual(afterScroll.scopes[i][element],beforeScroll.scopes[i][element],element+' stays fixed while its list scrolls');
  }
  await evaluate(q(scope('project')+' .bgm-track-list')+'.scrollTop=220');await rest();
  assert.equal(await evaluate(q(scope('playlist')+' .bgm-track-list')+'.scrollTop'),450);
  await add('playlist');
  assert(await evaluate(`(() => {const row=${q(scope('playlist')+' .bgm-track-row[aria-pressed=true]')}.getBoundingClientRect(),list=${q(scope('playlist')+' .bgm-track-list')}.getBoundingClientRect();return row.top>=list.top-1&&row.bottom<=list.bottom+1;})()`));
  checks.push({check:'24+ tracks, independent list scroll, fixed context/actions/volume and Add reveals selected row',pass:true});

  // At 1200px this filename can fit in full; exercise actual overflow at 1000px.
  await send('Emulation.setDeviceMetricsOverride',{width:1000,height:760,deviceScaleFactor:1,mobile:false});await rest();
  await click(row('playlist','playlist-long'));
  const filename=await evaluate(`(() => {const row=${q(row('playlist','playlist-long'))}, name=row.querySelector('.bgm-track-name'),duration=row.querySelector('.bgm-duration'),r=row.getBoundingClientRect(),d=duration.getBoundingClientRect();return {ellipsis:getComputedStyle(name).textOverflow,truncated:name.scrollWidth>name.clientWidth,durationWidth:d.width,durationRight:d.right,rowRight:r.right,fullName:row.getAttribute('aria-label')};})()`);
  assert.equal(filename.ellipsis,'ellipsis');assert(filename.truncated);assert(filename.durationWidth>=30);assert(filename.durationRight<=filename.rowRight);
  await evaluate(q(row('playlist','playlist-long'))+'.focus()');await rest();
  const tooltipFocus=await evaluate(`({exists:!!document.querySelector('.bgm-filename-tooltip'),hasFocus:document.hasFocus(),focused:document.activeElement.dataset.trackId,
    row:${q(row('playlist','playlist-long'))}.getBoundingClientRect().toJSON(),list:${q(scope('playlist')+' .bgm-track-list')}.getBoundingClientRect().toJSON()})`);
  assert(tooltipFocus.exists,'Full filename tooltip available to keyboard focus: '+JSON.stringify(tooltipFocus));
  assert(await evaluate('document.querySelector(".bgm-filename-tooltip").textContent.includes("Anniversary_Full_Version_Master.flac")'));
  await key('Escape','Escape',27);
  assert(!await evaluate('!!document.querySelector(".bgm-filename-tooltip")'));
  checks.push({check:'Long names truncate without shrinking duration; keyboard-accessible full-name tooltip',pass:true,filename});

  await evaluate(q(scope('playlist')+' input')+'.focus()');await key('Tab','Tab',9);
  assert(await evaluate('document.activeElement.classList.contains("bgm-track-row")'));
  const beforeKeyboard=await currentName('playlist');
  await key('ArrowUp','ArrowUp',38);
  assert.equal(await currentName('playlist'),beforeKeyboard,'Arrow selection is not Apply');
  assert(await evaluate('document.activeElement.getAttribute("aria-pressed")==="true"&&getComputedStyle(document.activeElement).outlineStyle!=="none"'));
  await key('Tab','Tab',9);assert(await evaluate('document.activeElement.classList.contains("bgm-use")'));
  await key(' ','Space',32);
  assert.notEqual(await currentName('playlist'),beforeKeyboard);
  checks.push({check:'Roving row keyboard access, selection-only arrows, visible focus and explicit Space Apply',pass:true});

  for(const [name,width,height,stacked] of [['wide',1600,900,false],['preferred',1200,760,false],['normal',1000,650,false],['dual-minimum',820,700,false],['narrow',780,760,true],['compact',560,700,true]]) {
    await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});await rest();
    const g=await layout(),[left,right]=g.scopes;
    assert(g.scrollWidth<=width+1,'No horizontal overflow: '+name);assert(g.scrollHeight<=height+1,'Outer shell fits: '+name+' '+JSON.stringify(g));
    assert(Math.abs(left.rect.width-right.rect.width)<=1,'Equal scope widths');
    if(stacked) assert(right.rect.y>=left.rect.bottom-1,'Scopes stack only at narrow width');
    else {assert(Math.abs(left.rect.y-right.rect.y)<1);assert(right.rect.x>=left.rect.right-1);assert.deepEqual(left.add.y,right.add.y);}
    assert(g.scopes.every(s=>s.list.height>=34&&s.opacity==='1'));
    checks.push({check:name+' symmetric layout and contained independent lists',pass:true,geometry:g});
  }
  await send('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'reduce'}]});
  assert.equal(await evaluate('getComputedStyle(document.querySelector(".bgm-track-row")).transitionDuration'),'0s');
  for(const label of ['Hide to tray','Minimize','Maximize','Restore','Close']) {
    await click('[aria-label='+JSON.stringify(label)+']');
    assert(await evaluate('document.querySelector(".bgm-shell-feedback").textContent.includes("visual prototype only")'));
  }
  checks.push({check:'Reduced motion and inherited mock-only caption controls',pass:true});
  assert.equal(runtimeErrors.length,0,'No runtime errors');assert.equal(browserWarnings.length,0,'No browser warnings/errors');
  await fs.writeFile(new URL('dom-checks.json',evidence),JSON.stringify({pass:true,checks,runtimeErrors,browserWarnings,uxTrial:'PENDING HUMAN continuous operation'},null,2));
  console.log(JSON.stringify({pass:true,checks:checks.length,runtimeErrors:runtimeErrors.length,browserWarnings:browserWarnings.length}));
} catch(error) {
  await fs.writeFile(new URL('dom-checks-failed.json',evidence),JSON.stringify({pass:false,error:String(error),checks,runtimeErrors,browserWarnings},null,2));
  throw error;
} finally { socket.close(); }
