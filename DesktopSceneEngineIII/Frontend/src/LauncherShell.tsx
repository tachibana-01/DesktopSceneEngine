import { useEffect, useState } from 'react';
import { DisplayDeck, DisplayStatus } from './DisplayDeck';
import { launcherScenarios, primaryAction, readScenario, sessionDescription, type Display, type Language } from './displayData';
import { LauncherSettings } from './LauncherSettings';
import { TitleBar } from './ShellTitleBar';
import { useLauncherPrototype } from './useLauncherPrototype';
import './launcher.css';

function DisplayDetails({ display, onActivate }: { display: Display; onActivate: (id: string) => void }) {
  const action = primaryAction(display);
  const active = display.identityStatus === 'ready' && display.sessionStatus === 'ready';
  return <section className="display-details" aria-labelledby="detail-title">
    <div className="detail-context" key={display.id}>
      <div className="detail-heading"><h2 id="detail-title">{display.name}</h2><DisplayStatus display={display} /></div>
      <p className="detail-metadata">{display.resolution.width} × {display.resolution.height}
        {display.primary && <><span className="metadata-separator">·</span>Primary</>}
      </p>
      <div className="detail-bottom">
        <p className={'session-context' + (active ? '' : ' session-context--empty')} id="display-session-description">
          {active && <svg width="14" height="16" viewBox="0 0 14 16" fill="none" aria-hidden="true">
            <path d="M3 1.5h5L11.5 5v9.5H2.5v-13H3Zm5 0V5h3.5M5 8h4M5 11h4" />
          </svg>}
          <span>{sessionDescription(display)}</span>
        </p>
        {action && <button type="button" className="editorial-action" disabled={action.disabled}
          aria-describedby="display-session-description" onClick={() => onActivate(display.id)}>
          <span>{action.label}</span>
          <svg className="action-arrow" width="88" height="16" viewBox="0 0 88 16" fill="none" aria-hidden="true">
            <path className="arrow-shaft" d="M1 8h83" /><path d="m79 3 5 5-5 5" />
          </svg>
        </button>}
      </div>
    </div>
  </section>;
}

export function LauncherShell() {
  const initialScenario = readScenario(new URLSearchParams(window.location.search).get('launcherScenario'));
  const mock = useLauncherPrototype(initialScenario);
  const { state } = mock;
  const [language, setLanguage] = useState<Language>('en');
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [notice, setNotice] = useState<{ message: string } | null>(null);
  const chinese = language === 'zh-CN';
  const selected = state.displays[((state.position % state.displays.length) + state.displays.length) % state.displays.length];
  const refreshing = state.refreshState === 'refreshing';
  // A retry is pending work, not evidence that the prior failed list is valid.
  const failed = state.enumerationFailed;
  const hideEligible = state.scenario !== 'hide-blocked' && !settingsOpen && !state.displays.some(display => display.sessionStatus === 'closing');
  useEffect(() => {
    if (!notice) return;
    const timer = window.setTimeout(() => setNotice(null), 2400);
    return () => window.clearTimeout(timer);
  }, [notice]);
  return <div className="launcher-shell" data-language={language} data-refresh-state={state.refreshState}>
    <TitleBar onFeedback={message => setNotice({ message })} hideEligible={hideEligible}
      leftActions={<div className="launcher-global-actions" role="group" aria-label="Launcher settings and displays">
        <button type="button" className="window-control" aria-label="Settings" aria-describedby="tooltip-settings"
          aria-haspopup="dialog" onClick={() => setSettingsOpen(true)}>
          <svg width="15" height="15" viewBox="0 0 20 20" fill="none" aria-hidden="true">
            <path d="m8 2-.6 2.1-1.6.9-2.1-.5-2 3.4 1.5 1.6v1.9l-1.5 1.5 2 3.5 2.1-.5 1.6.9.6 2.2h4l.6-2.2 1.6-.9 2.1.5 2-3.5-1.5-1.5V9.5l1.5-1.6-2-3.4-2.1.5-1.6-.9L12 2Z" />
            <circle cx="10" cy="10" r="2.6" />
          </svg>
          <span className="control-tooltip" role="tooltip" id="tooltip-settings">{chinese ? '设置' : 'Settings'}</span>
        </button>
        <button type="button" className={'window-control launcher-refresh' + (refreshing ? ' is-refreshing' : '')}
          aria-label="Refresh displays" aria-describedby="tooltip-refresh" aria-busy={refreshing} disabled={refreshing} onClick={mock.refresh}>
          <svg width="15" height="15" viewBox="0 0 20 20" fill="none" aria-hidden="true"><path d="M16.7 8A7 7 0 1 0 17 12M17 3v5h-5" /></svg>
          <span className="control-tooltip" role="tooltip" id="tooltip-refresh">{chinese ? '刷新显示器' : 'Refresh displays'}</span>
        </button>
      </div>} />
    <main className="launcher-main">
      <div className="page-identity"><h1>{chinese ? '显示器' : 'Displays'}</h1>
        <p>{failed ? (chinese ? '刷新未完成' : 'Refresh incomplete') : state.displays.length + (chinese ? ' 已连接' : ' connected')}</p>
      </div>
      {selected && !failed ? <>
        <DisplayDeck key={state.displays.map(display => display.id).join(',')} displays={state.displays}
          position={state.position} onPositionChange={mock.move} identifyStates={state.identify} onIdentify={mock.identify} />
        <DisplayDetails display={selected} onActivate={mock.activate} />
      </> : <section className="launcher-empty" aria-labelledby="empty-title" aria-busy={refreshing}>
        <h2 id="empty-title">{refreshing ? 'Refreshing displays…' : failed ? 'Unable to load displays.' : 'No available displays'}</h2>
        {failed && <button type="button" data-refresh-retry disabled={refreshing} onClick={mock.refresh}>RETRY <span aria-hidden="true">→</span></button>}
      </section>}
      <p className="launcher-operation" role="status" aria-live="polite" aria-atomic="true">{state.operation}</p>
    </main>
    <footer className="launcher-dev">
      <details><summary>DEV STATE <span aria-hidden="true">·</span> UI prototype</summary>
        <div className="launcher-dev-controls"><label htmlFor="launcher-scenario">Mock scenario</label>
          <select id="launcher-scenario" aria-label="Dev scenario" value={state.scenario} onChange={event => mock.reset(readScenario(event.target.value))}>
            {launcherScenarios.map(([value, label]) => <option key={value} value={value}>{label}</option>)}
          </select>
          <button type="button" onClick={() => mock.reset(state.scenario)}>Reset scenario</button>
          {state.scenario === 'closing' && <button type="button" data-dev-closing disabled={selected?.sessionStatus !== 'ready'} onClick={mock.closeSelected}>Simulate closing</button>}
          <p>Mock only. Reset cancels pending operations; language remains effective.</p>
        </div>
      </details>
    </footer>
    <div className={'mock-feedback' + (notice ? ' is-visible' : '')} role="status" aria-live="polite" aria-atomic="true">{notice?.message ?? ''}</div>
    {settingsOpen && <LauncherSettings effective={language} failSave={state.scenario === 'settings-failure'}
      onClose={() => setSettingsOpen(false)} onCommit={next => { setLanguage(next); mock.feedback('Settings saved.'); }} />}
  </div>;
}
