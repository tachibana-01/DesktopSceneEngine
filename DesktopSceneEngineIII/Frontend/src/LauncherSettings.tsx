import { useLayoutEffect, useRef, useState, type KeyboardEvent } from 'react';
import type { Language } from './displayData';

export function LauncherSettings({ effective, failSave, onCommit, onClose }: {
  effective: Language; failSave: boolean; onCommit: (language: Language) => void; onClose: () => void;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  const language = useRef<HTMLSelectElement>(null);
  const pending = useRef<number | null>(null);
  const [staged, setStaged] = useState(effective);
  const [result, setResult] = useState('');
  const [saving, setSaving] = useState(false);
  useLayoutEffect(() => {
    const opener = document.activeElement;
    dialog.current?.showModal();
    language.current?.focus();
    return () => {
      if (pending.current !== null) window.clearTimeout(pending.current);
      dialog.current?.close();
      if (opener instanceof HTMLElement) opener.focus({ preventScroll: true });
    };
  }, []);
  function apply() {
    if (pending.current !== null) return;
    setSaving(true); setResult('Saving settings…');
    pending.current = window.setTimeout(() => {
      pending.current = null;
      if (failSave) { setSaving(false); setResult('Unable to save settings.'); }
      else { onCommit(staged); onClose(); }
    }, 650);
  }
  function containFocus(event: KeyboardEvent<HTMLDialogElement>) {
    if (event.key !== 'Tab') return;
    const controls = [...event.currentTarget.querySelectorAll<HTMLElement>('button:not(:disabled), select:not(:disabled)')];
    const first = controls[0], last = controls[controls.length - 1];
    if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
    else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
  }
  return <dialog ref={dialog} className="launcher-settings" aria-labelledby="launcher-settings-title"
    onKeyDown={containFocus} onCancel={event => { event.preventDefault(); onClose(); }}>
    <div className="settings-heading">
      <h2 id="launcher-settings-title">SETTINGS</h2>
      <button type="button" data-settings-close aria-label="Close settings" onClick={onClose}>
        <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="m3.5 3.5 9 9m0-9-9 9" /></svg>
      </button>
    </div>
    <label htmlFor="launcher-language">LANGUAGE</label>
    <select ref={language} id="launcher-language" value={staged} disabled={saving}
      onChange={event => { setStaged(event.target.value as Language); setResult(''); }}>
      <option value="en">English</option><option value="zh-CN">简体中文</option>
    </select>
    <p className="settings-effective">Effective language · {effective === 'en' ? 'English' : '简体中文'}</p>
    <p className="settings-result" role="status" aria-live="polite">{result || 'Changes take effect when applied.'}</p>
    <div className="settings-actions">
      <button type="button" data-settings-cancel onClick={onClose}>Cancel</button>
      <button type="button" data-settings-apply disabled={saving || staged === effective} onClick={apply}>{saving ? 'Saving…' : 'Apply'}</button>
    </div>
  </dialog>;
}
