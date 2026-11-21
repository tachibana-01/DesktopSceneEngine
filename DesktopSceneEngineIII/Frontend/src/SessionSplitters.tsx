import { useCallback, useLayoutEffect, useRef, useState } from 'react';
import type { CSSProperties, KeyboardEvent, PointerEvent } from 'react';
import { createSessionLayout, INSPECTOR_MINIMUM, LIBRARY_MINIMUM, moveSessionSplitter, resetSessionLayout, resizeSessionLayout, SPLITTER_WIDTH } from './sessionLayoutModel.ts';
import type { SessionLayoutPreset, SessionLayoutState, SessionSplitter } from './sessionLayoutModel.ts';
import './sessionLayout.css';
import {translateSession} from './sessionLanguage';

function sameLayout(a: SessionLayoutState, b: SessionLayoutState) {
  return a.width === b.width && a.previewMinimum === b.previewMinimum && a.preferredLeft === b.preferredLeft && a.preferredRight === b.preferredRight && a.left === b.left && a.preview === b.preview && a.right === b.right && a.mode === b.mode;
}

/** Qt derives the middle minimum from its current header/layout minimumSizeHint.
 * Measure this projection's fixed header contents instead of inventing a fixed
 * product minimum. The Display label remains shrinkable, just as in production. */
function previewContentMinimum(workspace: HTMLElement): number {
  const header = workspace.querySelector<HTMLElement>('.preview-context');
  if (!header) return 1;
  const style = getComputedStyle(header);
  const padding = parseFloat(style.paddingLeft) + parseFloat(style.paddingRight);
  const facts = header.querySelector<HTMLElement>('.preview-facts-row');
  const factsMinimum = facts ? Array.from(facts.children).reduce((sum, child) => sum + child.getBoundingClientRect().width, 0) + Math.max(0, facts.children.length - 1) * (parseFloat(getComputedStyle(facts).columnGap) || 0) : 0;
  const actions = header.querySelector<HTMLElement>('.preview-actions');
  const displayRow = header.querySelector<HTMLElement>('.preview-display-row');
  const actionsMinimum = (actions?.getBoundingClientRect().width ?? 0) + (displayRow ? parseFloat(getComputedStyle(displayRow).columnGap) || 0 : 0);
  return Math.ceil(Math.max(9, factsMinimum, actionsMinimum) + padding);
}

export function useSessionLayout(resetEpoch: unknown, preset?: SessionLayoutPreset,language:'en'|'zh_CN'='en') {
  const t=(text:string)=>translateSession(language,text);
  const [workspace, setWorkspace] = useState<HTMLElement | null>(null);
  const workspaceRef = useCallback((element: HTMLElement | null) => setWorkspace(current => current === element ? current : element), []);
  const [layout, setLayout] = useState(() => createSessionLayout(0, 1, preset));
  const [dragging, setDragging] = useState<SessionSplitter | null>(null);
  const pointer = useRef<{ id: number; node: HTMLDivElement; splitter: SessionSplitter; offset: number } | null>(null);
  const cancelDrag = useCallback(() => {
    const active = pointer.current;
    if (!active) return;
    pointer.current = null;
    setDragging(null);
    if (active.node.hasPointerCapture(active.id)) active.node.releasePointerCapture(active.id);
  }, []);
  useLayoutEffect(() => {
    if (!workspace) return;
    const width = () => Math.floor(workspace.getBoundingClientRect().width);
    setLayout(createSessionLayout(width(), previewContentMinimum(workspace), preset));
    const measure = () => setLayout(current => {
      const next = resizeSessionLayout(current, width(), previewContentMinimum(workspace));
      return sameLayout(current, next) ? current : next;
    });
    const observer = new ResizeObserver(measure);
    observer.observe(workspace);
    const header = workspace.querySelector<HTMLElement>('.preview-context');
    if (header) observer.observe(header);
    // Committed retranslation can change min-content without changing height.
    const mutation = new MutationObserver(measure);
    if (header) mutation.observe(header, { childList: true, subtree: true, characterData: true });
    const visibility = () => { if (document.hidden) cancelDrag(); };
    window.addEventListener('blur', cancelDrag);
    window.addEventListener('pagehide', cancelDrag);
    document.addEventListener('visibilitychange', visibility);
    return () => {
      observer.disconnect(); mutation.disconnect();
      window.removeEventListener('blur', cancelDrag);
      window.removeEventListener('pagehide', cancelDrag);
      document.removeEventListener('visibilitychange', visibility);
      cancelDrag();
    };
  }, [workspace, resetEpoch, preset, cancelDrag]);
  useLayoutEffect(() => {
    if (layout.mode !== 'three-column') cancelDrag();
  }, [layout.mode, cancelDrag]);

  const reset = useCallback(() => { cancelDrag(); setLayout(current => resetSessionLayout(current)); }, [cancelDrag]);
  const move = (splitter: SessionSplitter, position: number) => setLayout(current => moveSessionSplitter(current, splitter, position));
  function start(event: PointerEvent<HTMLDivElement>, splitter: SessionSplitter) {
    if (event.button !== 0 || pointer.current || layout.mode !== 'three-column') return;
    event.preventDefault(); event.currentTarget.focus();
    const node = event.currentTarget;
    node.setPointerCapture(event.pointerId);
    pointer.current = { id: event.pointerId, node, splitter, offset: event.clientX - node.getBoundingClientRect().left };
    setDragging(splitter);
  }
  function drag(event: PointerEvent<HTMLDivElement>) {
    const active = pointer.current;
    if (!workspace || !active || active.id !== event.pointerId || active.node !== event.currentTarget) return;
    if (!(event.buttons & 1)) { cancelDrag(); return; }
    move(active.splitter, event.clientX - workspace.getBoundingClientRect().left - active.offset);
  }
  function finish(event: PointerEvent<HTMLDivElement>) {
    const active = pointer.current;
    if (active?.id === event.pointerId && active.node === event.currentTarget) cancelDrag();
  }
  function keyboard(event: KeyboardEvent<HTMLDivElement>, splitter: SessionSplitter) {
    if (layout.mode !== 'three-column' || !['ArrowLeft', 'ArrowRight'].includes(event.key)) return;
    // Focus-local separator accessibility, not an application/transport shortcut.
    event.preventDefault(); event.stopPropagation();
    const position = splitter === 'left' ? layout.left : layout.width - layout.right - SPLITTER_WIDTH;
    move(splitter, position + (event.key === 'ArrowRight' ? 10 : -10));
  }
  const available = layout.width - 2 * SPLITTER_WIDTH;
  const minimumPreview = Math.min(layout.previewMinimum, Math.max(0, available - LIBRARY_MINIMUM - INSPECTOR_MINIMUM));
  const libraryWidth = layout.mode === 'three-column' ? layout.left : layout.preferredLeft;
  const inspectorWidth = layout.mode === 'three-column' ? layout.right : layout.preferredRight;
  // Keep the ordinary gap paint present. CSS controls participation and position
  // with the same responsive topology as the panels, even before measurement.
  const separators = (['left', 'right'] as const).map(splitter => <div key={splitter}
    className="session-splitter" data-layout-splitter={splitter} role="separator" aria-orientation="vertical"
    aria-label={t(splitter === 'left' ? 'Resize Library and Preview' : 'Resize Preview and Inspector')} tabIndex={0}
    aria-valuemin={splitter === 'left' ? LIBRARY_MINIMUM : INSPECTOR_MINIMUM}
    aria-valuemax={Math.max(0, available - minimumPreview - (splitter === 'left' ? INSPECTOR_MINIMUM : LIBRARY_MINIMUM))}
    aria-valuenow={splitter === 'left' ? libraryWidth : inspectorWidth}
    onPointerDown={event => start(event, splitter)} onPointerMove={drag} onPointerUp={finish} onPointerCancel={finish}
    onLostPointerCapture={finish} onKeyDown={event => keyboard(event, splitter)} />);
  // CSS owns responsive panel participation and track count together. These
  // values only preserve splitter widths while ResizeObserver catches up.
  const workspaceStyle = {
    '--session-library-width': `${libraryWidth}px`,
    '--session-inspector-width': `${inspectorWidth}px`,
  } as CSSProperties;
  const attributes = { 'data-layout-mode': layout.mode, 'data-layout-left': layout.left, 'data-layout-preview': layout.preview, 'data-layout-right': layout.right,
    'data-layout-preview-minimum': layout.previewMinimum, 'data-layout-dragging': dragging ?? '', 'data-layout-persistence': 'none' };
  return { workspaceRef, workspaceStyle, attributes, separators, reset, layout };
}
