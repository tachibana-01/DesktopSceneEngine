import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { SPLITTER_WIDTH, LIBRARY_MINIMUM, INSPECTOR_MINIMUM, DEFAULT_LAYOUT, createSessionLayout, resizeSessionLayout, moveSessionSplitter, resetSessionLayout } from '../src/sessionLayoutModel.ts';

const checks = [];
function check(name, test) { try { test(); checks.push({name, pass:true}); } catch(error) { checks.push({name, pass:false, error:error.message}); } }
const initial = () => createSessionLayout(1240, 300);
const widths = state => [state.left, state.preview, state.right];
function assertDesktop(state) {
  assert.equal(state.mode, 'three-column');
  assert.equal(state.left + state.preview + state.right + 2 * SPLITTER_WIDTH, state.width);
  assert(state.left >= LIBRARY_MINIMUM); assert(state.right >= INSPECTOR_MINIMUM);
  assert(state.preview >= Math.min(state.previewMinimum, state.width - 2 * SPLITTER_WIDTH - LIBRARY_MINIMUM - INSPECTOR_MINIMUM));
  for(const extent of widths(state)) assert(Number.isSafeInteger(extent) && extent >= 0);
}
check('Production 5px handles and non-collapsible side minima', () => {
  assert.equal(SPLITTER_WIDTH,5);assert.equal(LIBRARY_MINIMUM,200);assert.equal(INSPECTOR_MINIMUM,240);
});
check('Production Reset Layout sizes are 230 / 700 / 300', () => assert.deepEqual(DEFAULT_LAYOUT,{left:230,preview:700,right:300}));
check('Default 1240px workspace contains exact production sizes plus two handles', () => { const s=initial();assert.deepEqual(widths(s),[230,700,300]);assertDesktop(s); });
check('Additional desktop width is assigned to Preview, outer sizes stay fixed', () => { const s=resizeSessionLayout(initial(),1800);assert.deepEqual(widths(s),[230,1260,300]);assertDesktop(s); });
check('Initial desktop width uses fixed side tendencies rather than all-column proportional scaling', () => { assert.deepEqual(widths(createSessionLayout(1500,300)),[230,960,300]); });
check('Left splitter drag changes Library and Preview, retaining Inspector', () => { const s=moveSessionSplitter(initial(),'left',390);assert.deepEqual(widths(s),[390,540,300]);assertDesktop(s); });
check('Right splitter drag changes Inspector and Preview, retaining Library', () => { const s=moveSessionSplitter(initial(),'right',815);assert.deepEqual(widths(s),[230,580,420]);assertDesktop(s); });
check('Left cannot collapse below production 200px', () => {const s=moveSessionSplitter(initial(),'left',-999);assert.equal(s.left,200);assertDesktop(s);});
check('Right cannot collapse below production 240px', () => {const s=moveSessionSplitter(initial(),'right',99999);assert.equal(s.right,240);assertDesktop(s);});
check('Left drag can push neighboring panels but cannot consume minimum Preview or Inspector', () => {const s=moveSessionSplitter(initial(),'left',99999);assert.deepEqual(widths(s),[690,300,240]);assertDesktop(s);});
check('Right drag can push neighboring panels but cannot consume minimum Preview or Library', () => {const s=moveSessionSplitter(initial(),'right',-99999);assert.deepEqual(widths(s),[200,300,730]);assertDesktop(s);});
check('Window resize preserves custom outer widths while Preview absorbs the delta', () => {const s=resizeSessionLayout(moveSessionSplitter(initial(),'left',390),1440);assert.deepEqual(widths(s),[390,740,300]);assertDesktop(s);});
check('Window shrink protects content minimum and side minima without negative widths', () => {let s=moveSessionSplitter(createSessionLayout(1800,400),'left',900);s=resizeSessionLayout(s,1110);assertDesktop(s);assert.equal(s.preview,400);});
check('Shrunk preferred side widths recover when the window expands again', () => {let s=moveSessionSplitter(createSessionLayout(1800,400),'left',900);s=resizeSessionLayout(s,1110);s=resizeSessionLayout(s,1800);assert.deepEqual(widths(s),[900,590,300]);});
check('Reset restores production defaults at current width without restoring the window size', () => {let s=moveSessionSplitter(createSessionLayout(1500,300),'right',900);s=resetSessionLayout(s);assert.equal(s.width,1500);assert.deepEqual(widths(s),[230,960,300]);assertDesktop(s);});
check('Reset is idempotent and leaves dynamic Preview minimum intact', () => {const s=resetSessionLayout(initial());assert.deepEqual(resetSessionLayout(s),s);assert.equal(s.previewMinimum,300);});
check('Existing <=1100 responsive Library protection takes precedence over user layout', () => {const s=resizeSessionLayout(moveSessionSplitter(initial(),'left',390),1000);assert.equal(s.mode,'preview-inspector');assert.deepEqual(widths(s),[0,736,264]);assert.equal(s.preferredLeft,390);});
check('Existing <=760 responsive Inspector protection takes precedence over user layout', () => {const s=resizeSessionLayout(initial(),680);assert.equal(s.mode,'preview-only');assert.deepEqual(widths(s),[0,680,0]);});
check('Returning to desktop restores custom splitter widths after responsive protection', () => {let s=moveSessionSplitter(initial(),'left',390);s=resizeSessionLayout(s,440);s=resizeSessionLayout(s,1240);assert.deepEqual(widths(s),[390,540,300]);assertDesktop(s);});
check('Pointer/keyboard splitter requests are inert while responsive protection is active', () => {for(const width of [440,760,1000,1100]){const s=resizeSessionLayout(initial(),width);assert.deepEqual(moveSessionSplitter(s,'left',500),s);assert.deepEqual(moveSessionSplitter(s,'right',500),s);}});
check('Tiny/zero/invalid viewports produce finite nonnegative protected geometry', () => {for(const width of [0,1,4,NaN,Infinity,-1]){const s=createSessionLayout(width,300);assert.deepEqual(widths(s),[0,Number.isFinite(width)&&width>=0?Math.floor(width):0,0]);}});
check('Fractional CSS measurements and pointer positions quantize down to integer logical pixels', () => {const s=createSessionLayout(1240.9,300.9);assert.equal(s.width,1240);assert.equal(s.previewMinimum,300);assert.deepEqual(widths(moveSessionSplitter(s,'left',390.9)),[390,540,300]);});
check('Nonfinite pointer requests cannot poison layout state', () => {for(const position of [NaN,Infinity,-Infinity])assert.deepEqual(moveSessionSplitter(initial(),'left',position),initial());});
check('Dynamic header minimum is honored and clamped only when it cannot physically fit', () => {const s=resizeSessionLayout(initial(),1240,2000);assert.deepEqual(widths(s),[200,790,240]);assertDesktop(s);});
check('Transitions do not mutate prior layout or introduce document/application/persisted state', () => {
  const s=Object.freeze(initial());const before=JSON.stringify(s);const moved=moveSessionSplitter(s,'left',390);resizeSessionLayout(s,1500);resetSessionLayout(moved);assert.equal(JSON.stringify(s),before);
  for(const key of ['project','document','selection','activeItemId','runtime','display','view','persisted','storage'])assert.equal(key in moved,false);
});
check('Both splitter drags over representative desktops keep all content extents valid', () => {
  for(const width of [1101,1240,1280,1800,3840])for(const minimum of [1,240,300,500,10000])for(const position of [-1000,0,200,400,800,width,10000])for(const side of ['left','right'])assertDesktop(moveSessionSplitter(createSessionLayout(width,minimum),side,position));
});

const result={suite:'MainWindow Phase F layout projection',checks:checks.length,passed:checks.filter(check=>check.pass).length,status:checks.every(check=>check.pass)?'PASS':'FAIL',results:checks};
const directory=fileURLToPath(new URL('../node_modules/.cache/session-phase-f/',import.meta.url));mkdirSync(directory,{recursive:true});
const stage=process.env.DSE_CHECK_STAGE||'green';for(const extension of ['json','log'])writeFileSync(`${directory}/layout-model-${stage}.${extension}`,JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));if(result.status!=='PASS')process.exitCode=1;
