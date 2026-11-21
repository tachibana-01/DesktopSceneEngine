import { useEffect, useId, useLayoutEffect, useRef, useState, type KeyboardEvent } from 'react';
import { createPortal } from 'react-dom';
import { useSessionText } from './sessionLanguage';
import { documentShortcut } from './sessionShortcuts.ts';
import './sessionSettings.css';

export function SessionSettings({ onResetLayout, resetAvailable = true, highlighted, onHighlight, highlightAvailable = true, native = false, background, onBackground }: {
  onResetLayout: () => void; resetAvailable?: boolean;
  highlighted: boolean; onHighlight: (enabled: boolean) => void; highlightAvailable?: boolean;
  native?:boolean; background?:'cover'|'reveal'; onBackground?:(mode:'cover'|'reveal')=>void;
}) {
  const t = useSessionText(), menuId = useId(), submenuId = useId(), tooltipId = useId();
  const trigger = useRef<HTMLButtonElement>(null), main = useRef<HTMLDivElement>(null);
  const submenu = useRef<HTMLDivElement>(null), parent = useRef<HTMLButtonElement>(null);
  const closeTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const [open, setOpen] = useState(false), [subOpen, setSubOpen] = useState(false);
  const [anchor, setAnchor] = useState({ left: 0, top: 0 });
  const [subAnchor, setSubAnchor] = useState({ left: 0, top: 0, maxWidth: 360 });
  const unavailable = t(native ? 'The View controls are currently unavailable.' : 'Unavailable until a production Session is connected');
  function retainSubmenu() { clearTimeout(closeTimer.current); }
  function close(restoreFocus = false) {
    retainSubmenu(); setSubOpen(false); setOpen(false);
    if (restoreFocus) trigger.current?.focus({ preventScroll: true });
  }
  function showSubmenu() {
    retainSubmenu(); setSubOpen(true);
  }
  function leaveSubmenu() {
    retainSubmenu(); closeTimer.current = setTimeout(() => setSubOpen(false), 120);
  }
  useEffect(() => () => clearTimeout(closeTimer.current), []);
  useLayoutEffect(() => {
    if (!open) return;
    const inside = (target: EventTarget | null) => target instanceof Node &&
      [trigger.current, main.current, submenu.current].some(element => element?.contains(target));
    const outside = (event: Event) => { if (!inside(event.target)) close(); };
    const blur = () => close();
    const keyboard = (event: globalThis.KeyboardEvent) => {
      if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); close(true); }
      else if (documentShortcut(event)) close();
    };
    main.current?.querySelector<HTMLElement>('[role^=menuitem]:not([aria-disabled=true])')?.focus({ preventScroll: true });
    document.addEventListener('pointerdown', outside, true); document.addEventListener('contextmenu', outside, true);
    document.addEventListener('focusin', outside, true); window.addEventListener('blur', blur);
    window.addEventListener('keydown', keyboard, true);
    return () => {
      document.removeEventListener('pointerdown', outside, true); document.removeEventListener('contextmenu', outside, true);
      document.removeEventListener('focusin', outside, true); window.removeEventListener('blur', blur);
      window.removeEventListener('keydown', keyboard, true);
    };
  }, [open]);
  useLayoutEffect(() => {
    if (!open) return;
    const measure = () => {
      const rect = trigger.current?.getBoundingClientRect(), menu = main.current, row = parent.current;
      if (!rect?.width || !menu || !row) { close(); return; }
      const menuWidth = menu.getBoundingClientRect().width;
      const maxWidth = Math.max(1, window.innerWidth - menuWidth - 14);
      const childWidth = subOpen ? Math.min(submenu.current?.getBoundingClientRect().width ?? 0, maxWidth) : 0;
      // Local fit for these two surfaces only. Keep the child on the right;
      // at narrow widths shift the pair and wrap its labels, never the workspace.
      const left = Math.max(6, Math.min(rect.left, window.innerWidth - menuWidth - (childWidth ? childWidth + 2 : 0) - 6));
      const top = rect.bottom + 6;
      setAnchor(old => old.left === left && old.top === top ? old : { left, top });
      const child = { left: left + menuWidth + 2, top: row.getBoundingClientRect().top - 4, maxWidth };
      setSubAnchor(old => old.left === child.left && old.top === child.top && old.maxWidth === maxWidth ? old : child);
    };
    measure();
    const observer = new ResizeObserver(measure);
    for (const element of [trigger.current, main.current, submenu.current]) if (element) observer.observe(element);
    window.addEventListener('resize', measure); window.addEventListener('scroll', measure, true);
    return () => { observer.disconnect(); window.removeEventListener('resize', measure); window.removeEventListener('scroll', measure, true); };
  }, [open, subOpen]);
  function navigate(event: KeyboardEvent<HTMLDivElement>, child = false) {
    if (event.key === 'Tab') { close(); return; }
    if (child && event.key === 'ArrowLeft') {
      event.preventDefault(); event.stopPropagation();
      // Focus first, then close: the parent's focus handler normally opens it.
      parent.current?.focus(); setSubOpen(false); return;
    }
    if (!child && event.key === 'ArrowRight' && document.activeElement === parent.current) {
      event.preventDefault(); showSubmenu();
      requestAnimationFrame(() => submenu.current?.querySelector<HTMLElement>('[role^=menuitem]')?.focus()); return;
    }
    if (!['ArrowUp', 'ArrowDown', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    // Unavailable items remain focusable so their reason is accessible; no
    // activation is possible and no false unchecked state is announced.
    const items = [...event.currentTarget.querySelectorAll<HTMLButtonElement>('[role^=menuitem]')];
    const index = items.indexOf(document.activeElement as HTMLButtonElement);
    const next = event.key === 'Home' ? 0 : event.key === 'End' ? items.length - 1
      : (index + (event.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length;
    items[next]?.focus();
  }
  return <>
    <button ref={trigger} type="button" className="session-settings-trigger" data-session-settings-trigger
      aria-label={t('Settings')} aria-haspopup="menu" aria-expanded={open} aria-controls={open ? menuId : undefined}
      aria-describedby={!open ? tooltipId : undefined} onClick={() => open ? close() : setOpen(true)}
      onKeyDown={event => { if (event.key === 'ArrowDown') { event.preventDefault(); setOpen(true); } }}>
      <svg width="17" height="17" viewBox="0 0 24 24" fill="none" aria-hidden="true">
        <path d="m9.7 3-.6 2.1-1.8 1-2.1-.5L3 9.4l1.5 1.6v2L3 14.6l2.2 3.8 2.1-.5 1.8 1 .6 2.1h4.6l.6-2.1 1.8-1 2.1.5 2.2-3.8-1.5-1.6v-2L21 9.4l-2.2-3.8-2.1.5-1.8-1-.6-2.1z" />
        <circle cx="12" cy="12" r="3" />
      </svg>
      {!open && <span className="control-tooltip" id={tooltipId} role="tooltip">{t('Settings')}</span>}
    </button>
    {open && createPortal(<>
      <div ref={main} id={menuId} className="session-settings-menu" role="menu" aria-label={t('Settings')}
        data-session-settings-menu style={anchor} onKeyDown={event => navigate(event)}>
        <button type="button" role="menuitem" tabIndex={-1} data-settings-command="reset-layout"
          aria-disabled={!resetAvailable || undefined} onPointerEnter={() => { retainSubmenu(); setSubOpen(false); }}
          onFocus={() => { retainSubmenu(); setSubOpen(false); }}
          onClick={() => { if (resetAvailable) { onResetLayout(); close(true); } }}>{t('Reset Layout')}</button>
        <button type="button" role="menuitemcheckbox" tabIndex={-1} aria-checked={highlighted}
          aria-disabled={!highlightAvailable || undefined} data-settings-command="highlight" data-highlight-authority={native ? 'native' : 'prototype-projection'}
          onPointerEnter={() => { retainSubmenu(); setSubOpen(false); }}
          onFocus={() => { retainSubmenu(); setSubOpen(false); }}
          onClick={() => { if (highlightAvailable) { onHighlight(!highlighted); close(true); } }}>
          <span>{t('Highlight Canvas Boundary')}</span><span aria-hidden="true">{highlighted ? '✓' : ''}</span>
        </button>
        <button ref={parent} type="button" role="menuitem" tabIndex={-1} aria-haspopup="menu"
          aria-expanded={subOpen} aria-controls={subOpen ? submenuId : undefined} data-settings-submenu-trigger
          onPointerEnter={showSubmenu} onPointerLeave={leaveSubmenu} onFocus={showSubmenu}
          onClick={() => { showSubmenu(); requestAnimationFrame(() => submenu.current?.querySelector<HTMLElement>('[role^=menuitem]')?.focus()); }}>
          <span>{t('Desktop Background')}</span><span aria-hidden="true">›</span>
        </button>
      </div>
      {subOpen && <div ref={submenu} id={submenuId} className="session-settings-menu session-settings-submenu"
        role="menu" aria-label={t('Desktop Background')} data-session-settings-submenu data-state-authority={native ? 'native' : 'unavailable'}
        style={subAnchor} onPointerEnter={retainSubmenu} onPointerLeave={leaveSubmenu} onKeyDown={event => navigate(event, true)}>
        {([['cover','Cover Entire Desktop Background'],['reveal','Reveal Wallpaper in Uncovered Areas']] as const).map(([mode,label]) =>
          <button key={mode} type="button" role={native ? 'menuitemradio' : 'menuitem'} aria-checked={native ? background === mode : undefined} tabIndex={-1} aria-disabled={!onBackground || undefined}
            data-settings-unavailable={onBackground ? undefined : 'desktop-background'} title={onBackground ? undefined : unavailable} aria-description={onBackground ? undefined : unavailable}
            onClick={() => { if (onBackground) { onBackground(mode); close(true); } }}>{t(label)}</button>)}
      </div>}
    </>, document.body)}
  </>;
}
