import { chromeExecutable } from './chrome-executable.mjs';
// Native Chromium range gestures in both production BGM scopes. The wire peer
// controls acknowledgements, so this does not claim a Windows HWND/audio test.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp,realpath,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {setTimeout as pause} from 'node:timers/promises';
import {fixtureAudioEffects,generation,incarnation,ids} from './session-readonly-fixture.mjs';

const root=fileURLToPath(new URL('../',import.meta.url));
const origin='http://127.0.0.1:9474',devtools='http://127.0.0.1:9475';
const [playlistId,itemId]=[ids.a,ids.video];
const replacementItemId=ids.image;
const globalTrackId='30000000-0000-4000-8000-000000000001',itemTrackId='30000000-0000-4000-8000-000000000002';
const permit='33333333-1111-4222-8333-444444444444',audio=fixtureAudioEffects();
const queue=(id,alias)=>({enabled:true,mode:'list-loop',volume:.5,tracks:[{id,alias,source:'C:/owned/test.wav',volume:.5}]});
const initial={permit,documentIncarnation:audio.documentIncarnation,playlist:{id:playlistId,alias:'BGM QA'},items:[{id:itemId,alias:'Item QA'},{id:replacementItemId,alias:'Replacement Item'}],
  selectedItemId:itemId,playlistQueue:queue(globalTrackId,'Global track'),itemQueue:queue(itemTrackId,'Item track'),
  revision:'10',dirty:false,language:'en',busy:false,pending:null,lastImport:null};

for(const url of [origin,devtools])await assert.rejects(fetch(url),'QA port must be free');
const profile=await mkdtemp(path.join(tmpdir(),'dse-bgm-volume-dom-'));
const server=spawn(process.execPath,[path.join(root,'node_modules/vite/bin/vite.js'),'--host','127.0.0.1','--port','9474','--strictPort'],{cwd:root,windowsHide:true,stdio:'ignore'});
const browser=spawn(chromeExecutable,['--headless=new','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-port=9475','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:'ignore'});
let socket,serial=0;
const replies=new Map(),diagnostics=[];
const until=async (label,test)=>{
  for(let i=0;i<125;i++){const value=await test();if(value)return value;await pause(40);}
  assert.fail(label);
};
const call=(method,params={})=>new Promise((resolve,reject)=>{
  const id=++serial;
  const timeout=setTimeout(()=>{replies.delete(id);reject(Error(method+' deadline'));},6000);
  replies.set(id,message=>{clearTimeout(timeout);message.error?reject(Error(JSON.stringify(message.error))):resolve(message.result);});
  socket.send(JSON.stringify({id,method,params}));
});
async function evaluate(expression){
  const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
  assert(!result.exceptionDetails,JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
const render=()=>evaluate('new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))');
async function mouse(type,x,y,down=false){
  await call('Input.dispatchMouseEvent',{type,x,y,button:type==='mouseMoved'&&!down?'none':'left',buttons:down?1:0,clickCount:type==='mouseMoved'?0:1});
  await render();
}
function installPeer(initial,audio) {
  const listeners=new Set(), state=structuredClone(initial);
  const reply=(command,payload)=>queueMicrotask(()=>{
    for(const listener of listeners)listener({data:{kind:'result',version:1,requestId:command.requestId,
      generation:command.generation,sessionIncarnation:command.sessionIncarnation,type:command.type,ok:true,payload}});
  });
  const qa=window.bgmVolumeQa={admitted:[],held:[],hold:false,release(){this.held.shift()?.();}};
  window.chrome??={};
  window.chrome.webview={
    addEventListener(_,listener){listeners.add(listener);},
    removeEventListener(_,listener){listeners.delete(listener);},
    postMessage(command){
      const {type,payload}=command;
      if(type==='NegotiateAudioEffects'||type==='GetAudioEffects'){reply(command,audio);return;}
      if(type==='GetBgmEditorSnapshot'){
        if(payload.itemId && payload.itemId!==state.selectedItemId){
          state.selectedItemId=payload.itemId;
          state.itemQueue={...initial.itemQueue,volume:.21};
        }
        reply(command,structuredClone(state));return;
      }
      const {scope,playlistId,itemId,value,trackId}=payload;
      if(type==='GetBgmRuntimeState'){
        reply(command,{status:'ready',reason:'none',state:{scope,playlistId,itemId:itemId??null,activePlaylistId:null,activeItemId:null,
          playbackState:'stopped',active:false,exists:true,phase:'silent-empty-queue',currentTrackId:null,switchEligible:false,lastFailedAttempt:null}});
        return;
      }
      if(!['BgmSetQueueVolume','BgmSetTrackVolume'].includes(type))throw Error('Unexpected '+type);
      if(payload.permit!==state.permit||playlistId!==state.playlist.id||scope!=='playlist'&&itemId!==state.selectedItemId
          ||command.audioEffects?.documentIncarnation!==state.documentIncarnation)throw Error('Wrong BGM target');
      const queue=scope==='playlist'?state.playlistQueue:state.itemQueue;
      const track=trackId&&queue.tracks.find(item=>item.id===trackId);
      if(trackId&&!track)throw Error('Wrong BGM track');
      const owner=track||queue,change=owner.volume!==value;
      owner.volume=value;
      if(change){state.revision=String(Number(state.revision)+1);state.dirty=true;}
      qa.admitted.push({scope,itemId:itemId??null,trackId:trackId??null,value});
      const result={status:change?'applied':'no-change',playlistId,itemId:itemId??null,token:null,authored:change?'committed':'unchanged',
        revision:state.revision,dirty:state.dirty,runtime:'not-required',positionRestore:'not-required',warnings:[],sources:[],
        bgmTarget:{scope,playlistId,...(itemId?{itemId}:{})},bgmReadback:track?{trackVolume:value}:{queueVolume:value}};
      if(qa.hold)qa.held.push(()=>reply(command,result));else reply(command,result);
    },
  };
}

try{
  const target=await until('Browser and Vite readiness',async()=>{
    try{await fetch(origin);return (await (await fetch(devtools+'/json/list')).json()).find(page=>page.type==='page');}
    catch{return false;}
  });
  socket=new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
  socket.onmessage=({data})=>{
    const message=JSON.parse(data);
    if(message.method==='Runtime.exceptionThrown'||message.method==='Runtime.consoleAPICalled'&&['error','warning'].includes(message.params.type)
        ||message.method==='Log.entryAdded'&&['error','warning'].includes(message.params.entry.level))diagnostics.push(message);
    const reply=replies.get(message.id);if(reply){replies.delete(message.id);reply(message);}
  };
  await call('Runtime.enable');await call('Log.enable');await call('Page.enable');
  await call('Emulation.setDeviceMetricsOverride',{width:1400,height:950,deviceScaleFactor:1,mobile:false});
  await call('Page.addScriptToEvaluateOnNewDocument',{source:`(${installPeer})(${JSON.stringify(initial)},${JSON.stringify(audio)})`});
  await call('Page.navigate',{url:`${origin}/?mode=production&view=background-music&generation=${generation}&session=${incarnation}`});
  const range='input[type="range"][data-bgm-control]';
  await until('Four usable native-backed range projections',()=>evaluate(`document.querySelectorAll(${JSON.stringify(range+':enabled')}).length===4`));
  const ranges=()=>evaluate(`[...document.querySelectorAll(${JSON.stringify(range)})].map(e=>Number(e.value))`);

  for(const [index,scope,control,trackId] of [
    [0,'playlist','queue',null], [1,'playlist','track',globalTrackId], [2,'item','queue',null], [3,'item','track',itemTrackId],
  ]){
    const selector=`[data-scope="${scope}"] input[data-bgm-control="${control}-volume"]`;
    const input=`document.querySelector(${JSON.stringify(selector)})`;
    const bounds=await evaluate(`${input}.getBoundingClientRect().toJSON()`);
    const y=bounds.y+bounds.height/2;
    const at=f=>bounds.x+bounds.width*f;
    const before=await ranges();
    const admitted=await evaluate('bgmVolumeQa.admitted.length');
    await evaluate(`window.heldRange=${input};bgmVolumeQa.hold=true`);
    await mouse('mousePressed',at(.70),y,true);
    await until('One initial slider edit admitted',()=>evaluate('bgmVolumeQa.held.length===1'));
    await mouse('mouseMoved',bounds.right+24,y-40,true);
    assert.equal((await ranges())[index],100);
    await mouse('mouseMoved',bounds.x-24,y+40,true);
    assert.equal((await ranges())[index],0);
    await mouse('mouseMoved',at(.35),y,true);
    const reentry=(await ranges())[index];
    assert(reentry>30&&reentry<40);
    assert(await evaluate(`heldRange===${input}&&!${input}.disabled`),'Range was replaced or disabled during local renders');
    await mouse('mouseMoved',bounds.x-22,y-44,true);
    await mouse('mouseReleased',bounds.x-22,y-44);
    await evaluate('bgmVolumeQa.hold=false;bgmVolumeQa.release()');
    await until('Queued overdrag end is absorbed by the same control',()=>evaluate('bgmVolumeQa.admitted.at(-1)?.value===0'));
    await mouse('mouseMoved',at(.55),y);
    assert.equal((await ranges())[index],0,'Hover after release cannot keep dragging');
    await mouse('mousePressed',at(.48),y,true);
    await mouse('mouseReleased',at(.48),y);
    await until('A fresh drag admits a new value',()=>evaluate('bgmVolumeQa.admitted.at(-1)?.value>0.44'));
    const next=await ranges();
    assert(next[index]>=45&&next[index]<=53);
    for(const key of before.keys())if(index!==key)assert.equal(next[key],before[key]);
    const intents=await evaluate(`bgmVolumeQa.admitted.slice(${admitted})`);
    assert(intents.every(edit=>edit.scope===scope&&edit.trackId===trackId));
    console.log(`PASS ${scope} ${control}: native range overdrag/re-entry/outside release, latest value, next gesture and exact target`);
  }
  const globalQueue='document.querySelector(\'[data-scope="playlist"] input[data-bgm-control="queue-volume"]\')';
  const bounds=await evaluate(`${globalQueue}.getBoundingClientRect().toJSON()`);
  const x=f=>bounds.x+bounds.width*f,y=bounds.y+bounds.height/2;
  await mouse('mousePressed',x(.78),y,true);
  const before=await evaluate('bgmVolumeQa.admitted.length');
  const other=await call('Target.createTarget',{url:'about:blank',background:true});
  try{
    await call('Target.activateTarget',{targetId:other.targetId});
    await until('The production BGM document becomes hidden during the gesture',()=>evaluate('document.visibilityState==="hidden"'));
    await call('Target.activateTarget',{targetId:target.id});
    await until('The same production BGM document returns',()=>evaluate('document.visibilityState==="visible"'));
    await mouse('mouseMoved',x(.17),y,true);
    assert.equal(await evaluate('bgmVolumeQa.admitted.length'),before,'Hidden gesture sent a late native edit on re-entry');
    await mouse('mouseReleased',x(.17),y);
    await mouse('mousePressed',x(.52),y,true);
    await mouse('mouseReleased',x(.52),y);
    await until('The next BGM gesture remains usable',()=>evaluate(`bgmVolumeQa.admitted.length>${before}`));
  }finally{
    await call('Target.closeTarget',{targetId:other.targetId});
  }
  console.log('PASS BGM hidden/re-entry cancels trailing input, next drag works');
  const itemQueue='document.querySelector(\'[data-scope="item"] input[data-bgm-control="queue-volume"]\')';
  const itemBounds=await evaluate(`${itemQueue}.getBoundingClientRect().toJSON()`);
  const ix=f=>itemBounds.x+itemBounds.width*f,iy=itemBounds.y+itemBounds.height/2;
  await evaluate(`void (window.retiredQueue=${itemQueue})`);
  await mouse('mousePressed',ix(.78),iy,true);
  const prior=await evaluate('bgmVolumeQa.admitted.length');
  await evaluate(`(()=>{const chooser=document.querySelector('[data-bgm-control=item-chooser]');chooser.value=${JSON.stringify(replacementItemId)};chooser.dispatchEvent(new Event('change',{bubbles:true}));})()`);
  await until('Replacement Item native snapshot',()=>evaluate(`document.querySelector('.bgm-production').dataset.selectedItemId===${JSON.stringify(replacementItemId)}`));
  assert(await evaluate('!retiredQueue.isConnected'),'The former Item range was reused across a target change');
  assert.equal(await evaluate(`Number(${itemQueue}.value)`),21);
  await mouse('mouseMoved',ix(.17),iy,true);
  await mouse('mouseReleased',ix(.17),iy);
  assert.equal(await evaluate('bgmVolumeQa.admitted.length'),prior,'Retired Item gesture changed the replacement Item');
  await mouse('mousePressed',ix(.55),iy,true);
  await mouse('mouseReleased',ix(.55),iy);
  const next=await until('The replacement Item accepts a fresh gesture',()=>evaluate(`bgmVolumeQa.admitted.length>${prior}&&bgmVolumeQa.admitted.at(-1)`));
  assert.equal(next.itemId,replacementItemId);
  console.log('PASS BGM Item replacement retires the old gesture and routes the next edit to the replacement');
  assert.deepEqual(diagnostics,[]);
}finally{
  socket?.close();
  await Promise.all([server,browser].map(child=>new Promise(resolve=>{
    if(child.exitCode!==null||child.signalCode!==null){resolve();return;}
    child.once('exit',resolve);child.kill();
  })));
  const cleanup=await realpath(profile);
  assert.equal(path.dirname(cleanup),await realpath(tmpdir()));
  assert(path.basename(cleanup).startsWith('dse-bgm-volume-dom-'));
  await rm(cleanup,{recursive:true,maxRetries:3,retryDelay:100});
}
