import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties, type KeyboardEvent } from 'react';
import { ProductionLauncherFacade, type LauncherBootstrapSnapshot, type LauncherDisplay, type LauncherDisplayCatalog } from './ProductionLauncherFacade.ts';
import { ProductionLauncherController, type LauncherIdentifyFeedback, type LauncherSessionActionFeedback, type ProductionLauncherState } from './ProductionLauncherController.ts';
import type { LauncherLanguageFeedback } from './ProductionLauncherController.ts';
import type { LauncherLanguage } from './ProductionLauncherFacade.ts';
import { ProductionLauncherSettings } from './ProductionLauncherSettings.tsx';
import { launcherCopy, launcherLanguageFeedback, launcherLocale, launcherSessionActionMessage } from './launcherLanguage.ts';
import { launcherSessionPresentation, launcherDisplayAction, type LauncherSessionPresentation, type LauncherDisplayAction } from './launcherSessionPresentation.ts';
import { productWindowContext, useProductPresentation } from './productWindowHost.ts';
import { ProductTitleBar } from './ProductTitleBar.tsx';
import { FrozenProductHide } from './ShellTitleBar.tsx';
import { translateSession } from './sessionLanguage.tsx';
import './launcher.css';
import './productionLauncher.css';

const modulo = (value: number, count: number) => ((value % count) + count) % count;
export function productionDeckPosition(visualPosition: number, selectedIndex: number, count: number): number {
  return count <= 1 ? 0 : visualPosition + selectedIndex - modulo(visualPosition, count);
}
function displayName(display: LauncherDisplay, language: LauncherLanguage) {
  const label = display.friendlyName.trim();
  if (label && !/[\\\x00-\x1f\x7f]/.test(label) && !/^(?:dse-display-id:|(?:display|monitor)#|display\s*\d+$)/i.test(label)) return label;
  return launcherCopy(language).display + ' ' + display.displayNumber;
}

function useLauncherHide(allowed: boolean, generation: string | null) {
  const [eligibleGeneration, setEligibleGeneration] = useState<string | null>(null);
  useLayoutEffect(() => {
    const context = productWindowContext();
    if (!context || context.generation !== generation) return;
    const receive = ({data}: {data: unknown}) => {
      if (!data || typeof data !== 'object' || Array.isArray(data) || Object.keys(data).length !== 4) return;
      if (!('kind' in data) || data.kind !== 'launcher-hide-state' || !('version' in data) || data.version !== 1
          || !('generation' in data) || data.generation !== context.generation
          || !('eligible' in data) || typeof data.eligible !== 'boolean') return;
      setEligibleGeneration(data.eligible ? context.generation : null);
    };
    context.bridge.addEventListener('message', receive);
    return () => context.bridge.removeEventListener('message', receive);
  }, [generation]);
  if (!allowed || !generation || eligibleGeneration !== generation) return undefined;
  return () => {
    const context = productWindowContext();
    if (context?.generation === generation && !document.querySelector('dialog:modal')) {
      context.bridge.postMessage({kind: 'launcher-hide-request', version: 1, generation});
    }
  };
}
type BootstrapFacts = Omit<LauncherBootstrapSnapshot, 'displayEnumeration'>;
function RegistrationFact({ session, starting = false, language }: { session?: LauncherSessionPresentation; starting?: boolean; language: LauncherLanguage }) {
  const copy = launcherCopy(language);
  const label = !session ? starting ? copy.starting : copy.noRegisteredSession : session.status === 'ACTIVE' ? copy.active
    : session.status === 'DISCONNECTED' ? copy.disconnected : session.status === 'CLOSING' ? copy.closing : copy.registered;
  return <span className={'display-status' + (session ? ' display-status--' + session.status.toLowerCase() : '')}
    data-session-status={session?.status ?? (starting ? 'STARTING' : 'noRegisteredSession')}>
    <span className="status-dot" aria-hidden="true" />{label}
  </span>;
}
function SessionSecondaryFacts({ session, language }: { session?: LauncherSessionPresentation; language: LauncherLanguage }) {
  if (!session) return null;
  const copy = launcherCopy(language);
  return <>
    {session.displayResolution === 'unknown' && <p className="production-session-secondary" data-session-resolution="unknown">{copy.resolutionUnknown}</p>}
    {session.status === 'CLOSING' && session.displayResolution === 'unresolved'
      && <p className="production-session-secondary" data-session-resolution="unresolved">{copy.boundDisplayUnresolved}</p>}
    {session.controlUnavailable && <p className="production-session-secondary" data-session-control="unavailable">{copy.controlUnavailable}</p>}
    {session.closeUncertain && <p className="production-session-secondary" data-session-close="uncertain">{copy.closeUncertain}</p>}
  </>;
}

function SessionAction({ action, displayId, language, createPending, openPending, onCreate, onOpen }: {
  action: LauncherDisplayAction; displayId: string; language: LauncherLanguage; createPending: boolean; openPending: boolean;
  onCreate: (displayId: string) => void; onOpen: (displayId: string, incarnation: string) => void;
}) {
  const copy = launcherCopy(language), opening = action.kind === 'Open';
  const pending = opening ? openPending : createPending;
  return <button type="button" className="editorial-action" data-session-action={action.kind}
    data-action-display-id={displayId} data-action-incarnation={action.session?.facts.incarnation ?? ''}
    disabled={!action.enabled || pending} aria-busy={pending}
    onClick={() => { if (opening && action.session) onOpen(displayId, action.session.facts.incarnation); else if (!opening) onCreate(displayId); }}>
    <span>{opening ? copy.open : action.status === 'STARTING' ? copy.starting : copy.create}</span>
    <svg className="action-arrow" width="88" height="16" viewBox="0 0 88 16" fill="none" aria-hidden="true">
      <path className="arrow-shaft" d="M1 8h83" /><path d="m79 3 5 5-5 5" />
    </svg>
  </button>;
}

function ProductionDisplayDeck({ snapshot, catalog, selectedId, onSelect, onIdentify, identifyPending, language }: {
  snapshot: BootstrapFacts; catalog: LauncherDisplayCatalog; selectedId: string; onSelect: (id: string) => void;
  onIdentify: () => void; identifyPending: boolean; language: LauncherLanguage;
}) {
  const copy = launcherCopy(language);
  const sessions = [...launcherSessionPresentation(snapshot, catalog).values()];
  const displays = catalog.displays;
  const count = displays.length;
  const selectedIndex = displays.findIndex(display => display.id === selectedId);
  const viewport = useRef<HTMLDivElement>(null);
  const focusAfterMove = useRef(false);
  const [{width: viewportWidth, maxHeight}, setViewport] = useState({width: 880, maxHeight: 176});
  // This position is animation bookkeeping. It never owns display selection.
  const [visual, setVisual] = useState({ id: selectedId, position: selectedIndex });
  const position = productionDeckPosition(visual.position, selectedIndex, count);
  const cycle = Math.floor(position / count);
  useLayoutEffect(() => {
    const element = viewport.current;
    if (!element) return;
    const measure = () => {
      const width = element.getBoundingClientRect().width;
      const maxHeight = Number.parseFloat(getComputedStyle(element).getPropertyValue('--production-deck-max-height')) || 176;
      setViewport(previous => previous.width === width && previous.maxHeight === maxHeight ? previous : {width, maxHeight});
    };
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(element);
    window.addEventListener('resize', measure);
    return () => {observer.disconnect(); window.removeEventListener('resize', measure);};
  }, []);
  useLayoutEffect(() => {
    if (!focusAfterMove.current) return;
    focusAfterMove.current = false;
    viewport.current?.querySelector<HTMLButtonElement>('[data-selected="true"]')?.focus({ preventScroll: true });
  }, [position]);

  const aspects = displays.map(display => display.pixelWidth > 0 && display.pixelHeight > 0 ? display.pixelWidth / display.pixelHeight : 16 / 9);
  const edgeRoom = Math.max(24, Math.min(72, viewportWidth * .06)) + 12;
  const slideHeight = Math.min(maxHeight, Math.max(100, (viewportWidth - edgeRoom * 2) / (Math.max(...aspects) * 1.2)));
  const gap = viewportWidth < 600 ? 24 : 32;
  const widths = aspects.map(aspect => slideHeight * aspect);
  const centers: number[] = [];
  let period = 0;
  for (const width of widths) { centers.push(period + width / 2); period += width + gap; }
  const centerOf = (index: number) => Math.floor(index / count) * period + centers[modulo(index, count)];
  const selectedCenter = centerOf(position);
  const extraSpace = widths[selectedIndex] * .2 / 2;
  const slots = count === 1 ? [0] : Array.from({ length: count * 5 }, (_, index) => (cycle - 2) * count + index);
  const accessibleInstance = (index: number) => [-1, 0, 1].map(offset => (cycle + offset) * count + index)
    .reduce((best, candidate) => Math.abs(centerOf(candidate) - selectedCenter) <= Math.abs(centerOf(best) - selectedCenter) ? candidate : best);

  function select(next: number, moveFocus = false) {
    const active = document.activeElement;
    const focusedCard = active instanceof HTMLElement && viewport.current?.contains(active);
    if (focusedCard) active.blur();
    focusAfterMove.current = moveFocus || Boolean(focusedCard);
    const display = displays[modulo(next, count)];
    setVisual({ id: display.id, position: next });
    onSelect(display.id);
  }
  function keyDown(event: KeyboardEvent<HTMLElement>) {
    if (count < 2 || event.ctrlKey || event.metaKey || event.altKey || !['ArrowLeft', 'ArrowRight'].includes(event.key)) return;
    event.preventDefault();
    select(position + (event.key === 'ArrowRight' ? 1 : -1), true);
  }
  return <section className="display-deck" aria-label={copy.deck} aria-roledescription={copy.carousel} onKeyDown={keyDown}
    data-display-count={count} data-position={position} style={{ '--slide-height': slideHeight + 'px', '--deck-duration': '220ms' } as CSSProperties}>
    <div className="deck-viewport" ref={viewport}>
      <div className="deck-track" style={{ transform: 'translateX(' + -selectedCenter + 'px)' }}>
        {slots.map(slot => {
          const index = modulo(slot, count);
          const display = displays[index];
          const selected = slot === position;
          const duplicate = count > 1 && accessibleInstance(index) !== slot;
          const center = centerOf(slot) + (slot < position ? -extraSpace : slot > position ? extraSpace : 0);
          return <div key={slot} className={'display-slide' + (selected ? ' is-focused' : '') + (display.pixelHeight > display.pixelWidth ? ' is-portrait' : '')}
            aria-hidden={duplicate || undefined} style={{ left: center + 'px', width: widths[index] + 'px', '--selection-rise': (selected ? slideHeight * .1 : 0) + 'px' } as CSSProperties}>
            <button type="button" className={'display-surface' + (selected ? ' is-selected' : '')}
              aria-label={copy.select + ' ' + displayName(display, language)} aria-pressed={selected} tabIndex={selected ? 0 : -1}
              data-selected={selected} data-display-id={display.id} data-visual-index={slot}
              onMouseDown={event => { if (duplicate) event.preventDefault(); }} onClick={() => { if (!selected) select(slot, true); }}>
              <span className="display-index" aria-hidden="true">{String(display.displayNumber).padStart(2, '0')}</span>
              {display.primary && <span className="primary-mark">{copy.primary.toLocaleUpperCase(launcherLocale(language))}</span>}
              <span className="display-label"><span className={'display-name' + (selected ? ' display-name--spacer' : '')} aria-hidden={selected || undefined}>{displayName(display, language)}</span>
                <span className="display-resolution"><span>{display.pixelWidth}</span><span>×</span><span>{display.pixelHeight}</span></span>
              </span><span className="selection-edge" aria-hidden="true" />
            </button>
            {selected && <div className="display-identify-layer">
              <div className="display-label">
                <button type="button" className="display-name display-identify" data-identify-display data-identify-target={display.id}
                  aria-label={copy.identify + ' ' + displayName(display, language)} aria-describedby="production-identify-tooltip"
                  aria-busy={identifyPending} aria-disabled={identifyPending} onClick={() => { if (!identifyPending) onIdentify(); }}>
                  <span className="production-display-name-text">{displayName(display, language)}</span>
                  <span className="control-tooltip" id="production-identify-tooltip" role="tooltip">{copy.identify}</span>
                </button>
                <span className="display-resolution display-name--spacer" aria-hidden="true"><span>{display.pixelWidth}</span><span>×</span><span>{display.pixelHeight}</span></span>
              </div>
            </div>}
            <div className="display-caption"><RegistrationFact session={sessions.find(session => session.facts.boundDisplayId === display.id)} starting={launcherDisplayAction(snapshot, catalog, display.id).status === 'STARTING'} language={language} /></div>
          </div>;
        })}
      </div>
    </div>
    {count > 1 && <div className="deck-navigation" role="group" aria-label={copy.navigation}>
      <button type="button" className="deck-arrow" aria-label={copy.previous} onClick={() => select(position - 1)}>←</button>
      <div className="deck-indicators" role="group" aria-label={copy.choose}>
        {displays.map((display, index) => <button type="button" key={display.id} className={'deck-indicator' + (index === selectedIndex ? ' is-current' : '')}
          aria-label={copy.goTo + ' ' + displayName(display, language)} aria-current={index === selectedIndex ? 'true' : undefined}
          onClick={() => select(position + index - selectedIndex)}><span aria-hidden="true" /></button>)}
      </div>
      <button type="button" className="deck-arrow" aria-label={copy.next} onClick={() => select(position + 1)}>→</button>
    </div>}
    <span className="sr-only" aria-live="polite" aria-atomic="true">{displayName(displays[selectedIndex], language)}, {selectedIndex + 1} {copy.of} {count}</span>
  </section>;
}

function ProductionHeader({ onRefresh, refreshing = false, language = 'en', onSettings, onHide }: {
  onRefresh?: () => void; refreshing?: boolean; language?: LauncherLanguage; onSettings?: () => void; onHide?: () => void;
}) {
  const copy = launcherCopy(language);
  const text = (value: string) => translateSession(language, value);
  return <ProductTitleBar text={text}
    beforeCaption={productWindowContext() && <FrozenProductHide text={text} onHide={onHide} />}
    beforeDrag={<>
    <span className="production-launcher-title">DesktopSceneEngine</span>
    {(onRefresh || onSettings) && <div className="launcher-global-actions" role="group" aria-label={copy.launcherActions}>
      {onRefresh && <button type="button" className={'window-control launcher-refresh' + (refreshing ? ' is-refreshing' : '')}
        data-refresh-displays aria-label={copy.refresh} aria-describedby="tooltip-refresh"
        aria-busy={refreshing} disabled={refreshing} onClick={onRefresh}>
        <svg width="15" height="15" viewBox="0 0 20 20" fill="none" aria-hidden="true"><path d="M16.7 8A7 7 0 1 0 17 12M17 3v5h-5" /></svg>
        <span className="control-tooltip" role="tooltip" id="tooltip-refresh">{copy.refresh}</span>
      </button>}
      {onSettings && <button type="button" className="window-control launcher-settings-trigger" data-settings-open
        aria-label={copy.settings} aria-describedby="tooltip-settings" onClick={onSettings}>
        <svg width="15" height="15" viewBox="0 0 20 20" fill="none" aria-hidden="true"><path d="m8 2-.5 2-2 .8-1.8-.6L2 7l1.5 1.4v2.8L2 12.6l1.7 2.8 1.8-.6 2 .8.5 2h3l.5-2 2-.8 1.8.6 1.7-2.8-1.5-1.4V8.4L17 7l-1.7-2.8-1.8.6-2-.8-.5-2H8Z"/><circle cx="9.5" cy="9.8" r="2.8" /></svg>
        <span className="control-tooltip" role="tooltip" id="tooltip-settings">{copy.settings}</span>
      </button>}
    </div>}
  </>} />;
}

export function ProductionLauncherView({ snapshot, catalog, selectedId, onSelect, onRefresh, onIdentify = () => {},
  refreshing = false, refreshError = null, identifyPending = false, identifyFeedback = null, generation = '', onSettings, onHide, languageFeedback = null,
  onCreate = () => {}, onOpen = () => {}, createRequestDisplayIds = [], openRequestIncarnations = [], sessionActionFeedback = null }: {
  snapshot: BootstrapFacts; catalog: LauncherDisplayCatalog | null;
  selectedId: string | null; onSelect: (id: string) => void; onRefresh: () => void;
  refreshing?: boolean; refreshError?: string | null; generation?: string;
  onIdentify?: () => void; identifyPending?: boolean; identifyFeedback?: LauncherIdentifyFeedback | null;
  onSettings?: () => void; onHide?: () => void; languageFeedback?: LauncherLanguageFeedback | null;
  onCreate?: (displayId: string) => void; onOpen?: (displayId: string, incarnation: string) => void;
  createRequestDisplayIds?: string[]; openRequestIncarnations?: string[]; sessionActionFeedback?: LauncherSessionActionFeedback | null;
}) {
  const language = snapshot.language.effective;
  const copy = launcherCopy(language);
  const selected = catalog?.status === 'Success' ? catalog.displays.find(display => display.id === selectedId) : undefined;
  const sessions = [...launcherSessionPresentation(snapshot, catalog).values()];
  const selectedSession = selected ? sessions.find(session => session.facts.boundDisplayId === selected.id) : undefined;
  const selectedAction = selected ? launcherDisplayAction(snapshot, catalog, selected.id) : undefined;
  const selectedFeedback = selected && sessionActionFeedback?.displayId === selected.id
    && (sessionActionFeedback.kind === 'Create' || sessionActionFeedback.incarnation === selectedSession?.facts.incarnation) ? sessionActionFeedback : null;
  const selectedReceipt = !selectedSession ? selectedAction?.operation : undefined;
  const actionMessage = launcherSessionActionMessage(selectedFeedback, language)
    ?? (selectedReceipt?.state === 'LaunchFailed' ? copy.createFailed : selectedReceipt?.state === 'ExitedBeforeRegistration' ? copy.createExited
      : selectedReceipt?.state === 'Uncertain' ? copy.createUncertain : null);
  const unresolvedSessions = sessions.filter(session => session.displayResolution !== 'resolved');
  // An old target's result never describes a newly selected Display.
  const identifyStatus = selected && identifyFeedback?.displayId === selected.id ? identifyFeedback.status : null;
  const identifyMessage = identifyPending ? copy.identifyPending
    : identifyStatus === 'Unresolved' ? copy.identifyUnresolved
    : identifyStatus === 'InvalidRequest' ? copy.identifyInvalid
    : identifyStatus === 'ApiFailure' ? copy.identifyFailure
    : identifyStatus === 'TransportFailure' ? copy.identifyTransport : null;
  const catalogMessage = catalog?.status === 'ApiFailure' ? copy.apiFailure
    : catalog?.status === 'IncompletePathMetadata' ? copy.metadataFailure
    : catalog?.status === 'DuplicateIdentity' ? copy.duplicateFailure : copy.catalogUnavailable;
  return <div className="launcher-shell production-launcher" data-bootstrap-state="ready" lang={launcherLocale(language)}
    data-language={language} data-committed-language={snapshot.language.committed ?? ''} data-language-revision={snapshot.language.revision}
    data-enumeration-status={catalog?.status ?? 'unavailable'} data-refresh-error={refreshError ? 'transport' : ''}
    data-refresh-state={refreshing ? 'pending' : refreshError || catalog?.status !== 'Success' ? 'failed' : 'idle'}
    data-identify-state={identifyPending ? 'pending' : 'idle'} data-identify-result={identifyStatus ?? ''}
    data-generation={generation} data-coordinator-epoch={snapshot.coordinatorEpoch} data-session-revision={snapshot.sessionRevision}
    data-selected-display-id={selected?.id ?? ''}>
    <ProductionHeader onRefresh={onRefresh} refreshing={refreshing} language={language} onSettings={onSettings} onHide={onHide} />
    <main className="launcher-main">
      <div className="page-identity"><h1>{copy.displays}</h1>
        <p>{refreshError ? copy.refreshIncomplete
          : catalog?.status === 'Success' ? catalog.displays.length + ' ' + copy.connected : copy.enumerationFailed}</p>
      </div>
      {selected ? <>
        <ProductionDisplayDeck snapshot={snapshot} catalog={catalog!} selectedId={selected.id} onSelect={onSelect}
          onIdentify={onIdentify} identifyPending={identifyPending} language={language} />
        <section className="display-details" aria-labelledby="detail-title">
          <div className="detail-context" key={selected.id} data-session-incarnation={selectedSession?.facts.incarnation ?? ''}
            data-bound-display-id={selectedSession?.facts.boundDisplayId ?? ''}
            data-create-operation-id={selectedAction?.operation?.operationId ?? ''} data-create-operation-state={selectedAction?.operation?.state ?? ''}
            data-session-control-connected={selectedSession?.facts.controlConnected}
            data-session-close-result={selectedSession?.facts.closeOperation?.result ?? ''}>
            <div className="detail-heading"><h2 id="detail-title">{displayName(selected, language)}</h2><RegistrationFact session={selectedSession} starting={selectedAction?.status === 'STARTING'} language={language} /></div>
            <p className="detail-metadata">{selected.pixelWidth} × {selected.pixelHeight}
              <span className="metadata-separator">·</span>{selected.primary ? copy.primary : copy.secondary}
              {selected.refreshRate && <><span className="metadata-separator">·</span>{(selected.refreshRate.numerator / selected.refreshRate.denominator).toFixed(2)} Hz</>}
            </p>
            <div className="detail-bottom">{(!selectedSession || selectedAction?.enabled) && <p className="session-context">{selectedSession ? copy.sessionRegistered
              : selectedAction?.status === 'STARTING' ? copy.sendingCreate
                : selected.persistentOwnerCapable ? copy.sessionUnknown : copy.notOwnerCapable}</p>}
              {selectedAction && <SessionAction action={selectedAction} displayId={selected.id} language={language}
                createPending={createRequestDisplayIds.includes(selected.id)} openPending={openRequestIncarnations.includes(selectedSession?.facts.incarnation ?? '')}
                onCreate={onCreate} onOpen={onOpen} />}</div>
            <SessionSecondaryFacts session={selectedSession} language={language} />
          </div>
        </section>
      </> : <section className="launcher-empty" aria-labelledby="empty-title" aria-busy={refreshing}>
        <h2 id="empty-title">{refreshError ? copy.refreshFailed : catalog?.status === 'Success' ? copy.noDisplays : copy.enumerateFailed}</h2>
        {(refreshError || catalog?.status !== 'Success') && <>
          <p className="production-enumeration-status" role="status">{refreshError ? copy.refreshInvalid : catalogMessage}</p>
          <button type="button" data-refresh-retry disabled={refreshing} onClick={onRefresh}>{copy.retry} <span aria-hidden="true">→</span></button>
        </>}
      </section>}
      {unresolvedSessions.length > 0 && <section className="production-unresolved-sessions" aria-label={copy.registeredSessions}>
        <h2>{copy.registeredSessions}</h2>
        {unresolvedSessions.map((session, index) => <div className="production-session-record" key={session.facts.incarnation}
          data-session-incarnation={session.facts.incarnation} data-bound-display-id={session.facts.boundDisplayId}
          data-session-control-connected={session.facts.controlConnected} data-session-close-result={session.facts.closeOperation?.result ?? ''}>
          <div className="production-session-record-heading"><span className="production-session-bound">{copy.workspace}{unresolvedSessions.length > 1 ? ' ' + (index + 1) : ''}</span>
            <RegistrationFact session={session} language={language} /></div>
          <SessionSecondaryFacts session={session} language={language} />
          <SessionAction action={launcherDisplayAction(snapshot, catalog, session.facts.boundDisplayId)} displayId={session.facts.boundDisplayId}
            language={language} createPending={false} openPending={openRequestIncarnations.includes(session.facts.incarnation)} onCreate={onCreate} onOpen={onOpen} />
          {sessionActionFeedback?.kind === 'Open' && sessionActionFeedback.incarnation === session.facts.incarnation
            && <p className="production-session-secondary" role="status">{launcherSessionActionMessage(sessionActionFeedback, language)}</p>}
        </div>)}

      </section>}
      <p className="launcher-operation" role="status" aria-live="polite">{actionMessage ?? identifyMessage ?? (selected && createRequestDisplayIds.includes(selected.id) ? copy.sendingCreate
        : selectedSession && openRequestIncarnations.includes(selectedSession.facts.incarnation) ? copy.sendingOpen : refreshing ? copy.refreshing : null)}</p>
      {snapshot.language.readStatus === 'Failure' && <p className="production-language-warning" role="status">{copy.languageReadFailure}</p>}
      {snapshot.language.readStatus === 'Success' && snapshot.language.committed !== language && !languageFeedback
        && <p className="production-language-warning" role="status">{copy.translationFallback}</p>}
      {languageFeedback && <p className="production-language-warning" data-language-feedback role="status" aria-live="polite">{launcherLanguageFeedback(languageFeedback, language)}</p>}
    </main>
  </div>;
}

export function ProductionLauncherShell() {
  const parameters = new URLSearchParams(window.location.search);
  const generation = parameters.get('generation');
  // Native presentation hint for loading/errors only, superseded by bootstrap.
  const initialLanguage: LauncherLanguage = parameters.get('language') === 'zh_CN' ? 'zh_CN' : 'en';
  const copy = launcherCopy(initialLanguage);
  const [state, setState] = useState<ProductionLauncherState | null>(null);
  const [error, setError] = useState<string | null>(null);
  const controllerRef = useRef<ProductionLauncherController | null>(null);
  const container = useRef<HTMLDivElement>(null);
  const repairFocus = useRef<'refresh' | 'selected' | null>(null);
  useLayoutEffect(() => {
    const target = repairFocus.current;
    repairFocus.current = null;
    if (target) container.current?.querySelector<HTMLButtonElement>(target === 'refresh'
      ? '[data-refresh-displays]' : '[data-selected="true"]')?.focus({ preventScroll: true });
  }, [state]);
  useEffect(() => {
    let controller: ProductionLauncherController | undefined;
    let unsubscribe: (() => void) | undefined;
    setState(null);
    setError(null);
    try {
      controller = new ProductionLauncherController(new ProductionLauncherFacade(window.chrome?.webview, generation));
      controllerRef.current = controller;
      let previousCatalog: LauncherDisplayCatalog | null = null;
      unsubscribe = controller.subscribe(next => {
        const active = document.activeElement;
        const card = active instanceof HTMLElement && container.current?.contains(active)
          ? active.closest<HTMLElement>('[data-display-id]') : null;
        if (card && next.catalog !== previousCatalog) {
          repairFocus.current = next.catalog?.displays.some(display => display.id === card.dataset.displayId) ? 'selected' : 'refresh';
        }
        previousCatalog = next.catalog;
        setState(next);
      });
      void controller.bootstrap();
    } catch (failure) { setError(failure instanceof Error ? failure.message : 'The native Launcher bootstrap failed.'); }
    return () => { unsubscribe?.(); controller?.dispose(); controllerRef.current = null; };
  }, [generation]);

  const failure = error ?? state?.bootstrapError;
  const onHide = useLauncherHide(Boolean(state?.bootstrap) && !state?.settingsOpen, generation);
  useProductPresentation(state?.bootstrap ? 'ready' : failure ? 'failed' : 'loading');
  if (state?.bootstrap) return <div ref={container} className="production-launcher-root"><ProductionLauncherView snapshot={state.bootstrap} catalog={state.catalog}
    selectedId={state.selectedDisplayId} onSelect={id => controllerRef.current?.select(id)} onRefresh={() => { void controllerRef.current?.refresh(); }}
    onIdentify={() => { void controllerRef.current?.identifySelected(); }} identifyPending={state.identifyPending} identifyFeedback={state.identifyFeedback}
    refreshing={state.refreshPending} refreshError={state.refreshError} generation={generation ?? ''}
    onSettings={() => controllerRef.current?.openSettings()} onHide={onHide} languageFeedback={state.languageFeedback}
    onCreate={id => { void controllerRef.current?.createSession(id); }} onOpen={(id, incarnation) => { void controllerRef.current?.openSession(id, incarnation); }}
    createRequestDisplayIds={state.createRequestDisplayIds} openRequestIncarnations={state.openRequestIncarnations} sessionActionFeedback={state.sessionActionFeedback} />
    {state.settingsOpen && <ProductionLauncherSettings language={state.bootstrap.language} draft={state.draftLanguage}
      pending={state.languagePending} feedback={state.settingsFeedback} onDraft={draft => controllerRef.current?.draftLanguage(draft)}
      onCancel={() => controllerRef.current?.cancelSettings()} onCommit={close => { void controllerRef.current?.commitLanguage(close); }} />}
    </div>;
  return <div className="launcher-shell production-launcher" data-bootstrap-state={failure ? 'failed' : 'loading'} data-generation={generation ?? ''} lang={launcherLocale(initialLanguage)}>
    <ProductionHeader language={initialLanguage} /><main className="launcher-main"><div className="page-identity"><h1>{copy.displays}</h1></div>
      {failure ? <section className="launcher-empty production-bootstrap-error" role="alert"><h2>{copy.bootstrapFailure}</h2><p>{copy.bootstrapExplanation}</p>
        {controllerRef.current && <button type="button" data-bootstrap-retry disabled={state?.bootstrapPending}
          onClick={() => { void controllerRef.current?.bootstrap(); }}>{copy.retry} <span aria-hidden="true">→</span></button>}</section>
        : <section className="launcher-empty" aria-busy="true"><h2>{copy.connecting}</h2></section>}
    </main>
  </div>;
}

export function LauncherEntryFailure() {
  const language: LauncherLanguage = new URLSearchParams(window.location.search).get('language') === 'zh_CN' ? 'zh_CN' : 'en';
  const copy = launcherCopy(language);
  return <div className="launcher-shell production-launcher" data-bootstrap-state="failed" lang={launcherLocale(language)}><ProductionHeader language={language} />
    <main className="launcher-main"><section className="launcher-empty production-bootstrap-error" role="alert">
      <h2>{copy.hostRequired}</h2><p>{copy.hostExplanation}</p>
    </section></main>
  </div>;
}
