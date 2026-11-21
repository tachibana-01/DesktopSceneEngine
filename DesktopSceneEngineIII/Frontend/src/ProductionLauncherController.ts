import { ProductionLauncherFacade, reconcileDisplaySelection, launcherEpochIdentity,
  type LauncherBootstrapSnapshot, type LauncherDisplayCatalog, type LauncherIdentifyResult,
  type LauncherLanguage, type LauncherLanguageProjection, type LauncherLanguageResult, type LauncherSessionsSnapshot,
  type LauncherCreateResult, type LauncherOpenResult } from './ProductionLauncherFacade.ts';

import { launcherDisplayAction } from './launcherSessionPresentation.ts';

export type LauncherSessionActionFeedback = { kind: 'Create'; displayId: string; result: LauncherCreateResult | { status: 'TransportFailure' } }
  | { kind: 'Open'; displayId: string; incarnation: string; result: LauncherOpenResult | { status: 'TransportFailure' } };
export type LauncherIdentifyFeedback = LauncherIdentifyResult | { displayId: string; status: 'TransportFailure' };
export type LauncherLanguageFeedback = LauncherLanguageResult | { status: 'TransportFailure' };

export interface ProductionLauncherState {
  bootstrap: Omit<LauncherBootstrapSnapshot, 'displayEnumeration'> | null;
  catalog: LauncherDisplayCatalog | null;
  selectedDisplayId: string | null;
  createRequestDisplayIds: string[];
  openRequestIncarnations: string[];
  sessionActionFeedback: LauncherSessionActionFeedback | null;
  bootstrapPending: boolean;
  bootstrapError: string | null;
  refreshPending: boolean;
  refreshError: string | null;
  identifyPending: boolean;
  identifyFeedback: LauncherIdentifyFeedback | null;
  settingsOpen: boolean;
  draftLanguage: LauncherLanguage | null;
  languagePending: boolean;
  settingsFeedback: LauncherLanguageFeedback | null;
  languageFeedback: LauncherLanguageFeedback | null;
}

// Coordinates one page's asynchronous presentation. Native owns catalog facts;
// this controller owns only pending feedback and exact-ID selection.
export class ProductionLauncherController {
  private state: ProductionLauncherState = {
    bootstrap: null, catalog: null, selectedDisplayId: null, bootstrapPending: false,
    createRequestDisplayIds: [], openRequestIncarnations: [], sessionActionFeedback: null,
    bootstrapError: null, refreshPending: false, refreshError: null,
    identifyPending: false, identifyFeedback: null,
    settingsOpen: false, draftLanguage: null, languagePending: false,
    settingsFeedback: null, languageFeedback: null,
  };
  private retired = false;
  private readonly listeners = new Set<(state: ProductionLauncherState) => void>();
  private readonly facade: ProductionLauncherFacade;
  private readonly unsubscribeLanguage: () => void;
  private latestLanguage: LauncherLanguageProjection | null = null;
  private readonly unsubscribeSessions: () => void;
  private latestSessions: LauncherSessionsSnapshot | null = null;
  private readonly retiredEpochs = new Set<string>();
  private bootstrapAttempt = 0;

  constructor(facade: ProductionLauncherFacade) {
    this.facade = facade;
    this.unsubscribeLanguage = facade.subscribeLanguage(language => {
      if (this.retired || (this.latestLanguage && language.revision < this.latestLanguage.revision)) return;
      const newer = !this.latestLanguage || language.revision > this.latestLanguage.revision;
      this.latestLanguage = language;
      if (this.state.bootstrap) this.publish({ ...this.state, bootstrap: { ...this.state.bootstrap, language },
        settingsFeedback: newer ? null : this.state.settingsFeedback,
        languageFeedback: newer ? null : this.state.languageFeedback });
    });
    this.unsubscribeSessions = facade.subscribeSessions(sessions => this.receiveSessions(sessions));
  }

  private receiveSessions(sessions: LauncherSessionsSnapshot): void {
    const epoch = launcherEpochIdentity(sessions.coordinatorEpoch);
    if (this.retired || this.retiredEpochs.has(epoch)) return;
    const previous = this.latestSessions;
    if (previous && launcherEpochIdentity(previous.coordinatorEpoch) !== epoch) {
      // UUIDs identify lifetimes; they cannot be ordered. Retire the old lifetime
      // and require a fresh bootstrap instead of mixing roles and host facts.
      this.retiredEpochs.add(launcherEpochIdentity(previous.coordinatorEpoch));
      this.latestSessions = sessions;
      this.bootstrapAttempt++;
      this.publish({ ...this.state, bootstrap: null, catalog: null, selectedDisplayId: null,
        bootstrapPending: false, bootstrapError: 'The Coordinator lifetime changed. Retry bootstrap.',
        refreshPending: false, identifyPending: false, identifyFeedback: null,
        settingsOpen: false, draftLanguage: null, languagePending: false, settingsFeedback: null,
        createRequestDisplayIds: [], openRequestIncarnations: [], sessionActionFeedback: null });
      return;
    }
    if (previous && sessions.sessionRevision <= previous.sessionRevision) return;
    this.latestSessions = sessions;
    if (this.state.bootstrap) this.publish({ ...this.state, bootstrap: { ...this.state.bootstrap, ...sessions } });
  }

  getState(): ProductionLauncherState { return this.state; }
  subscribe(listener: (state: ProductionLauncherState) => void): () => void {
    this.listeners.add(listener);
    return () => { this.listeners.delete(listener); };
  }
  private publish(next: ProductionLauncherState): void {
    this.state = next;
    for (const listener of this.listeners) listener(next);
  }

  async bootstrap(): Promise<void> {
    if (this.retired || this.state.bootstrapPending || this.state.bootstrap) return;
    this.publish({ ...this.state, bootstrapPending: true, bootstrapError: null });
    const attempt = ++this.bootstrapAttempt;
    try {
      const { displayEnumeration: catalog, ...bootstrap } = await this.facade.getBootstrapSnapshot();
      if (this.retired || attempt !== this.bootstrapAttempt) return;
      const epoch = launcherEpochIdentity(bootstrap.coordinatorEpoch);
      if (this.retiredEpochs.has(epoch) || (this.latestSessions
        && launcherEpochIdentity(this.latestSessions.coordinatorEpoch) !== epoch)) {
        this.retiredEpochs.add(epoch);
        throw new Error('The Coordinator bootstrap lifetime is stale. Retry bootstrap.');
      }
      if (this.latestSessions && this.latestSessions.sessionRevision > bootstrap.sessionRevision) Object.assign(bootstrap, this.latestSessions);
      else this.latestSessions = { coordinatorEpoch: bootstrap.coordinatorEpoch, sessionRevision: bootstrap.sessionRevision,
        registeredSessions: bootstrap.registeredSessions, registeredSessionsScope: bootstrap.registeredSessionsScope, createOperations: bootstrap.createOperations };
      if (this.latestLanguage && this.latestLanguage.revision > bootstrap.language.revision) bootstrap.language = this.latestLanguage;
      else this.latestLanguage = bootstrap.language;
      this.publish({ ...this.state, bootstrap, catalog, bootstrapPending: false,
        selectedDisplayId: reconcileDisplaySelection(this.state.selectedDisplayId, catalog) });
    } catch (error) {
      if (this.retired || attempt !== this.bootstrapAttempt) return;
      this.publish({ ...this.state, bootstrapPending: false,
        bootstrapError: error instanceof Error ? error.message : 'The native Launcher bootstrap failed.' });
    }
  }

  async refresh(): Promise<void> {
    // Synchronous guard also covers rapid clicks before React paints disabled.
    if (this.retired || !this.state.bootstrap || this.state.refreshPending) return;
    const epoch = this.state.bootstrap.coordinatorEpoch;
    this.publish({ ...this.state, refreshPending: true, refreshError: null });
    try {
      const catalog = await this.facade.refreshDisplays();
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      this.publish({ ...this.state, catalog, refreshPending: false,
        selectedDisplayId: reconcileDisplaySelection(this.state.selectedDisplayId, catalog) });
    } catch (error) {
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      // A bridge/validation failure is not a fabricated Platform ApiFailure.
      this.publish({ ...this.state, catalog: null, selectedDisplayId: null, refreshPending: false,
        refreshError: error instanceof Error ? error.message : 'The native Launcher refresh failed.' });
    }
  }

  select(id: string): void {
    if (this.retired || this.state.catalog?.status !== 'Success'
      || !this.state.catalog.displays.some(display => display.id === id)) return;
    this.publish({ ...this.state, selectedDisplayId: id });
  }

  async identifySelected(): Promise<void> {
    const displayId = this.state.selectedDisplayId;
    if (this.retired || this.state.identifyPending || this.state.catalog?.status !== 'Success'
      || !this.state.catalog.displays.some(display => display.id === displayId) || !displayId) return;
    const epoch = this.state.bootstrap?.coordinatorEpoch;
    // Capture only the opaque command target. This is request feedback, never a
    // second Display object or an overlay-lifetime/ownership state machine.
    this.publish({ ...this.state, identifyPending: true, identifyFeedback: null });
    try {
      const result = await this.facade.identifyDisplay(displayId);
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      this.publish({ ...this.state, identifyPending: false, identifyFeedback: result });
    } catch {
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      this.publish({ ...this.state, identifyPending: false,
        identifyFeedback: { displayId, status: 'TransportFailure' } });
    }
  }

  private invalidateCommandOwner(): void {
    if (this.state.bootstrap) this.retiredEpochs.add(launcherEpochIdentity(this.state.bootstrap.coordinatorEpoch));
    this.latestSessions = null;
    this.bootstrapAttempt++;
    this.publish({ ...this.state, bootstrap: null, catalog: null, selectedDisplayId: null,
      bootstrapPending: false, bootstrapError: 'The Coordinator lifetime changed. Retry bootstrap.',
      refreshPending: false, identifyPending: false, identifyFeedback: null,
      settingsOpen: false, draftLanguage: null, languagePending: false, settingsFeedback: null,
      createRequestDisplayIds: [], openRequestIncarnations: [], sessionActionFeedback: null });
  }

  async createSession(displayId: string): Promise<void> {
    const bootstrap = this.state.bootstrap;
    if (this.retired || !bootstrap || this.state.createRequestDisplayIds.includes(displayId)) return;
    const action = launcherDisplayAction(bootstrap, this.state.catalog, displayId);
    if (action.kind !== 'Create' || !action.enabled) return;
    const epoch = bootstrap.coordinatorEpoch;
    this.publish({ ...this.state, createRequestDisplayIds: [...this.state.createRequestDisplayIds, displayId], sessionActionFeedback: null });
    try {
      const result = await this.facade.createSession(epoch, displayId);
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      if (result.status === 'StaleCoordinator') { this.invalidateCommandOwner(); return; }
      // This is transport/result feedback only. Session and Pending operation
      // facts are updated exclusively through bootstrap/live projection.
      this.publish({ ...this.state, createRequestDisplayIds: this.state.createRequestDisplayIds.filter(id => id !== displayId),
        sessionActionFeedback: { kind: 'Create', displayId, result } });
    } catch {
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      this.publish({ ...this.state, createRequestDisplayIds: this.state.createRequestDisplayIds.filter(id => id !== displayId),
        sessionActionFeedback: { kind: 'Create', displayId, result: { status: 'TransportFailure' } } });
    }
  }

  async openSession(displayId: string, incarnation: string): Promise<void> {
    const bootstrap = this.state.bootstrap;
    if (this.retired || !bootstrap || this.state.openRequestIncarnations.includes(incarnation)) return;
    const action = launcherDisplayAction(bootstrap, this.state.catalog, displayId);
    if (action.kind !== 'Open' || !action.enabled || action.session?.facts.incarnation !== incarnation) return;
    const epoch = bootstrap.coordinatorEpoch;
    this.publish({ ...this.state, openRequestIncarnations: [...this.state.openRequestIncarnations, incarnation], sessionActionFeedback: null });
    try {
      const result = await this.facade.openSession(epoch, incarnation);
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      if (result.status === 'StaleCoordinator') { this.invalidateCommandOwner(); return; }
      this.publish({ ...this.state, openRequestIncarnations: this.state.openRequestIncarnations.filter(id => id !== incarnation),
        sessionActionFeedback: { kind: 'Open', displayId, incarnation, result } });
    } catch {
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      this.publish({ ...this.state, openRequestIncarnations: this.state.openRequestIncarnations.filter(id => id !== incarnation),
        sessionActionFeedback: { kind: 'Open', displayId, incarnation, result: { status: 'TransportFailure' } } });
    }
  }

  openSettings(): void {
    if (this.retired || !this.state.bootstrap || this.state.settingsOpen) return;
    this.publish({ ...this.state, settingsOpen: true,
      draftLanguage: this.state.bootstrap.language.committed ?? null, settingsFeedback: null });
  }

  draftLanguage(language: LauncherLanguage): void {
    if (this.retired || !this.state.settingsOpen || this.state.languagePending || (language !== 'en' && language !== 'zh_CN')) return;
    this.publish({ ...this.state, draftLanguage: language, settingsFeedback: null });
  }

  cancelSettings(): void {
    if (this.retired || !this.state.settingsOpen || this.state.languagePending) return;
    this.publish({ ...this.state, settingsOpen: false, draftLanguage: null, settingsFeedback: null });
  }

  async commitLanguage(closeOnSuccess: boolean): Promise<void> {
    const draft = this.state.draftLanguage;
    if (this.retired || !this.state.settingsOpen || this.state.languagePending || !draft) return;
    const epoch = this.state.bootstrap?.coordinatorEpoch;
    this.publish({ ...this.state, languagePending: true, settingsFeedback: null, languageFeedback: null });
    try {
      const result = await this.facade.commitLanguage(draft);
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      const current = this.state.bootstrap;
      const superseded = this.latestLanguage && this.latestLanguage.revision > result.language.revision;
      const language = superseded ? this.latestLanguage! : result.language;
      this.latestLanguage = language;
      const close = closeOnSuccess && (result.status === 'COMMITTED' || result.status === 'UNCHANGED');
      this.publish({ ...this.state, bootstrap: current ? { ...current, language } : current, languagePending: false,
        settingsOpen: !close, draftLanguage: close ? null : draft, settingsFeedback: close || superseded ? null : result,
        languageFeedback: close && !superseded && result.warnings.length > 0 ? result : null });
    } catch {
      if (this.retired || this.state.bootstrap?.coordinatorEpoch !== epoch) return;
      this.publish({ ...this.state, languagePending: false, settingsFeedback: { status: 'TransportFailure' } });
    }
  }

  dispose(): void {
    if (this.retired) return;
    this.retired = true;
    this.unsubscribeLanguage();
    this.unsubscribeSessions();
    this.listeners.clear();
    this.facade.dispose();
  }
}
