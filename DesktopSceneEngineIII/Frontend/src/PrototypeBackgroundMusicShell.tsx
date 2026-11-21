import { useEffect, useId, useLayoutEffect, useReducer, useRef, useState, type KeyboardEvent } from 'react';
import { createPortal } from 'react-dom';
import { TitleBar } from './ShellTitleBar';
import { backgroundMusicReducer, initialBackgroundMusic, projectNames, unavailableMusic,
  type MusicScopeAction, type MusicScopeState, type MusicTrack, type ProjectId } from './backgroundMusicState';
import { formatTime } from './sessionData';
import './backgroundMusic.css';

function TruncatedName({ name, keyboardFocus = false, focusable = false }: { name: string; keyboardFocus?: boolean; focusable?: boolean }) {
  const anchor = useRef<HTMLSpanElement>(null);
  const id = useId();
  const [point, setPoint] = useState<{ left: number; top: number } | null>(null);
  function show() {
    const rect = anchor.current?.getBoundingClientRect();
    const list = anchor.current?.closest('.bgm-track-list')?.getBoundingClientRect();
    if (rect && list && (rect.bottom < list.top || rect.top > list.bottom)) { setPoint(null); return; }
    if (rect) setPoint({ left: Math.max(16, Math.min(rect.left, window.innerWidth - 416)), top: Math.max(8, Math.min(rect.bottom + 8, window.innerHeight - 88)) });
  }
  useLayoutEffect(() => { if (keyboardFocus) show(); else setPoint(null); }, [keyboardFocus]);
  const visible = point !== null;
  useEffect(() => {
    if (!visible && !keyboardFocus) return;
    const dismiss = (event: globalThis.KeyboardEvent) => { if (event.key === 'Escape') setPoint(null); };
    const hide = () => setPoint(null);
    const scroll = () => { if (keyboardFocus) show(); else hide(); };
    window.addEventListener('keydown', dismiss);
    window.addEventListener('resize', hide);
    window.addEventListener('scroll', scroll, true);
    return () => { window.removeEventListener('keydown', dismiss); window.removeEventListener('resize', hide); window.removeEventListener('scroll', scroll, true); };
  }, [visible, keyboardFocus]);
  return <>
    <span ref={anchor} className="bgm-track-name" tabIndex={focusable ? 0 : undefined}
      aria-label={focusable ? name : undefined} aria-describedby={point ? id : undefined}
      onMouseEnter={show} onMouseLeave={() => setPoint(null)} onFocus={show} onBlur={() => setPoint(null)}>{name}</span>
    {point && createPortal(<span id={id} role="tooltip" className="bgm-filename-tooltip" style={point}>{name}</span>, document.body)}
  </>;
}

function MusicRow({ track, selected, tabbable, current, onSelect, onKeyDown, register }: {
  track: MusicTrack; selected: boolean; tabbable: boolean; current: boolean; onSelect: () => void;
  onKeyDown: (event: KeyboardEvent<HTMLButtonElement>) => void; register: (element: HTMLButtonElement | null) => void;
}) {
  const [focused, setFocused] = useState(false);
  return <li><button type="button" className="bgm-track-row" data-track-id={track.id} ref={register}
    aria-label={track.name + ', ' + formatTime(track.seconds) + (current ? ', current background track' : '')}
    aria-pressed={selected} aria-current={current ? 'true' : undefined} tabIndex={tabbable ? 0 : -1}
    onClick={onSelect} onKeyDown={event => { if (event.key === 'Escape') setFocused(false); onKeyDown(event); }}
    onFocus={() => setFocused(true)} onBlur={() => setFocused(false)}>
    <TruncatedName name={track.name} keyboardFocus={focused} />
    <span className="bgm-current-marker" aria-hidden="true">{current && <><span />CURRENT</>}</span>
    <span className="bgm-duration">{formatTime(track.seconds)}</span>
  </button></li>;
}

function MusicScopeColumn({ scope, context, prefix, state, available = true, onAction }: {
  scope: 'playlist' | 'project'; context: string; prefix: string; state: MusicScopeState; available?: boolean;
  onAction: (action: MusicScopeAction) => void;
}) {
  const headingId = useId();
  const selectionHelpId = useId();
  const rows = useRef(new Map<string, HTMLButtonElement>());
  const addButton = useRef<HTMLButtonElement>(null);
  const focusAfterChange = useRef(false);
  const title = scope === 'playlist' ? 'Playlist music' : 'Project music';
  const current = state.tracks.find(track => track.id === state.currentTrackId);
  const selected = state.tracks.find(track => track.id === state.selectedTrackId);
  const isCurrent = selected !== undefined && selected.id === current?.id;
  // Give the first row a tab stop in collections with no selection (e.g. Beta).
  const tabTrack = selected?.id ?? state.tracks[0]?.id;
  useLayoutEffect(() => {
    if (!focusAfterChange.current) return;
    focusAfterChange.current = false;
    const target = selected ? rows.current.get(selected.id) : addButton.current;
    target?.focus({ preventScroll: true });
    target?.scrollIntoView({ block: 'nearest' });
  }, [selected?.id, state.tracks]);
  function listKey(event: KeyboardEvent<HTMLButtonElement>, index: number) {
    let target = index;
    if (event.key === 'ArrowDown') target = Math.min(index + 1, state.tracks.length - 1);
    else if (event.key === 'ArrowUp') target = Math.max(index - 1, 0);
    else if (event.key === 'Home') target = 0;
    else if (event.key === 'End') target = state.tracks.length - 1;
    else return;
    event.preventDefault();
    const track = state.tracks[target];
    onAction({ type: 'select', id: track.id });
    rows.current.get(track.id)?.focus({ preventScroll: true });
    rows.current.get(track.id)?.scrollIntoView({ block: 'nearest' });
  }
  return <section className="bgm-scope" data-scope={scope} aria-labelledby={headingId}>
    <header className="bgm-scope-header"><h1 id={headingId}>{title.toUpperCase()}</h1><p className="bgm-scope-context">{context}</p></header>
    <section className="bgm-current" aria-label={title + ' current background'}>
      <h2>CURRENT</h2>
      <p className={'bgm-current-name' + (!current ? ' is-empty' : '')}>
        {current ? <TruncatedName key={current.id} name={current.name} focusable /> : available ? 'No background music' : 'No project selected'}
      </p>
      <p className="bgm-current-duration">{current ? formatTime(current.seconds) : '—'}</p>
    </section>
    <div className="bgm-volume"><label htmlFor={'volume-' + scope}>Volume</label><output htmlFor={'volume-' + scope}>{available ? state.volume + '%' : '—'}</output>
      <input id={'volume-' + scope} type="range" aria-label={title + ' volume'} aria-valuetext={state.volume + '%'} min="0" max="100" value={state.volume}
        disabled={!available} onChange={event => onAction({ type: 'volume', value: Number(event.currentTarget.value) })} />
    </div>
    <div className="bgm-list-heading"><h2>MUSIC</h2><span>{String(state.tracks.length).padStart(2, '0')}</span></div>
    <ul className="bgm-track-list" aria-label={title + ' tracks'}>
      {!available ? <li className="bgm-empty"><p>No project selected</p><span>Select a project to manage<br />its background music.</span></li>
        : state.tracks.length === 0 ? <li className="bgm-empty"><p>No music added</p></li>
          : state.tracks.map((track, index) => <MusicRow key={track.id} track={track}
            selected={track.id === selected?.id} tabbable={track.id === tabTrack} current={track.id === current?.id}
            register={element => { if (element) rows.current.set(track.id, element); else rows.current.delete(track.id); }}
            onSelect={() => onAction({ type: 'select', id: track.id })} onKeyDown={event => listKey(event, index)} />)}
    </ul>
    <div className="bgm-selection" aria-label={title + ' selection'}>
      <p>{selected ? <TruncatedName key={selected.id} name={selected.name} /> : 'No track selected'}</p>
      <span id={selectionHelpId}>{!available ? 'Select a project to enable controls.' : isCurrent ? 'Current track · Remove also clears background.' : selected ? 'Selected only · not current' : 'Select a track to choose its action.'}</span>
      <span className="sr-only" role="status" aria-live="polite" aria-atomic="true">{state.notice}</span>
    </div>
    <div className="bgm-actions">
      <button type="button" className="bgm-use" disabled={!available || !selected || isCurrent} onClick={() => onAction({ type: 'apply' })}>
        {isCurrent ? 'CURRENT TRACK' : 'USE AS BACKGROUND'}
        {!isCurrent && <svg width="28" height="12" viewBox="0 0 28 12" fill="none" aria-hidden="true"><path d="M1 6h24m-5-4 5 4-5 4" /></svg>}
      </button>
      <button type="button" className="bgm-remove" disabled={!available || !selected} aria-describedby={selectionHelpId}
        onClick={() => { focusAfterChange.current = true; onAction({ type: 'remove' }); }}>REMOVE</button>
    </div>
    <button ref={addButton} type="button" className="bgm-add" disabled={!available}
      onClick={() => { focusAfterChange.current = true; onAction({ type: 'add', prefix }); }}>
      <svg width="12" height="12" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="M8 3v10M3 8h10" /></svg>ADD MUSIC
    </button>
  </section>;
}

export function PrototypeBackgroundMusicShell() {
  const long = new URLSearchParams(window.location.search).get('tracks') === 'long';
  const [state, dispatch] = useReducer(backgroundMusicReducer, long, initialBackgroundMusic);
  const [notice, setNotice] = useState<{ message: string } | null>(null);
  useEffect(() => {
    if (!notice) return;
    const timer = window.setTimeout(() => setNotice(null), 2400);
    return () => window.clearTimeout(timer);
  }, [notice]);
  const projectId = state.activeProjectId;
  return <div className="background-music-shell">
    <TitleBar onFeedback={message => setNotice({ message })} context={<span className="bgm-title-context">Background Music</span>} />
    <main className="bgm-scopes" aria-label="Background music configuration">
      <MusicScopeColumn scope="playlist" context="ALL PROJECTS" prefix="playlist" state={state.playlist}
        onAction={action => dispatch({ type: 'scope', scope: 'playlist', action })} />
      <MusicScopeColumn key={projectId ?? 'none'} scope="project" context={projectId ? projectNames[projectId] : 'NO PROJECT SELECTED'}
        prefix={projectId ?? 'none'} state={projectId ? state.projects[projectId] : unavailableMusic} available={projectId !== null}
        onAction={action => dispatch({ type: 'scope', scope: 'project', action })} />
    </main>
    <footer className="bgm-footer">
      <div className="bgm-footer-left"><span>Main Playlist</span><a className="bgm-trial-data" href={'?view=background-music' + (long ? '' : '&tracks=long')}
        title="Development data: reloads mock state for a new UX trial">{long ? 'STANDARD DATA' : '20+ TRACKS'}<span>↗</span></a></div>
      <div className="bgm-project-trial"><label htmlFor="trial-project">TRIAL PROJECT</label>
        <select id="trial-project" aria-label="UX trial project" value={projectId ?? 'none'}
          onChange={event => dispatch({ type: 'project', id: event.currentTarget.value === 'none' ? null : event.currentTarget.value as ProjectId })}>
          <option value="alpha">Project Alpha</option><option value="beta">Project Beta</option><option value="none">No Project Selected</option>
        </select>
      </div>
      <span className="bgm-shell-feedback" role="status" aria-live="polite" aria-atomic="true">{notice?.message ?? ''}</span>
    </footer>
  </div>;
}
