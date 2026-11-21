import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createSessionLayout, moveSessionSplitter, resetSessionLayout, resizeSessionLayout } from '../src/sessionLayoutModel.ts';

// Narrow structural/owner guards. Pointer, focus and actual DOM layout are
// separately verified in the browser; these checks do not claim Native parity.
const read = path => readFileSync(new URL(path, import.meta.url), 'utf8');
const component = read('../src/SessionSettings.tsx');
const shell = read('../src/SessionShell.tsx');
const title = read('../src/ShellTitleBar.tsx');
const css = read('../src/sessionSettings.css');
const catalog = read('../src/sessionZh.ts');
const previewCss = read('../src/sessionPreview.css');
const preview = read('../src/SessionPreviewView.tsx');
const results = [];
function check(name, run) { try { run(); results.push({ name, pass: true }); } catch (error) { results.push({ name, pass: false, error: error.message.split('\n')[0] }); } }

check('Gear uses existing Session-only leftActions slot between identity and context', () => {
  const prototypeTitle=title.slice(title.indexOf('export function TitleBar'));
  assert(prototypeTitle.indexOf('{leftActions}') > prototypeTitle.indexOf('className="application-identity"'));
  assert(prototypeTitle.indexOf('{leftActions}') < prototypeTitle.indexOf('{context &&'));
  assert.match(title,/beforeDrag=\{<>\{leftActions\}<div className="session-title-context">\{context\}/);
  assert.match(shell, /leftActions=\{<>\s*<SessionSettings onResetLayout=\{layout\.reset\}/);
  for (const file of ['LauncherShell.tsx', 'BackgroundMusicShell.tsx']) assert(!read('../src/' + file).includes('SessionSettings'));
});
check('Settings uses existing layout and controlled highlight commands without persistence authority', () => {
  assert.match(component, /onResetLayout\(\); close\(true\)/);
  assert.match(component, /onHighlight\(!highlighted\)/);
  assert(!/mock\.|presentationTransition|viewTransition|setHighlighted|localStorage|sessionStorage|postMessage|fetch\(/.test(component));
});
check('Highlight comes from the shared projection; background exposes native selection only in production', () => {
  assert.match(component, /role="menuitemcheckbox"[\s\S]*?aria-checked=\{highlighted\}/);
  assert.match(shell, /highlighted=\{view\.highlighted\}/);
  assert.match(shell, /onHighlight=\{value => mock\.presentation\(\{scope:'view',action:\{type:'highlight',value\}\}\)\}/);
  assert.match(component, /data-state-authority=\{native \? 'native' : 'unavailable'\}/);
  assert.match(component, /data-settings-unavailable=\{onBackground \? undefined : 'desktop-background'\}/);
  const child = component.slice(component.indexOf('{subOpen && <div'));
  assert.match(child,/aria-checked=\{native \? background === mode : undefined\}/);
  assert.match(child,/role=\{native \? 'menuitemradio' : 'menuitem'\}/);
});
check('Exactly the requested three first-level entries and two existing production labels', () => {
  const main = component.slice(component.indexOf('<div ref={main}'), component.indexOf('{subOpen && <div'));
  assert.equal((main.match(/role="menuitem(?:checkbox)?"/g) ?? []).length, 3);
  for (const label of ['Reset Layout', 'Highlight Canvas Boundary', 'Desktop Background', 'Cover Entire Desktop Background', 'Reveal Wallpaper in Uncovered Areas']) {
    assert(component.includes(label)); assert(catalog.includes('"' + label + '"'));
  }
});
check('Popup lifecycle observes the complete system without swallowing outside input', () => {
  assert.match(component, /\[trigger\.current, main\.current, submenu\.current\]/);
  assert.match(component, /addEventListener\('pointerdown', outside, true\)/);
  assert.match(component, /addEventListener\('contextmenu', outside, true\)/);
  assert.match(component, /addEventListener\('focusin', outside, true\)/);
  assert.match(component, /createPortal/);
  assert.match(component, /120\)/);
});
check('Gear remains a 30px no-drag button without changing titlebar height', () => {
  assert.match(css, /width: 30px; height: 30px/); assert.match(css, /app-region: no-drag/);
  assert(!/\.title-bar\s*\{/.test(css));
  assert.match(component, /width="17" height="17"/);
  assert.match(component, /t\('Settings'\)/);
});
check('Highlight OFF has no legacy neutral frame; ON uses an outside outline only', () => {
  assert.match(previewCss, /\.preview-canvas\.preview-chrome-frame\s*\{[^}]*background:\s*transparent/);
  assert(!/#4a5562|#f04444/.test(previewCss));
  assert.match(previewCss, /outline:\s*2px solid #d27368/);
  assert.match(previewCss, /outline-offset:\s*0/);
});
check('TS wrapper highlight is aligned to the canonical native rectangle without owning media pixels', () => {
  for (const key of ['left','top','width','height']) {
    assert(preview.includes("'--canvas-" + key + "'"));
    assert(previewCss.includes('var(--canvas-' + key + ')'));
  }
  assert.match(previewCss, /\.preview-chrome-frame::after\s*\{[^}]*pointer-events:\s*none/);
  assert.match(previewCss, /\[data-highlight=true\] \.preview-chrome-frame::after/);
});
check('Real mutable three-column layout resets without changing window extent', () => {
  let state = createSessionLayout(1440, 300);
  state = moveSessionSplitter(state, 'left', 390);
  state = moveSessionSplitter(state, 'right', 1000);
  assert.notEqual(state.left, 230); assert.notEqual(state.right, 300);
  const result = resetSessionLayout(state);
  assert.equal(result.left, 230); assert.equal(result.right, 300);
  assert.equal(result.width, state.width); assert.equal(result.previewMinimum, state.previewMinimum);
  assert.deepEqual(resetSessionLayout(result), result);
  assert(!('project' in result)); assert(!('persisted' in result));
});
check('Reset while responsive restores defaults when the real workspace expands', () => {
  let state = moveSessionSplitter(createSessionLayout(1440, 300), 'left', 390);
  state = resetSessionLayout(resizeSessionLayout(state, 680));
  assert.equal(state.mode, 'preview-only');
  state = resizeSessionLayout(state, 1440);
  assert.equal(state.left, 230); assert.equal(state.right, 300);
});

const passed = results.filter(result => result.pass).length;
console.log(JSON.stringify({ suite: 'Settings owner and structure guards', passed, checks: results.length, results }, null, 2));
if (passed !== results.length) process.exitCode = 1;
