import { useLayoutEffect, useRef, type KeyboardEvent } from 'react';
import type { LauncherLanguage, LauncherLanguageProjection } from './ProductionLauncherFacade.ts';
import type { LauncherLanguageFeedback } from './ProductionLauncherController.ts';
import { launcherCopy, launcherLanguageName, launcherLanguageFeedback, launcherLocale } from './launcherLanguage.ts';

export function ProductionLauncherSettings({ language, draft, pending, feedback, onDraft, onCancel, onCommit }: {
  language: LauncherLanguageProjection; draft: LauncherLanguage | null; pending: boolean; feedback: LauncherLanguageFeedback | null;
  onDraft: (draft: LauncherLanguage) => void; onCancel: () => void; onCommit: (closeOnSuccess: boolean) => void;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  const select = useRef<HTMLSelectElement>(null);
  const copy = launcherCopy(language.effective);
  useLayoutEffect(() => {
    const opener = document.activeElement;
    const element = dialog.current;
    element?.showModal();
    select.current?.focus();
    return () => { element?.close(); if (opener instanceof HTMLElement && opener.isConnected) opener.focus({ preventScroll: true }); };
  }, []);
  function containFocus(event: KeyboardEvent<HTMLDialogElement>) {
    if (event.key !== 'Tab') return;
    const controls = [...event.currentTarget.querySelectorAll<HTMLElement>('button:not(:disabled), select:not(:disabled)')];
    const first = controls[0], last = controls.at(-1);
    if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
    else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
  }
  return <dialog ref={dialog} className="launcher-settings production-settings" data-production-settings lang={launcherLocale(language.effective)}
    data-language-pending={pending} data-draft-language={draft ?? ''} aria-labelledby="production-settings-title"
    onKeyDown={containFocus} onCancel={event => { event.preventDefault(); if (!pending) onCancel(); }}>
    <div className="settings-heading"><h2 id="production-settings-title">{copy.settings}</h2>
      <button type="button" data-settings-close aria-label={copy.closeSettings} disabled={pending} onClick={onCancel}>
        <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="m3.5 3.5 9 9m0-9-9 9" /></svg>
      </button>
    </div>
    <label htmlFor="production-launcher-language">{copy.language}</label>
    <select ref={select} id="production-launcher-language" data-settings-language value={draft ?? ''} disabled={pending}
      onChange={event => onDraft(event.target.value as LauncherLanguage)}>
      {draft === null && <option value="" disabled>{copy.committedUnavailable}</option>}
      <option value="en">English</option><option value="zh_CN">简体中文</option>
    </select>
    <p className="settings-effective">{copy.effective} · {launcherLanguageName(language.effective)}</p>
    <p className="settings-effective" data-settings-committed>{copy.committed} · {language.committed ? launcherLanguageName(language.committed) : copy.committedUnavailable}</p>
    <p className="settings-result" role="status" aria-live="polite">{pending ? copy.saving : launcherLanguageFeedback(feedback, language.effective)
      || (language.readStatus === 'Failure' ? copy.languageReadFailure
        : language.committed !== language.effective ? copy.translationFallback : copy.changesOnApply)}</p>
    <div className="settings-actions">
      <button type="button" data-settings-cancel disabled={pending} onClick={onCancel}>{copy.cancel}</button>
      <button type="button" data-settings-apply disabled={pending || draft === null} onClick={() => onCommit(false)}>{copy.apply}</button>
      <button type="button" data-settings-ok disabled={pending || draft === null} onClick={() => onCommit(true)}>{copy.ok}</button>
    </div>
  </dialog>;
}
