import { useId, useState } from 'react';
import { SessionModal } from './SessionModal';
import { useSessionText } from './sessionLanguage';
import './sessionChoice.css';

// Qt connects activated rather than currentIndexChanged. Options must submit
// even when the user chooses the current option (not emitted by HTML change).
export function SessionChoice({ label, value, options, onValue, className = 'property-value inspector-choice', disabled = false }: { label: string; value: string; options: readonly (readonly [string, string])[]; onValue: (value: string) => void; className?: string; disabled?: boolean }) {
  const t = useSessionText();
  const id = useId();
  const [position, setPosition] = useState<{ x: number; y: number } | null>(null);
  function open(element: HTMLElement) { const r = element.getBoundingClientRect(); setPosition({ x: r.left, y: r.bottom + 4 }); }
  return <>
    <button type="button" className={"session-choice " + className} disabled={disabled} role="combobox" aria-label={t(label)} value={value}
      aria-haspopup="listbox" aria-expanded={Boolean(position)} aria-controls={position ? id : undefined}
      onClick={event => open(event.currentTarget)} onKeyDown={event => {
        if (event.key === 'ArrowDown' || event.key === 'ArrowUp') { event.preventDefault(); open(event.currentTarget); }
      }}><span>{t(options.find(([key]) => key === value)?.[1] ?? '—')}</span><span aria-hidden="true">⌄</span></button>
    {position && <SessionModal title={t(label)} position={position} className="session-command-menu inspector-choice-menu" initialFocus="[aria-selected=true]" onClose={() => setPosition(null)}>
      <div role="listbox" aria-label={t('%1 options').replace('%1', t(label))} id={id}>
        {options.map(([key, text]) => <button type="button" role="option" aria-selected={key === value} data-choice-value={key} key={key}
          onClick={() => { setPosition(null); onValue(key); }}><span>{t(text)}</span><span aria-hidden="true">{key === value ? '✓' : ''}</span></button>)}
      </div>
    </SessionModal>}
  </>;
}
