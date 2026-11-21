import assert from 'node:assert/strict';
import fs from 'node:fs';
import { TimelinePreviewMock } from '../src/sessionTimelinePreviewMock.ts';

const source={itemId:'a-opening',source:'C:/MockMedia/Opening.mp4',mediaType:'video',duration:23};
const checks=[];
function check(name,test){try{test();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:String(error)});}}
check('Driver fake clock submits at75 and resolves normal frame at315, without wall time',()=>{
  const d=new TimelinePreviewMock('normal',true,()=>{});d.hover(source,7);d.advance(74);assert.equal(d.submissions,0);d.advance(1);assert.equal(d.submissions,1);assert.equal(d.model.status,'loading');d.advance(239);assert.equal(d.model.status,'loading');d.advance(1);assert.equal(d.model.status,'valid');d.dispose();
});
check('Driver controlled held result can resolve explicitly, no implicit real clock',()=>{
  const d=new TimelinePreviewMock('loading',true,()=>{});d.hover(source,7);d.advance(100000);assert.equal(d.model.status,'loading');assert.equal(d.submissions,1);d.resolve('latest');assert.equal(d.model.status,'valid');d.dispose();
});
check('Driver unavailable and synchronous Image provider follow different result paths',()=>{
  const d=new TimelinePreviewMock('unavailable',true,()=>{});d.hover(source,7);d.advance(315);assert.equal(d.model.status,'unavailable');assert.equal(d.model.displayed,null);d.hover({...source,itemId:'image',mediaType:'image'},2);d.advance(75);assert.equal(d.model.status,'unavailable');assert.equal(d.imageReads,1);d.dispose();
});
check('Out-of-order completion never overwrites latest accepted frame',()=>{
  const d=new TimelinePreviewMock('out-of-order',true,()=>{});d.hover(source,7);d.advance(75);d.hover(source,17);d.advance(75);d.advance(120);assert.equal(d.model.displayed.request.representativeTimestampMilliseconds,15000);d.advance(405);assert.equal(d.model.displayed.request.representativeTimestampMilliseconds,15000);assert.equal(d.model.staleResultsRejected,1);d.dispose();
});
check('Hide and source lifecycle reject still-pending jobs; composition preserves decode job',()=>{
  const d=new TimelinePreviewMock('normal',true,()=>{});d.hover(source,7);d.advance(75);d.compose({key:'position-changed',canvasNumerator:9,canvasDenominator:16});d.advance(240);assert.equal(d.model.status,'valid');assert.equal(d.submissions,1);d.hover(source,17);d.advance(75);d.hide();d.advance(240);assert.equal(d.model.active,false);assert.equal(d.model.staleResultsRejected,1);d.hover(source,17);d.advance(75);d.invalidate();d.advance(240);assert.equal(d.model.displayed,null);assert.equal(d.model.staleResultsRejected,2);d.dispose();
});
check('Long rapid trial consumes model request ledgers and bounds mock completion jobs',()=>{
  const d=new TimelinePreviewMock('loading',true,()=>{});for(let n=0;n<400;n++){d.hover(source,n%2?7:17);d.advance(75);}assert.equal(d.submissions,400);assert.equal(d.model.submittedRequests.length,0);assert.equal(d.model.imageReads.length,0);assert(d.jobs.length<=64);d.dispose();
});
check('Automatic scheduler uses same fake75/240ms contracts and dispose prevents callbacks',()=>{
  const original={performance:globalThis.performance,setTimeout:globalThis.setTimeout,clearTimeout:globalThis.clearTimeout};let now=0,serial=0,notifications=0;const tasks=new Map();
  try{
    Object.defineProperty(globalThis,'performance',{configurable:true,value:{now:()=>now}});
    globalThis.setTimeout=(run,delay)=>{const id=++serial;tasks.set(id,{run,due:now+delay});return id;};globalThis.clearTimeout=id=>tasks.delete(id);
    const tick=target=>{while(true){const pair=[...tasks].sort((a,b)=>a[1].due-b[1].due)[0];if(!pair||pair[1].due>target)break;now=pair[1].due;tasks.delete(pair[0]);pair[1].run();}now=target;};
    const d=new TimelinePreviewMock('normal',false,()=>notifications++);d.hover(source,7);tick(74);assert.equal(d.submissions,0);tick(75);assert.equal(d.submissions,1);tick(315);assert.equal(d.model.status,'valid');d.hover(source,17);assert(tasks.size);d.dispose();const last=notifications;tick(10000);assert.equal(notifications,last);assert.equal(tasks.size,0);
  }finally{Object.defineProperty(globalThis,'performance',{configurable:true,value:original.performance});globalThis.setTimeout=original.setTimeout;globalThis.clearTimeout=original.clearTimeout;}
});
const report={suite:'MainWindow Phase D thumbnail mock scheduler',checks:checks.length,passed:checks.filter(c=>c.pass).length,results:checks};report.status=report.passed===report.checks?'PASS':'FAIL';
fs.writeFileSync(new URL('../node_modules/.cache/session-phase-d/driver-green.json',import.meta.url),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));if(report.status!=='PASS')process.exitCode=1;
