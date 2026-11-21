import assert from 'node:assert/strict';import fs from 'node:fs';import {documentShortcut}from '../src/sessionShortcuts.ts';
const checks=[];const event=(key,patch={})=>({key,ctrlKey:true,altKey:false,metaKey:false,shiftKey:false,...patch});
function check(name,fn){try{fn();checks.push({name,pass:true});}catch(e){checks.push({name,pass:false,error:e.message});}}
check('Ctrl+N and Ctrl+O map to the shared project command names',()=>{assert.equal(documentShortcut(event('n')),'new');assert.equal(documentShortcut(event('o')),'open');});
check('Ctrl+S and Ctrl+Shift+S remain distinct Save/SaveAs',()=>{assert.equal(documentShortcut(event('s')),'save');assert.equal(documentShortcut(event('S',{shiftKey:true})),'save-as');});
check('Key case does not invent another binding',()=>{assert.equal(documentShortcut(event('N')),'new');assert.equal(documentShortcut(event('O')),'open');});
check('Shift+N/O is not silently treated as standard New/Open',()=>{assert.equal(documentShortcut(event('N',{shiftKey:true})),null);assert.equal(documentShortcut(event('O',{shiftKey:true})),null);});
check('Ctrl+Alt, Meta and unmodified keys cannot invoke document commands',()=>{for(const patch of [{ctrlKey:false},{altKey:true},{metaKey:true}])for(const key of ['n','o','s'])assert.equal(documentShortcut(event(key,patch)),null);});
check('No new Space/Delete/F5/Close/Print/transport shortcut is added',()=>{for(const key of [' ','Delete','F5','w','p','ArrowLeft','ArrowRight'])assert.equal(documentShortcut(event(key)),null);});
const result={pass:checks.every(c=>c.pass),checks};fs.writeFileSync(new URL('../node_modules/.cache/session-phase-f/shortcuts-green.json',import.meta.url),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));if(!result.pass)process.exitCode=1;
