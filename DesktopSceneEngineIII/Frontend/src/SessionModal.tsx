import { useLayoutEffect, useRef, type KeyboardEvent, type ReactNode } from 'react';
import { useSessionText } from './sessionLanguage';
import {useNativePreviewOverlay} from './NativePreviewOverlayScope';

export function SessionModal({ title, children, onClose, initialFocus, className = '', position, dismissible = true }: {
  title: string; children: ReactNode; onClose: () => void; initialFocus?: string; className?: string; position?: { x: number; y: number }; dismissible?: boolean;
}) {
  const t = useSessionText();
  const previewOverlay=useNativePreviewOverlay(position?'floating':'modal');
  const dialog = useRef<HTMLDialogElement>(null);
  useLayoutEffect(() => {
    const element = dialog.current;
    const opener = document.activeElement;
    element?.showModal();
    function place() {
      if (!position || !element) return;
      const width = element.getBoundingClientRect().width, height = element.getBoundingClientRect().height;
      element.style.left = Math.max(12, Math.min(position.x, innerWidth - width - 12)) + 'px';
      element.style.top = Math.max(12, Math.min(position.y, innerHeight - height - 12)) + 'px';
    }
    place(); window.addEventListener('resize', place);
    const target = element?.querySelector<HTMLElement>(initialFocus ?? 'button:not(:disabled), input:not(:disabled), select:not(:disabled)');
    target?.focus();
    if (target instanceof HTMLInputElement && target.type === 'text') target.select();
    return () => {
      element?.close();
      window.removeEventListener('resize', place);
      const fallback = document.querySelector<HTMLElement>('.session-project-trigger');
      if (opener instanceof HTMLElement && opener.isConnected) opener.focus({ preventScroll: true });
      else fallback?.focus({ preventScroll: true });
    };
  }, []);
  function key(event: KeyboardEvent<HTMLDialogElement>) {
    // Some browser CloseWatcher paths deliver a non-cancelable cancel event.
    // Consume Escape before that native close request for required acknowledgement.
    if (!dismissible && event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); return; }
    const controls = [...event.currentTarget.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled), select:not(:disabled)')];
    const first = controls[0], last = controls.at(-1);
    if (event.key === 'Tab') {
      if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
      if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
    }
    if (className.includes('session-command-menu') && ['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
      event.preventDefault();
      const index = controls.indexOf(document.activeElement as HTMLElement);
      const next = event.key === 'Home' ? 0 : event.key === 'End' ? controls.length - 1
        : (index + (event.key === 'ArrowDown' ? 1 : -1) + controls.length) % controls.length;
      controls[next]?.focus();
    }
  }
  return <dialog ref={dialog} aria-label={title} className={'session-modal ' + className} data-native-preview-overlay={previewOverlay}
    onKeyDown={key} onCancel={event => { event.preventDefault(); if (dismissible) onClose(); }}
    onClick={event => {
      const bounds = event.currentTarget.getBoundingClientRect();
      if (dismissible && event.target === event.currentTarget && (event.clientX < bounds.left || event.clientX > bounds.right || event.clientY < bounds.top || event.clientY > bounds.bottom)) onClose();
    }}>
    {!className.includes('session-command-menu') && <div className="session-modal-heading"><h2>{title}</h2>
      {dismissible && <button type="button" className="session-dialog-close" data-dialog-close aria-label={t('Close %1').replace('%1', () => title)} onClick={onClose}>×</button>}
    </div>}
    {children}
  </dialog>;
}
