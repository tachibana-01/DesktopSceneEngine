import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createSessionLanguage,languageTransition as step}from '../src/sessionLanguageModel.ts';
const checks=[];function check(name,fn){try{fn();checks.push({name,pass:true});}catch(e){checks.push({name,pass:false,error:e.message});}}
const external=(s,value)=>step(step(s,{type:'external-write',value}),{type:'invalidate'});
const flush=s=>step(s,{type:'flush'});
check('Default source language English',()=>assert.equal(createSessionLanguage().effective,'en'));
check('Uncommitted draft never changes effective or committed language',()=>{let s=step(createSessionLanguage(),{type:'draft',value:'zh_CN'});assert.equal(s.draft,'zh_CN');assert.equal(s.effective,'en');assert.equal(s.committed,'en');});
check('Committed change consumed only after invalidation reload',()=>{let s=external(createSessionLanguage(),'zh_CN');assert.equal(s.effective,'en');assert(s.pending);s=flush(s);assert.equal(s.desired,'zh_CN');assert.equal(s.effective,'zh_CN');assert(!s.pending);});
check('Queued invalidation reloads latest committed truth, never captured event payload',()=>{let s=external(createSessionLanguage(),'zh_CN');s=step(s,{type:'external-write',value:'en'});assert.equal(flush(s).effective,'en');});
check('Multiple invalidations coalesce; flush is inert without queued reload',()=>{let s=external(createSessionLanguage(),'zh_CN');s=step(s,{type:'invalidate'});s=flush(s);assert.equal(s.effective,'zh_CN');assert.deepEqual(flush(s),s);});
check('Reload failure keeps current desired and effective UI without warning',()=>{let s=flush(external(createSessionLanguage(),'zh_CN'));s=step(s,{type:'faults',value:{readFails:true}});s=flush(external(s,'en'));assert.equal(s.desired,'zh_CN');assert.equal(s.effective,'zh_CN');});
for(const field of ['catalogAvailable','installSucceeds'])check(field+' failure retains desired Chinese but falls back to effective English',()=>{let s=step(createSessionLanguage(),{type:'faults',value:{[field]:false}});s=flush(external(s,'zh_CN'));assert.equal(s.desired,'zh_CN');assert.equal(s.effective,'en');});
check('Missing Qt base catalog does not prevent application Chinese',()=>{let s=step(createSessionLanguage(),{type:'faults',value:{qtbaseAvailable:false}});assert.equal(flush(external(s,'zh_CN')).effective,'zh_CN');});
check('Unsupported saved values normalize to English without a new fallback policy',()=>{for(const value of ['xx','ZH_CN',' zh_CN ','']){const s=flush(external(createSessionLanguage(),value));assert.equal(s.desired,'en');assert.equal(s.effective,'en');}});
check('Recovering catalog allows a later reload to apply desired Chinese',()=>{let s=step(createSessionLanguage(),{type:'faults',value:{catalogAvailable:false}});s=flush(external(s,'zh_CN'));s=step(s,{type:'faults',value:{catalogAvailable:true}});assert.equal(flush(step(s,{type:'invalidate'})).effective,'zh_CN');});
check('Transitions do not mutate frozen value-only preference input',()=>{const s=Object.freeze(createSessionLanguage());const n=flush(external(s,'zh_CN'));assert.equal(s.effective,'en');assert.equal(n.effective,'zh_CN');});
const result={pass:checks.every(c=>c.pass),checks};fs.mkdirSync(new URL('../node_modules/.cache/session-phase-f/',import.meta.url),{recursive:true});fs.writeFileSync(new URL('../node_modules/.cache/session-phase-f/language-'+(result.pass?'green':'red')+'.json',import.meta.url),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));if(!result.pass)process.exitCode=1;
