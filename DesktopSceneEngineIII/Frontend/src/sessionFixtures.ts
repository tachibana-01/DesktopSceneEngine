import type { MockSource, SessionItem, SessionProject, SessionProjection } from './sessionTypes';
import { defaultPresentation } from './sessionInspectorDefaults.ts';
import { createController } from './sessionPlaybackSequence.ts';
import { rebasePlaybackToActive } from './sessionPlayback.ts';
import { createSessionDisplay, displayTransition } from './sessionDisplayModel.ts';
import { createSessionView } from './sessionViewModel.ts';
import { createSessionRole } from './sessionLifecycleModel.ts';

export const MOCK_DISPLAY_OWNER = 'mock-persistent-display-a';
export const EDITORIAL_PATH = 'C:/MockProjects/editorial.json';
export const STUDIO_PATH = 'C:/MockProjects/studio.json';
export const SAVE_AS_PATH = 'C:/MockProjects/editorial-copy.json';
export const INVALID_PATH = 'C:/MockProjects/invalid.json';
export const WRONG_OWNER_PATH = 'C:/MockProjects/other-display.json';

export const mockSources: readonly MockSource[] = [
  { source: 'C:/MockMedia/Opening.mp4', kind: 'video', duration: 23 },
  { source: 'C:/MockMedia/Scene-A.mp4', kind: 'video', duration: 107 },
  { source: 'C:/MockMedia/Background.png', kind: 'image', duration: 10 },
  { source: 'C:/MockMedia/unsupported.txt', kind: null, duration: 0 },
];
function item(id: string, alias: string, sourceIndex: number, enabled = true, rate = 1): SessionItem {
  const source = mockSources[sourceIndex];
  return { id, alias, source: source.source, kind: source.kind as 'image' | 'video', enabled, duration: source.duration, rate, mediaGain: 1, presentation: defaultPresentation() };
}
export function editorialProject(): SessionProject {
  return { displayOwner: MOCK_DISPLAY_OWNER, canvasAspect: [16, 9], playlistMode: 'repeat-current', playlists: [
    { id: 'playlist-a', alias: 'Playlist A', enabled: true, masterVolume: 1, itemMode: 'list-loop', items: [
      item('a-opening', 'Opening', 0), item('a-scene', 'Scene A', 1, true, 1.2), item('a-disabled', 'Background', 2, false),
    ] },
    { id: 'playlist-b', alias: 'Playlist B', enabled: true, masterVolume: 1, itemMode: 'list-loop', items: [
      item('b-opening', 'Alternate Opening', 0), item('b-image', 'Background', 2),
    ] },
    { id: 'playlist-c', alias: 'Playlist C', enabled: true, masterVolume: 1, itemMode: 'list-loop', items: [] },
    { id: 'playlist-d', alias: 'Playlist D', enabled: false, masterVolume: 1, itemMode: 'list-loop', items: [item('d-scene', 'Scene A', 1)] },
  ] };
}
export function studioProject(): SessionProject {
  return { displayOwner: MOCK_DISPLAY_OWNER, canvasAspect: [16, 9], playlistMode: 'repeat-current', playlists: [
    { id: 'studio', alias: 'Studio', enabled: true, masterVolume: 1, itemMode: 'list-loop', items: [item('studio-image', 'Studio background', 2)] },
  ] };
}

export const inspectorScenarios = [
  ['no-selection', 'Inspector · No selection, playback remains active'],
  ['playlist-selected', 'Inspector · selected active Playlist'],
  ['playlist-selected-active-different', 'Inspector · selected Playlist ≠ active Playlist'],
  ['image-selected', 'Inspector · selected Image, another Item active'],
  ['video-selected', 'Inspector · selected Video, another Item active'],
  ['selected-item-not-active', 'Inspector · selected Video ≠ active Item'],
  ['selected-video-active', 'Inspector · selected active Video'],
  ['same-source-independent-items', 'Inspector · independent same-source Items'],
  ['configured-rate-runtime-failure', 'Inspector · configured Rate succeeds, runtime fails'],
  ['long-source', 'Inspector · long mixed-language Source'], ['long-alias', 'Inspector · long aliases'],
  ['custom-aspect', 'Inspector · authored Custom Aspect 8:5'],
  ['active-image-duration', 'Inspector · active Image duration'],
  ['visual-prepare-failure', 'Inspector · silent visual prepare failure'],
  ['visual-bind-failure', 'Inspector · silent visual rollback succeeds'],
  ['visual-seek-failure', 'Inspector · silent restore seek failure'],
  ['visual-rollback-failure', 'Inspector · silent rollback bind failure'],
  ['visual-rollback-seek-failure', 'Inspector · silent rollback seek failure'],
] as const;

export const playbackScenarios = [
  ['playback-list-loop', 'Playback · List Loop, selected B/B3, active A/A2'],
  ['playback-repeat-project', 'Playback · Project Repeat Current, A at end'],
  ['playback-repeat-item', 'Playback · Item Repeat Current'],
  ['playback-random', 'Playback · deterministic Random with replacement'],
  ['playback-empty', 'Playback · no media, selection remains'],
  ['playback-no-selection', 'Playback · active runtime, no editor selection'],
  ['playback-paused', 'Playback · paused runtime'],
  ['playback-single-item', 'Playback · one playable Item'],
  ['playback-disabled', 'Playback · disabled/empty targets filtered'],
  ['playback-end-project', 'Playback · final playable Playlist boundary'],
  ['playback-natural', 'Playback · natural completion across Playlists'],
  ['playback-no-target', 'Playback · stale projection, no playable target'],
  ['playback-masters', 'Playback · Inspector Master B, Rail Master A'],
  ['playback-muted', 'Playback · Master 70%, Gain 50%, muted'],
  ['playback-rate', 'Playback · actual Rate 1.25×'],
  ['playback-long-alias', 'Playback · long mixed-language aliases'],
  ['playback-long-time', 'Playback · hour-format runtime time'],
  ['playback-switch-failure', 'Playback · same-Playlist switch fails silently'],
  ['playback-natural-failure', 'Playback · completion halted after switch failure'],
  ['playback-cross-failure', 'Playback · cross-Playlist bind fails, rollback succeeds'],
  ['playback-cross-rollback', 'Playback · rollback fails, runtime unconfigured'],
  ['playback-prepare-failure', 'Playback · sequence preparation fails'],
] as const;

export const timelineScenarios = [
  ['timeline-valid-video', 'Timeline · valid Video'], ['timeline-valid-image', 'Timeline · valid Image'],
  ['timeline-no-media', 'Timeline · No media loaded'], ['timeline-zero-duration', 'Timeline · zero duration'],
  ['timeline-invalid-duration', 'Timeline · non-finite duration'], ['drag-preview', 'Timeline · preview only until Release'],
  ['drag-stale-item', 'Timeline · external Next 500 ms after Press'], ['drag-stale-duration', 'Timeline · duration changes 500 ms after Press'],
  ['drag-natural-completion', 'Timeline · completion 500 ms after Press'],
  ['seek-paused', 'Timeline · seek Paused / 1.25× / 70% / 50% / Muted'], ['seek-rate-125', 'Timeline · actual 1.25×'],
  ['seek-muted', 'Timeline · independent Output Mute'], ['seek-failure', 'Timeline · silent seek failure'],
  ['right-edge-9999', 'Timeline · exact 9999 ordinary seek'], ['right-edge-10000', 'Timeline · exact 10000 Next'],
  ['hover-loading', 'Hover · Loading, result held'], ['hover-valid', 'Hover · Valid deterministic frame'],
  ['hover-unavailable', 'Hover · Unavailable result'], ['hover-out-of-order', 'Hover · out-of-order 600 / 120 ms'],
  ['hover-source-change', 'Hover · Next changes active source'], ['hover-generation-change', 'Hover · same Item Repeat / generation change'],
  ['hover-leave-pending', 'Hover · leave before result'], ['hover-drag-pending', 'Hover · press before result'],
  ['selected-not-active', 'Timeline · selected B/B3, active A/A2'],
  ['hover-floating-16-9', 'Floating hover · Canvas 16:9'], ['hover-floating-16-10', 'Floating hover · Canvas 16:10'],
  ['hover-floating-4-3', 'Floating hover · Canvas 4:3'], ['hover-floating-1-1', 'Floating hover · Canvas 1:1'],
  ['hover-floating-ultrawide', 'Floating hover · Canvas 32:9'], ['hover-floating-portrait', 'Floating hover · Canvas 9:16'],
  ['hover-floating-extreme-wide', 'Floating hover · Canvas 1000:1'], ['hover-floating-extreme-portrait', 'Floating hover · Canvas 1:1000'],
  ['hover-left-edge', 'Floating hover · move to left edge'], ['hover-center', 'Floating hover · move to center'],
  ['hover-right-edge', 'Floating hover · move to right edge'], ['hover-selected-not-active', 'Floating hover · selected B, active A'],
  ['hover-then-drag', 'Floating hover · hover then drag'], ['hover-then-leave', 'Floating hover · hover then leave'],
  ['hover-window-resize', 'Floating hover · resize while visible'],
] as const;

export const presentationScenarios = [
  ['canvas-16-9-display-16-9','Preview · Canvas16:9 / Display16:9'], ['canvas-16-9-display-16-10','Preview · loaded Canvas16:9 / Display16:10'],
  ['canvas-16-10','Preview · Canvas16:10'], ['canvas-4-3','Preview · Canvas4:3'], ['canvas-1-1','Preview · Canvas1:1'],
  ['canvas-ultrawide','Preview · Canvas32:9'], ['canvas-portrait','Preview · Canvas9:16'],
  ['preview-tiny','Preview · tiny8×8 viewport'], ['preview-zero','Preview · zero viewport'],
  ['display-primary','Display · Primary'], ['display-secondary','Display · Secondary bound Session'], ['display-long-name','Display · long name / identity / dimensions'],
  ['display-unavailable','Display · exact bound unavailable'], ['enumeration-failed','Display · enumeration failed'],
  ['identify-ready','Display · Identify ready'], ['identify-failure','Display · Identify overlay fails'], ['identify-resolution-loss','Display · Identify fresh resolution lost'],
  ['preview-only','Presentation · Preview-only'], ['deployed','Presentation · Deployed'], ['deploy-empty','Presentation · no media, Deploy eligible'],
  ['deploy-failure','Presentation · Deploy fails'], ['deploy-partial-failure','Presentation · failed Deploy retains host'],
  ['retained-display-lost','Presentation · retained, Display lost'], ['retained-enumeration-failed','Presentation · retained, enumeration failed'], ['undeploy-failure','Presentation · Undeploy cleanup fails'],
  ['highlight-off','View · Highlight OFF'], ['highlight-on','View · Highlight ON'],
  ['background-cover','View · Cover Entire'], ['background-reveal','View · Reveal Wallpaper'], ['background-save-failure','View · background save fails'],
] as const;

export const remainingScenarios = [
  ['playlist-first','Move · first Playlist'],['playlist-middle','Move · middle Playlist'],['playlist-last','Move · last Playlist'],
  ['item-first','Move · first Item'],['item-middle','Move · middle Item'],['item-last','Move · last Item'],
  ['selected-playlist-active-other','Move · selected Playlist ≠ active'],['selected-item-active-other','Move · selected Item ≠ active, same Playlist'],
  ['custom-layout-left-wide','Layout · Library wide'],['custom-layout-inspector-wide','Layout · Inspector wide'],['reset-layout','Layout · production default'],
  ['language-en','Language · committed English'],['language-zh','Language · committed 简体中文'],['language-invalid','Language · unsupported saved value → English'],
  ['language-catalog-failure','Language · Chinese catalog unavailable → English'],['language-read-failure','Language · reload failure keeps current UI'],
  ['dirty-shortcut-new','Shortcut · dirty New'],['dirty-shortcut-open','Shortcut · dirty Open'],['untitled-shortcut-save','Shortcut · Untitled Save'],
  ['hide-eligible','Hide · qualified restoration'],['hide-no-restore','Hide · no restore eligibility'],['hide-modal','Hide · owned delete modal'],
  ['hide-close-pending','Hide · Close Pending fact'],['hide-stale-permit','Hide · Show wins before permit'],['hide-denied-permit','Hide · Coordinator denies permit'],
  ['hide-recovery-race','Hide · restore authority lost before permit'],['hide-expired-permit','Hide · permit expired'],
  ['unsupported-media','Feedback · unsupported media Add'],['empty-playlist-play','Feedback · empty Playlist double-click'],
  ['playback-sync-warning','Feedback · Add cannot synchronize'],['playback-position-warning','Feedback · Add cannot restore position'],
] as const;
export const sessionScenarios = [
  ['empty-project', 'New Session · empty Project'], ['normal', 'Working Session · multiple Playlists'],
  ['selected-playlist-differs', 'Selected Playlist ≠ active Playlist'], ['selected-item-differs', 'Selected Item ≠ active Item'],
  ['dirty-untitled', 'Dirty Untitled'], ['path-backed-clean', 'Path-backed · clean'],
  ['save-failure', 'Save failure'], ['open-failure', 'Open invalid candidate'], ['wrong-owner', 'Open wrong DisplayOwner'],
  ['active-playlist-disable', 'Disable active Playlist'], ['active-item-disable', 'Disable active Item'],
  ['delete-active-item', 'Delete active Item'], ['delete-active-playlist', 'Delete active Playlist'],
  ['playback-failure', 'Playback bind failure'], ['successor-failure', 'Successor bind failure'],
  ['sync-warning', 'Active projection sync warning'], ['open-reload-failure', 'Open reload fails after Save'],
  ['open-reload-owner', 'Open reload owner changes after Save'],
  ...inspectorScenarios,
  ...playbackScenarios,
  ...timelineScenarios,
  ...presentationScenarios,
  ...remainingScenarios,
  ['gap-duplicate-playlists','Gap F01 · duplicate Playlist aliases'],
  ['gap-add-rollback-seek','Gap F03 · Add, rollback seek fails'],
  ['gap-remove-rollback-seek','Gap F03 · Remove noncurrent, rollback seek fails'],
  ['gap-close-pending-deployed','Gap F05 · Close Pending, deployed'],
  ['gap-ending-ready','Gap F05 · ending, Preview-only'],
  ['gap-ending-deployed','Gap F05 · ending, retained'],
  ['gap-hover-geometry','Gap F06 · active geometry, selected differs'],
  ['playlist-volume-default', 'Playlist volume · row controls / Inspector / Output Mute'],
  ['playlist-volume-selected-vs-target', 'Playlist volume · selected A / active B / target C'],
  ['playlist-volume-active-vs-target', 'Playlist volume · selected B / active A / target C'],
  ['playlist-volume-duplicate-alias', 'Playlist volume · duplicate Scene aliases'],
  ['playlist-volume-zero', 'Playlist volume · zero Master'],
  ['playlist-volume-last-nonzero', 'Playlist volume · 40 → mute → restore 40'],
  ['playlist-volume-no-history', 'Playlist volume · zero without history → restore 100'],
  ['playlist-volume-hover', 'Playlist volume · speaker / gap / arrow / popover'],
  ['playlist-volume-wheel', 'Playlist volume · wheel ±5 / slider 1%'],
  ['playlist-volume-bottom-row', 'Playlist volume · scroll to last row; always downward'],
] as const;
export type SessionScenario = typeof sessionScenarios[number][0];
export function readSessionScenario(value: string | null): SessionScenario {
  return sessionScenarios.find(([key]) => key === value)?.[0] ?? 'empty-project';
}

export function createSessionFixture(scenario: SessionScenario): SessionProjection {
  const project = editorialProject();
  const state: SessionProjection = {
    display: createSessionDisplay(MOCK_DISPLAY_OWNER), view: createSessionView(MOCK_DISPLAY_OWNER),
    project,
    document: { path: EDITORIAL_PATH, revision: 1, cleanRevision: 1 },
    selectedPlaylistId: 'playlist-a', selectedItemId: 'a-opening', activePlaylistId: 'playlist-a', activeItemId: 'a-opening',
    runtime: { time: 8, paused: false, halted: false, rate: 1, volume: 1, items: structuredClone(project.playlists[0].items) },
    sequence: createController(42), outputMuted: false,
    nextPlaylistOrdinal: '4', nextEntityOrdinal: 100,
    files: {
      [EDITORIAL_PATH]: { kind: 'project', project: editorialProject() },
      [STUDIO_PATH]: { kind: 'project', project: studioProject() },
      [INVALID_PATH]: { kind: 'invalid' },
      [WRONG_OWNER_PATH]: { kind: 'project', project: { ...studioProject(), displayOwner: 'mock-other-display' } },
      'C:/MockProjects/invalid-v3.json': { kind: 'invalid-v3' },
      'C:/MockProjects/legacy.json': { kind: 'legacy' },
      'C:/MockProjects/unknown-version.json': { kind: 'unknown-version' },
      'C:/MockProjects/unreadable.json': { kind: 'unreadable' },
    },
    faults: { saveFails: false, rateFails: false, navigation: 'none', reload: 'none', playback: 'none' }, closed: false, feedback: null,
  };
  if (scenario === 'empty-project') {
    state.project = { displayOwner: MOCK_DISPLAY_OWNER, canvasAspect: [16, 9], playlistMode: 'repeat-current', playlists: [] };
    state.document.path = null; state.nextPlaylistOrdinal = '0';
    state.selectedPlaylistId = state.selectedItemId = state.activePlaylistId = state.activeItemId = null;
    state.runtime = null;
  } else if (scenario === 'path-backed-clean') {
    state.selectedPlaylistId = state.selectedItemId = state.activePlaylistId = state.activeItemId = null;
    state.runtime = null;
  } else if (scenario === 'selected-playlist-differs') {
    state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = null;
  } else if (scenario === 'selected-item-differs') state.selectedItemId = 'a-scene';
  if (scenario === 'dirty-untitled' || scenario === 'save-failure' || scenario.startsWith('open-reload')) {
    state.document.revision++;
    state.project.playlists[0].alias = 'Edited Playlist A';
    if (scenario === 'dirty-untitled') state.document.path = null;
  }
  if (scenario === 'save-failure') state.faults.saveFails = true;
  if (scenario === 'playback-failure') state.faults.playback = 'start-bind';
  if (scenario === 'successor-failure') state.faults.playback = 'successor-bind';
  if (scenario === 'sync-warning') state.faults.playback = 'rebuild-prepare';
  if (scenario === 'open-reload-failure') state.faults.reload = 'invalid';
  if (scenario === 'open-reload-owner') state.faults.reload = 'wrong-owner';
  if (scenario === 'no-selection') state.selectedPlaylistId = state.selectedItemId = null;
  if (scenario === 'playlist-selected') state.selectedItemId = null;
  if (scenario === 'playlist-selected-active-different') { state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = null; }
  if (scenario === 'image-selected') { state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = 'b-image'; }
  if (scenario === 'video-selected' || scenario === 'selected-item-not-active') state.selectedItemId = 'a-scene';
  if (scenario === 'same-source-independent-items') {
    project.playlists[0].items.splice(1, 0, { ...structuredClone(project.playlists[0].items[0]), id: 'a-opening-copy', alias: 'Opening copy' });
    state.selectedItemId = 'a-opening-copy';
    state.runtime!.items = structuredClone(project.playlists[0].items);
  }
  if (scenario === 'configured-rate-runtime-failure') state.faults.rateFails = true;
  if (scenario === 'long-source') project.playlists[0].items[0].source = 'C:/MockMedia/Editorial/素材归档/' + 'Very long mixed-language 路径/'.repeat(8) + 'Opening original 源视频.mp4';
  if (scenario === 'long-alias') {
    project.playlists[0].alias = 'Very long Playlist 名称 · Precision Editorial '.repeat(6);
    project.playlists[0].items[0].alias = 'Very long authored Item 独立名称 '.repeat(8);
  }
  if (scenario === 'custom-aspect') {
    project.playlists[0].items[0].presentation.mediaPresentation = { mode: 'custom', customAspect: { numerator: 8, denominator: 5 } };
  }
  if (scenario === 'active-image-duration') {
    state.selectedPlaylistId = state.activePlaylistId = 'playlist-b'; state.selectedItemId = state.activeItemId = 'b-image';
    state.runtime = { time: 8, paused: true, halted: false, rate: 1, volume: 1, items: structuredClone(project.playlists[1].items) };
  }
  if (scenario.startsWith('visual-')) {
    state.runtime!.paused = true; state.runtime!.rate = 1.3;
    state.faults.playback = scenario === 'visual-prepare-failure' ? 'rebuild-prepare'
      : scenario === 'visual-bind-failure' ? 'rebuild-bind'
      : scenario === 'visual-seek-failure' ? 'restore-seek'
      : scenario === 'visual-rollback-failure' ? 'rollback-bind' : 'rollback-seek';
  }
  function active(playlistId: string, itemId: string) {
    const playlist = project.playlists.find(entry => entry.id === playlistId)!;
    const target = playlist.items.find(entry => entry.id === itemId)!;
    state.activePlaylistId = playlistId; state.activeItemId = itemId;
    state.runtime = { time: 8, paused: false, halted: false, rate: target.kind === 'video' ? target.rate : 1,
      volume: Math.fround(Math.fround(playlist.masterVolume) * (target.kind === 'video' ? target.mediaGain : 1)), items: structuredClone(playlist.items) };
  }
  if (scenario.startsWith('playback-') && scenario !== 'playback-failure') {
    if (['playback-list-loop','playback-disabled','playback-natural'].includes(scenario)) {
      project.playlists[1].items.push(item('b-third','B third',1));
      project.playlistMode = 'list-loop'; active('playlist-a','a-scene');
      state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = 'b-third';
    }
    if (scenario === 'playback-repeat-project') active('playlist-a','a-scene');
    if (scenario === 'playback-repeat-item') { project.playlistMode = 'list-loop'; project.playlists[0].itemMode = 'repeat-current'; }
    if (scenario === 'playback-random') {
      project.playlistMode = 'random'; project.playlists[0].itemMode = 'random';
      state.sequence = createController(42,[1,1,0,0,1,0],[1,1,0,1,0,0]);
    }
    if (scenario === 'playback-empty') { state.runtime = null; state.activePlaylistId = state.activeItemId = null; }
    if (scenario === 'playback-no-selection') state.selectedPlaylistId = state.selectedItemId = null;
    if (scenario === 'playback-paused') state.runtime!.paused = true;
    if (scenario === 'playback-single-item') {
      project.playlists.slice(1).forEach(playlist => { playlist.enabled = false; });
      project.playlists[0].items.slice(1).forEach(entry => { entry.enabled = false; });
    }
    if (scenario === 'playback-end-project') { project.playlistMode = 'list-loop'; active('playlist-b','b-image'); }
    if (scenario === 'playback-natural') state.runtime!.time = 107;
    if (scenario === 'playback-no-target') project.playlists.forEach(playlist => { playlist.enabled = false; });
    if (scenario === 'playback-masters') { state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = null; }
    if (scenario === 'playback-muted') {
      project.playlists[0].masterVolume = .7; project.playlists[0].items[0].mediaGain = .5;
      active('playlist-a','a-opening'); state.outputMuted = true;
    }
    if (scenario === 'playback-rate') state.runtime!.rate = 1.25;
    if (scenario === 'playback-long-alias') {
      project.playlists[0].alias = 'Playlist 编辑中的长名称 Precision Editorial '.repeat(8);
      project.playlists[0].items[0].alias = 'Item 独立 occurrence 长名称 '.repeat(10);
    }
    if (scenario === 'playback-long-time') {
      project.playlists[0].items[0].source = 'C:/MockMedia/Long-video.mp4'; project.playlists[0].items[0].duration = 7200;
      active('playlist-a','a-opening'); state.runtime!.time = 3661.9;
    }
    if (scenario === 'playback-switch-failure' || scenario === 'playback-natural-failure') {
      state.faults.navigation = 'bind'; if (scenario === 'playback-natural-failure') state.runtime!.time = 23;
    }
    if (['playback-cross-failure','playback-cross-rollback','playback-prepare-failure'].includes(scenario)) {
      project.playlistMode = 'list-loop'; active('playlist-a','a-scene');
      state.faults.navigation = scenario === 'playback-cross-failure' ? 'bind' : scenario === 'playback-cross-rollback' ? 'rollback' : 'prepare';
    }
  }
  if (timelineScenarios.some(([key]) => key === scenario)) {
    if (scenario === 'timeline-valid-image') { state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = 'b-image'; active('playlist-b', 'b-image'); }
    if (scenario === 'timeline-no-media') { state.runtime = null; state.activePlaylistId = state.activeItemId = null; }
    if (scenario === 'timeline-zero-duration') state.runtime!.items[0].duration = 0;
    if (scenario === 'timeline-invalid-duration') state.runtime!.items[0].duration = NaN;
    if (scenario === 'selected-not-active' || scenario.startsWith('right-edge-')) {
      project.playlists[1].items.push(item('b-third', 'B third', 1)); project.playlistMode = 'list-loop';
      active('playlist-a', 'a-scene'); state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = 'b-third';
    }
    if (scenario.startsWith('seek-')) {
      project.playlists[0].masterVolume = .7; project.playlists[0].items[0].mediaGain = .5;
      active('playlist-a', 'a-opening'); state.runtime!.paused = true; state.runtime!.rate = 1.25; state.outputMuted = true;
      if (scenario === 'seek-failure') state.faults.seekFails = true;
    }
    if (scenario === 'hover-generation-change') project.playlists[0].itemMode = 'repeat-current';
    state.files[EDITORIAL_PATH] = { kind: 'project', project: structuredClone(state.project) };
  }
  if (presentationScenarios.some(([key]) => key === scenario)) {
    const ratios: Record<string, [number,number]> = { 'canvas-16-10':[16,10], 'canvas-4-3':[4,3], 'canvas-1-1':[1,1], 'canvas-ultrawide':[32,9], 'canvas-portrait':[9,16] };
    if (ratios[scenario]) project.canvasAspect = ratios[scenario];
    if (scenario === 'canvas-16-9-display-16-10') state.display!.enumeration.displays[0].pixelHeight = 1600;
    if (scenario === 'display-secondary') {
      state.display = createSessionDisplay('mock-persistent-display-b'); project.displayOwner = state.display.boundId;
      state.view = createSessionView(state.display.boundId, { highlight: false, backgrounds: { [MOCK_DISPLAY_OWNER]:'cover', [state.display.boundId]:'reveal' } });
    }
    if (scenario === 'display-long-name') {
      const id = 'mock-persistent-route-' + 'opaque-target-device-path-'.repeat(12);
      state.display = createSessionDisplay(id); state.display.enumeration.displays[0] = { id, displayNumber: 7, friendlyName:'Studio 校色显示器 · Very long friendly display name '.repeat(7), pixelWidth:7680, pixelHeight:4320, primary:false };
      project.displayOwner = id; state.view = createSessionView(id);
    }
    const available = structuredClone(state.display!.enumeration);
    state.previewMock = { returnEnumeration: available };
    const absent = { status:'success' as const, displays: available.displays.filter(display=>display.id!==state.display!.boundId) };
    if (scenario === 'preview-tiny') state.previewMock.viewport = { width:8, height:8 };
    if (scenario === 'preview-zero') state.previewMock.viewport = { width:0, height:0 };
    if (scenario === 'display-unavailable') state.display = displayTransition(state.display!, { type:'enumerate', result:absent });
    if (scenario === 'enumeration-failed') state.display = displayTransition(state.display!, { type:'enumerate', result:{ status:'api-failure', displays:[] } });
    if (scenario === 'identify-failure') state.previewMock.identifyStatus = 'api-failure';
    if (scenario === 'identify-resolution-loss') { state.previewMock.identifyStatus = 'unresolved'; state.previewMock.identifyRefresh = absent; }
    if (['deployed','retained-display-lost','retained-enumeration-failed','undeploy-failure'].includes(scenario)) state.display = displayTransition(state.display!, { type:'deploy' });
    if (scenario === 'deploy-failure') state.display!.faults.hostCreate = 'unsupported-shell-topology';
    if (scenario === 'deploy-partial-failure') { state.display!.faults.attach = true; state.display!.faults.destroy = true; }
    if (scenario === 'retained-display-lost') state.display = displayTransition(state.display!, { type:'native-event', result:absent });
    if (scenario === 'retained-enumeration-failed') state.display = displayTransition(state.display!, { type:'native-event', result:{status:'api-failure',displays:[]} });
    if (scenario === 'undeploy-failure') state.display!.faults.detach = true;
    if (scenario === 'deploy-empty') {
      project.playlists = []; state.runtime = null; state.selectedPlaylistId = state.selectedItemId = state.activePlaylistId = state.activeItemId = null;
    }
    if (scenario === 'highlight-on') { state.view!.highlighted = true; state.view!.persisted.highlight = true; }
    if (scenario === 'background-reveal') { state.view!.background = 'reveal'; state.view!.persisted.backgrounds[state.display!.boundId] = 'reveal'; }
    if (scenario === 'background-save-failure') state.view!.backgroundSaveFails = true;
    state.files[EDITORIAL_PATH] = { kind:'project', project:structuredClone(project) };
    state.files[STUDIO_PATH] = { kind:'project', project:{...studioProject(),displayOwner:project.displayOwner} };
  }
  if (inspectorScenarios.some(([key]) => key === scenario) || playbackScenarios.some(([key]) => key === scenario)) {
    state.files[EDITORIAL_PATH] = { kind: 'project', project: structuredClone(state.project) };
  }
  if(remainingScenarios.some(([key])=>key===scenario)){
    if(['playlist-middle','selected-playlist-active-other'].includes(scenario)){state.selectedPlaylistId='playlist-b';state.selectedItemId=null;}
    if(scenario==='playlist-last'){state.selectedPlaylistId='playlist-d';state.selectedItemId=null;}
    if(['item-middle','selected-item-active-other'].includes(scenario))state.selectedItemId='a-scene';
    if(scenario==='item-last')state.selectedItemId='a-disabled';
    if(scenario.startsWith('language-'))state.languageMock={committed:scenario==='language-zh'||scenario==='language-catalog-failure'?'zh_CN':scenario==='language-invalid'?'unsupported':'en',catalogAvailable:scenario!=='language-catalog-failure',readFails:scenario==='language-read-failure'};
    if(scenario.startsWith('dirty-shortcut-')||scenario==='untitled-shortcut-save'){state.document.revision++;if(scenario==='untitled-shortcut-save')state.document.path=null;}
    if(scenario.startsWith('hide-')){
      const initial=createSessionRole();
      if(scenario==='hide-no-restore')initial.hide.recovery.recoveryQualified=false;
      if(scenario==='hide-close-pending')initial.lifecycle.closePending=true;
      state.hideMock={initial,race:scenario==='hide-stale-permit'?'show':scenario==='hide-denied-permit'?'denied':scenario==='hide-recovery-race'?'recovery':scenario==='hide-expired-permit'?'expired':undefined};
    }
    if(scenario==='empty-playlist-play'){state.selectedPlaylistId='playlist-c';state.selectedItemId=null;}
    if(scenario==='playback-sync-warning')state.faults.playback='rebuild-prepare';
    if(scenario==='playback-position-warning')state.faults.playback='restore-seek';
    state.files[EDITORIAL_PATH]={kind:'project',project:structuredClone(project)};
  }
  if(scenario==='gap-duplicate-playlists'){
    project.playlists[0].alias=project.playlists[1].alias='Playlist';
    state.selectedPlaylistId='playlist-b';state.selectedItemId=null;
  }
  if(scenario==='gap-add-rollback-seek'||scenario==='gap-remove-rollback-seek'){
    state.faults.playback='rollback-seek';state.selectedItemId=scenario==='gap-remove-rollback-seek'?'a-scene':null;
    project.playlists[0].masterVolume=.7;project.playlists[0].items[0].mediaGain=.5;
    state.runtime={...state.runtime!,time:8,paused:true,rate:1.7,volume:Math.fround(Math.fround(.7)*.5),items:structuredClone(project.playlists[0].items)};
    state.outputMuted=true;
  }
  if(scenario==='gap-close-pending-deployed'||scenario==='gap-ending-ready'||scenario==='gap-ending-deployed'){
    const initial=createSessionRole();
    if(scenario!=='gap-ending-ready')state.display=displayTransition(state.display!,{type:'deploy'});
    initial.lifecycle={closePending:scenario==='gap-close-pending-deployed',ending:scenario!=='gap-close-pending-deployed'};
    state.hideMock={initial};
  }
  if(scenario==='gap-hover-geometry'){
    state.selectedPlaylistId='playlist-b';state.selectedItemId='b-image';
    const visual=project.playlists[0].items[0].presentation;
    visual.mediaPresentation={mode:'custom',customAspect:{numerator:4,denominator:3}};
    visual.crop={x:.1,y:.2,width:.7,height:.6};
    visual.layout={positionX:.2,positionY:.15,scaleX:.7,scaleY:.7};
    state.runtime!.items=structuredClone(project.playlists[0].items);
    project.canvasAspect=[1,1];
  }
  const floatingAspects: Partial<Record<SessionScenario, [number, number]>> = {
    'hover-floating-16-9': [16, 9], 'hover-floating-16-10': [16, 10],
    'hover-floating-4-3': [4, 3], 'hover-floating-1-1': [1, 1],
    'hover-floating-ultrawide': [32, 9], 'hover-floating-portrait': [9, 16],
    'hover-floating-extreme-wide': [1000, 1], 'hover-floating-extreme-portrait': [1, 1000],
  };
  if (floatingAspects[scenario]) project.canvasAspect = floatingAspects[scenario]!;
  if (scenario === 'hover-selected-not-active') { state.selectedPlaylistId = 'playlist-b'; state.selectedItemId = 'b-image'; }
  if (scenario.startsWith('playlist-volume-')) {
    project.playlists.forEach((playlist, index) => { playlist.masterVolume = [.72, .5, .4, .02][index]; });
    state.selectedItemId = null;
    if (scenario === 'playlist-volume-selected-vs-target') {
      state.activePlaylistId = 'playlist-b'; state.activeItemId = 'b-opening';
      state.runtime!.items = structuredClone(project.playlists[1].items);
    }
    if (scenario === 'playlist-volume-active-vs-target') state.selectedPlaylistId = 'playlist-b';
    if (scenario === 'playlist-volume-duplicate-alias') project.playlists[0].alias = project.playlists[1].alias = 'Scene';
    if (scenario === 'playlist-volume-zero' || scenario === 'playlist-volume-no-history') project.playlists[0].masterVolume = 0;
    if (scenario === 'playlist-volume-last-nonzero') project.playlists[0].masterVolume = .4;
    if (scenario === 'playlist-volume-wheel') project.playlists[0].masterVolume = .5;
    if (scenario === 'playlist-volume-bottom-row') for (let index = 0; index < 20; index++) project.playlists.push({ id: 'volume-extra-' + index, alias: 'Playlist ' + (index + 5), enabled: true, masterVolume: .5, itemMode: 'list-loop', items: [] });
    state.runtime!.volume = Math.fround(project.playlists.find(playlist => playlist.id === state.activePlaylistId)!.masterVolume);
    state.files[EDITORIAL_PATH] = { kind: 'project', project: structuredClone(project) };
  }
  rebasePlaybackToActive(state);
  return state;
}
