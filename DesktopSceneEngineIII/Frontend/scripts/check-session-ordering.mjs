import assert from 'node:assert/strict';
import fs from 'node:fs';
import { createSessionFixture } from '../src/sessionFixtures.ts';
import { entityTransition, findPlaylist } from '../src/sessionEntities.ts';
import { afterEntityPlayback, playbackTransition } from '../src/sessionPlayback.ts';

const checks=[];
function check(name,test){try{test();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.message});}}
const ids=s=>[s.selectedPlaylistId,s.selectedItemId,s.activePlaylistId,s.activeItemId];
const order=s=>s.project.playlists.map(p=>p.id);
const items=(s,id='playlist-a')=>findPlaylist(s,id).items.map(i=>i.id);
function move(s,kind,id,direction){const action={type:'move-'+kind,id,direction};return afterEntityPlayback(s,entityTransition(s,action),action);}
const fixture=()=>createSessionFixture('normal');
for(const [kind,id] of [['playlist','playlist-b'],['item','a-scene']])for(const direction of [-1,1]){
 check(`${kind} Move ${direction<0?'Up':'Down'} swaps exactly adjacent authored entries`,()=>{
  const s=fixture(),next=move(s,kind,id,direction);
  const before=kind==='playlist'?order(s):items(s),expected=[...before],index=expected.indexOf(id);
  [expected[index],expected[index+direction]]=[expected[index+direction],expected[index]];
  assert.deepEqual(kind==='playlist'?order(next):items(next),expected);
  assert.equal(next.document.revision,s.document.revision+1);
 });
}
for(const [kind,id,direction] of [['playlist','playlist-a',-1],['playlist','playlist-d',1],['item','a-opening',-1],['item','a-disabled',1]]){
 check(`${kind} ${id} rejects boundary ${direction} without dirty or wrap`,()=>{const s=fixture();assert.deepEqual(move(s,kind,id,direction),s);});
}
for(const kind of ['playlist','item']){
 check(`${kind} move rejects missing target and invalid direction`,()=>{const s=fixture();for(const [id,direction]of[['missing',1],[kind==='playlist'?'playlist-b':'a-scene',0],[kind==='playlist'?'playlist-b':'a-scene',2]])assert.deepEqual(move(s,kind,id,direction),s);});
 check(`${kind} move preserves stable objects, selected/active identities, runtime, display and preferences`,()=>{
  let s=fixture();s=entityTransition(s,{type:'select-item',id:'b-image'});
  const next=move(s,kind,kind==='playlist'?'playlist-a':'a-opening',1);
  assert.deepEqual(ids(next),ids(s));assert.deepEqual(next.runtime,s.runtime);
  assert.deepEqual(next.sequence,s.sequence);assert.deepEqual(next.display,s.display);assert.deepEqual(next.view,s.view);
  const flatten=x=>x.project.playlists.flatMap(p=>p.items).sort((a,b)=>a.id.localeCompare(b.id));
  assert.deepEqual(flatten(next),flatten(s));assert.deepEqual(next.project.canvasAspect,s.project.canvasAspect);
 });
}
check('Selected Item moves only within its owner while another Item remains active',()=>{
 let s=fixture();s=entityTransition(s,{type:'select-item',id:'b-image'});
 const next=move(s,'item','b-image',-1);
 assert.deepEqual(ids(next),ids(s));assert.deepEqual(items(next,'playlist-b'),['b-image','b-opening']);assert.deepEqual(items(next),items(s));
});
check('Disabled Playlist and disabled Item retain move capability',()=>{
 const s=fixture(),p=move(s,'playlist','playlist-d',-1),i=move(s,'item','a-disabled',-1);
 assert.equal(order(p)[2],'playlist-d');assert.equal(items(i)[1],'a-disabled');
});
check('Active Item move leaves existing runtime scene order intact; future Next uses new authored order by ID',()=>{
 const s=fixture();const next=move(s,'item','a-opening',1);
 assert.deepEqual(next.runtime,s.runtime);assert.equal(next.activeItemId,'a-opening');
 const navigated=playbackTransition(next,{type:'next'});
 assert.equal(navigated.activeItemId,'a-scene'); // only other enabled target, wrapping new authored order
 assert.equal(navigated.selectedItemId,s.selectedItemId);
});
check('Authored reordered Item becomes next target without rebuilding currently playing projection',()=>{
 let s=fixture();s.project.playlists[0].items[2].enabled=true;
 s=move(s,'item','a-disabled',-1);
 assert.deepEqual(items(s),['a-opening','a-disabled','a-scene']);
 assert.equal(playbackTransition(s,{type:'next'}).activeItemId,'a-disabled');
});
check('Single Item boundary is inert and equal-source occurrences retain independent IDs',()=>{
 const s=fixture();s.project.playlists[0].items=[s.project.playlists[0].items[0]];
 assert.deepEqual(move(s,'item','a-opening',1),s);assert.deepEqual(move(s,'item','a-opening',-1),s);
 const same=fixture();same.project.playlists[0].items[1].source=same.project.playlists[0].items[0].source;
 const n=move(same,'item','a-scene',-1);assert.equal(items(n)[0],'a-scene');assert.notEqual(items(n)[0],items(n)[1]);
});
check('Moves accept frozen inputs and closed Session remains inert',()=>{
 const s=fixture(),before=JSON.stringify(s);function freeze(x){if(x&&typeof x==='object'){Object.freeze(x);Object.values(x).forEach(freeze);}}freeze(s);
 assert(move(s,'playlist','playlist-b',-1));assert.equal(JSON.stringify(s),before);
 const closed={...s,closed:true};assert.deepEqual(move(closed,'item','a-scene',-1),closed);
});
const result={pass:checks.every(c=>c.pass),checks};
fs.mkdirSync(new URL('../node_modules/.cache/session-phase-f/',import.meta.url),{recursive:true});
fs.writeFileSync(new URL('../node_modules/.cache/session-phase-f/ordering-'+(result.pass?'green':'red')+'.json',import.meta.url),JSON.stringify(result,null,2));
console.log(JSON.stringify(result,null,2));if(!result.pass)process.exitCode=1;
