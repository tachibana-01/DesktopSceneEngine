// Production-only contract. It does not import the prototype model.
export type LauncherLanguage = 'en' | 'zh_CN';
export interface LauncherLanguageProjection {
  readStatus: 'Success' | 'Failure';
  committed?: LauncherLanguage;
  effective: LauncherLanguage;
  revision: number;
}
export type LauncherLanguageWarning = 'TranslationFallback' | 'NotificationFailure';
export interface LauncherLanguageResult {
  status: 'UNCHANGED' | 'READ_FAILURE' | 'SAVE_FAILURE' | 'COMMITTED';
  language: LauncherLanguageProjection;
  warnings: LauncherLanguageWarning[];
}
export type DisplayEnumerationStatus = 'Success' | 'ApiFailure' | 'IncompletePathMetadata' | 'DuplicateIdentity';
export interface LauncherDisplay {
  id: string;
  identityKind: 'TargetDevicePath' | 'SourceDeviceName';
  persistentOwnerCapable: boolean;
  friendlyName: string;
  sourceDeviceName: string;
  desktopX: number;
  desktopY: number;
  pixelWidth: number;
  pixelHeight: number;
  primary: boolean;
  displayNumber: number;
  refreshRate?: { numerator: number; denominator: number };
}
export interface LauncherDisplayCatalog {
  status: DisplayEnumerationStatus;
  displays: LauncherDisplay[];
}
export type LauncherIdentifyStatus = 'Shown' | 'Unresolved' | 'InvalidRequest' | 'ApiFailure';
export interface LauncherIdentifyResult {
  displayId: string;
  status: LauncherIdentifyStatus;
}
export type LauncherCloseResult = 'Received' | 'Pending' | 'CloseAccepted' | 'CloseAborted' | 'Rejected' | 'Uncertain';
export interface LauncherRegisteredSession {
  boundDisplayId: string;
  incarnation: string;
  controlConnected: boolean;
  closeOperation?: { generation: string; request: string; result: LauncherCloseResult };
}
export type LauncherCreateState = 'Pending' | 'Registered' | 'LaunchFailed' | 'ExitedBeforeRegistration' | 'Uncertain';
export interface LauncherCreateOperation {
  displayId: string;
  operationId: string;
  state: LauncherCreateState;
  incarnation?: string;
}
export type LauncherCreateStatus = 'Accepted' | 'AlreadyPending' | 'AlreadyRegistered' | 'NotOwnerCapable' | 'Unresolved'
  | 'CatalogUnavailable' | 'CapacityExceeded' | 'LaunchFailed' | 'StaleCoordinator' | 'Unavailable';
export interface LauncherCreateResult {
  coordinatorEpoch: string;
  displayId: string;
  status: LauncherCreateStatus;
  operationId?: string;
  incarnation?: string;
}
export type LauncherOpenStatus = 'ActivationRequested' | 'RoleGone' | 'ControlUnavailable' | 'Closing' | 'StaleCoordinator' | 'ApiFailure' | 'Unavailable';
export interface LauncherOpenResult {
  coordinatorEpoch: string;
  incarnation: string;
  status: LauncherOpenStatus;
}
export interface LauncherSessionsSnapshot {
  coordinatorEpoch: string;
  sessionRevision: number;
  registeredSessions: LauncherRegisteredSession[];
  registeredSessionsScope: 'verified-registered-only';
  createOperations: LauncherCreateOperation[];
}
export interface LauncherBootstrapSnapshot extends LauncherSessionsSnapshot {
  displayEnumeration: LauncherDisplayCatalog;
  language: LauncherLanguageProjection;
  host: { ready: true; launcherRoleReady: true };
}
export interface LauncherTransport {
  postMessage(message: unknown): void;
  addEventListener(type: 'message', callback: (event: MessageEvent<unknown>) => void): void;
  removeEventListener(type: 'message', callback: (event: MessageEvent<unknown>) => void): void;
}

export function launcherEntryMode(search: string, development: boolean): 'production' | 'prototype' | 'invalid' {
  const mode = new URLSearchParams(search).get('mode');
  if (mode === 'production') return 'production';
  if (development && (mode === null || mode === 'prototype')) return 'prototype';
  return 'invalid';
}

function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}
function exactKeys(value: Record<string, unknown>, required: string[], optional: string[] = []): boolean {
  return required.every(key => Object.hasOwn(value, key))
    && Object.keys(value).every(key => required.includes(key) || optional.includes(key));
}
function text(value: unknown, max = 32768): value is string {
  return typeof value === 'string' && value.length > 0 && value.length <= max && !value.includes('\0');
}
function integer(value: unknown, minimum = -2147483648, maximum = 2147483647): value is number {
  return typeof value === 'number' && Number.isSafeInteger(value) && value >= minimum && value <= maximum;
}
function language(value: unknown): value is LauncherLanguage { return value === 'en' || value === 'zh_CN'; }
export function readLauncherLanguage(value: unknown): LauncherLanguageProjection {
  if (!record(value) || !exactKeys(value, ['readStatus', 'effective', 'revision'], ['committed'])
    || !language(value.effective) || !integer(value.revision, 0, Number.MAX_SAFE_INTEGER)
    || (value.readStatus === 'Success' ? !language(value.committed)
      : value.readStatus !== 'Failure' || Object.hasOwn(value, 'committed'))) {
    throw new Error('The native Launcher language projection is invalid.');
  }
  return value as unknown as LauncherLanguageProjection;
}
export function readLauncherLanguageResult(value: unknown): LauncherLanguageResult {
  if (!record(value) || !exactKeys(value, ['status', 'language', 'warnings'])
    || typeof value.status !== 'string' || !['UNCHANGED', 'READ_FAILURE', 'SAVE_FAILURE', 'COMMITTED'].includes(value.status)
    || !Array.isArray(value.warnings) || !value.warnings.every(item => item === 'TranslationFallback' || item === 'NotificationFailure')
    || new Set(value.warnings).size !== value.warnings.length
    || (value.status !== 'COMMITTED' && value.warnings.length > 0)) {
    throw new Error('The native Launcher language result is invalid.');
  }
  const projection = readLauncherLanguage(value.language);
  if ((value.status === 'READ_FAILURE') !== (projection.readStatus === 'Failure')) {
    throw new Error('The native Launcher language result is invalid.');
  }
  return value as unknown as LauncherLanguageResult;
}
const uuid = (value: unknown): value is string => typeof value === 'string'
  && /^\{?[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\}?$/i.test(value)
  && (value.startsWith('{') === value.endsWith('}'))
  && !/^\{?00000000-0000-0000-0000-000000000000\}?$/.test(value);
export const launcherEpochIdentity = (value: string): string => value.replace(/[{}]/g, '').toLowerCase();
function uint64Request(value: unknown): value is string {
  return typeof value === 'string' && /^[1-9][0-9]{0,19}$/.test(value)
    && (value.length < 20 || value <= '18446744073709551615');
}
function sessionFacts(value: Record<string, unknown>): boolean {
  if (!uuid(value.coordinatorEpoch) || !integer(value.sessionRevision, 0, Number.MAX_SAFE_INTEGER)
    || value.registeredSessionsScope !== 'verified-registered-only' || !Array.isArray(value.registeredSessions)
    || value.registeredSessions.length > 64 || !Array.isArray(value.createOperations) || value.createOperations.length > 64) return false;
  const operationDisplays = new Set<string>(), operationIds = new Set<string>();
  for (const operation of value.createOperations) {
    if (!record(operation) || !exactKeys(operation, ['displayId', 'operationId', 'state'], operation.state === 'Registered' ? ['incarnation'] : [])
      || !text(operation.displayId) || !uuid(operation.operationId)
      || typeof operation.state !== 'string' || !['Pending', 'Registered', 'LaunchFailed', 'ExitedBeforeRegistration', 'Uncertain'].includes(operation.state)
      || (operation.state === 'Registered' && !uuid(operation.incarnation))
      || operationDisplays.has(operation.displayId) || operationIds.has(launcherEpochIdentity(operation.operationId))) return false;
    operationDisplays.add(operation.displayId); operationIds.add(launcherEpochIdentity(operation.operationId));
  }
  const displays = new Set<string>(), incarnations = new Set<string>();
  for (const item of value.registeredSessions) {
    if (!record(item) || !exactKeys(item, ['boundDisplayId', 'incarnation', 'controlConnected'], ['closeOperation'])
      || !text(item.boundDisplayId) || !uuid(item.incarnation) || typeof item.controlConnected !== 'boolean'
      || displays.has(item.boundDisplayId) || incarnations.has(launcherEpochIdentity(item.incarnation))) return false;
    displays.add(item.boundDisplayId); incarnations.add(launcherEpochIdentity(item.incarnation));
    if (Object.hasOwn(item, 'closeOperation')) {
      const close = item.closeOperation;
      if (!record(close) || !exactKeys(close, ['generation', 'request', 'result']) || !uuid(close.generation)
        || !uint64Request(close.request) || typeof close.result !== 'string'
        || !['Received', 'Pending', 'CloseAccepted', 'CloseAborted', 'Rejected', 'Uncertain'].includes(close.result)) return false;
    }
  }
  return true;
}
export function readLauncherSessionsSnapshot(value: unknown): LauncherSessionsSnapshot {
  if (!record(value) || !exactKeys(value, ['coordinatorEpoch', 'sessionRevision', 'registeredSessionsScope', 'registeredSessions', 'createOperations'])
    || !sessionFacts(value)) throw new Error('The native Launcher Session projection is invalid.');
  return value as unknown as LauncherSessionsSnapshot;
}

function display(value: unknown): value is LauncherDisplay {
  if (!record(value) || !exactKeys(value, ['id', 'identityKind', 'persistentOwnerCapable', 'friendlyName', 'sourceDeviceName',
    'desktopX', 'desktopY', 'pixelWidth', 'pixelHeight', 'primary', 'displayNumber'], ['refreshRate'])) return false;
  if (!text(value.id) || typeof value.identityKind !== 'string' || !['TargetDevicePath', 'SourceDeviceName'].includes(value.identityKind)
    || typeof value.persistentOwnerCapable !== 'boolean'
    || value.persistentOwnerCapable !== (value.identityKind === 'TargetDevicePath')
    || typeof value.friendlyName !== 'string' || value.friendlyName.length > 32768
    || typeof value.sourceDeviceName !== 'string' || value.sourceDeviceName.length > 32768
    || !integer(value.desktopX) || !integer(value.desktopY)
    || !integer(value.pixelWidth, 0) || !integer(value.pixelHeight, 0)
    || typeof value.primary !== 'boolean' || !integer(value.displayNumber, 0)) return false;
  if (Object.hasOwn(value, 'refreshRate')) {
    const rate = value.refreshRate;
    if (!record(rate) || !exactKeys(rate, ['numerator', 'denominator'])
      || !integer(rate.numerator, 1, 4294967295) || !integer(rate.denominator, 1, 4294967295)) return false;
  }
  return true;
}

export function readLauncherCatalog(value: unknown): LauncherDisplayCatalog {
  if (!record(value) || !exactKeys(value, ['status', 'displays'])
    || typeof value.status !== 'string' || !['Success', 'ApiFailure', 'IncompletePathMetadata', 'DuplicateIdentity'].includes(value.status)
    || !Array.isArray(value.displays) || value.displays.length > 256 || !value.displays.every(display)
    || new Set(value.displays.map(item => item.id)).size !== value.displays.length
    || (value.status !== 'Success' && value.displays.length !== 0)) {
    throw new Error('The native Launcher display catalog is invalid.');
  }
  return value as unknown as LauncherDisplayCatalog;
}

export function readLauncherSnapshot(value: unknown): LauncherBootstrapSnapshot {
  const fail = () => { throw new Error('The native Launcher bootstrap snapshot is invalid.'); };
  if (!record(value) || !exactKeys(value, ['coordinatorEpoch', 'sessionRevision', 'displayEnumeration', 'registeredSessions',
    'registeredSessionsScope', 'createOperations', 'language', 'host']) || !sessionFacts(value)) return fail();
  readLauncherCatalog(value.displayEnumeration);
  readLauncherLanguage(value.language);
  if (!record(value.host) || !exactKeys(value.host, ['ready', 'launcherRoleReady'])
    || value.host.ready !== true || value.host.launcherRoleReady !== true) return fail();
  return value as unknown as LauncherBootstrapSnapshot;
}

// Selection owns an opaque native ID. Array indices are a visual concern only.
export function reconcileDisplaySelection(selectedId: string | null, catalog: LauncherDisplayCatalog): string | null {
  if (catalog.status !== 'Success') return null;
  const displays = catalog.displays;
  return displays.some(item => item.id === selectedId) ? selectedId : displays[0]?.id ?? null;
}

interface PendingRequest {
  type: 'GetLauncherBootstrapSnapshot' | 'RefreshDisplays' | 'IdentifyDisplay' | 'CommitLanguage' | 'CreateSession' | 'OpenSession';
  complete: (payload: unknown) => void;
  reject: (error: Error) => void;
  timeout: ReturnType<typeof setTimeout>;
}
export class ProductionLauncherFacade {
  private readonly transport: LauncherTransport;
  private readonly generation: string;
  private readonly pending = new Map<string, PendingRequest>();
  private retired = false;
  private refreshRequest: Promise<LauncherDisplayCatalog> | null = null;
  private readonly languageListeners = new Set<(language: LauncherLanguageProjection) => void>();
  private readonly sessionListeners = new Set<(sessions: LauncherSessionsSnapshot) => void>();

  constructor(transport: LauncherTransport | undefined, generation: string | null) {
    if (!transport || typeof transport.postMessage !== 'function' || typeof transport.addEventListener !== 'function'
      || typeof transport.removeEventListener !== 'function') throw new Error('The production Launcher native bridge is unavailable.');
    if (!uuid(generation)) throw new Error('The production Launcher page generation is missing or invalid.');
    this.transport = transport;
    this.generation = generation;
    this.transport.addEventListener('message', this.onMessage);
  }

  getBootstrapSnapshot(): Promise<LauncherBootstrapSnapshot> {
    return this.request('GetLauncherBootstrapSnapshot', readLauncherSnapshot);
  }

  refreshDisplays(): Promise<LauncherDisplayCatalog> {
    // Coalesce accidental parallel callers; no older enumeration can win later.
    if (this.refreshRequest) return this.refreshRequest;
    this.refreshRequest = this.request('RefreshDisplays', readLauncherCatalog).then(value => {
      this.refreshRequest = null;
      return value;
    }, error => { this.refreshRequest = null; throw error; });
    return this.refreshRequest;
  }

  identifyDisplay(displayId: string): Promise<LauncherIdentifyResult> {
    // IDs stay opaque here. Native validates and resolves the canonical identity.
    if (!text(displayId)) return Promise.reject(new Error('The Identify display target is invalid.'));
    return this.request('IdentifyDisplay', payload => {
      if (!record(payload) || !exactKeys(payload, ['displayId', 'status'])
        || payload.displayId !== displayId || typeof payload.status !== 'string'
        || !['Shown', 'Unresolved', 'InvalidRequest', 'ApiFailure'].includes(payload.status)) {
        throw new Error('The native Launcher Identify result is invalid.');
      }
      return payload as unknown as LauncherIdentifyResult;
    }, { displayId });
  }

  commitLanguage(requested: LauncherLanguage): Promise<LauncherLanguageResult> {
    if (!language(requested)) return Promise.reject(new Error('The language request is invalid.'));
    return this.request('CommitLanguage', payload => {
      const result = readLauncherLanguageResult(payload);
      if ((result.status === 'COMMITTED' || result.status === 'UNCHANGED') && result.language.committed !== requested) {
        throw new Error('The native Launcher language result does not match the request.');
      }
      return result;
    }, { language: requested });
  }

  createSession(coordinatorEpoch: string, displayId: string): Promise<LauncherCreateResult> {
    if (!uuid(coordinatorEpoch) || !text(displayId)) return Promise.reject(new Error('The Create target is invalid.'));
    return this.request('CreateSession', value => {
      if (!record(value) || !exactKeys(value, ['coordinatorEpoch', 'displayId', 'status'], ['operationId', 'incarnation'])
        || !uuid(value.coordinatorEpoch) || value.displayId !== displayId || typeof value.status !== 'string'
        || !['Accepted', 'AlreadyPending', 'AlreadyRegistered', 'NotOwnerCapable', 'Unresolved', 'CatalogUnavailable',
          'CapacityExceeded', 'LaunchFailed', 'StaleCoordinator', 'Unavailable'].includes(value.status)
        || (value.status !== 'StaleCoordinator' && launcherEpochIdentity(value.coordinatorEpoch) !== launcherEpochIdentity(coordinatorEpoch))
        || (['Accepted', 'AlreadyPending', 'LaunchFailed'].includes(value.status) ? !uuid(value.operationId) : Object.hasOwn(value, 'operationId'))
        || (value.status === 'AlreadyRegistered' ? !uuid(value.incarnation) : Object.hasOwn(value, 'incarnation'))) {
        throw new Error('The native Launcher Create result is invalid.');
      }
      return value as unknown as LauncherCreateResult;
    }, { coordinatorEpoch, displayId });
  }

  openSession(coordinatorEpoch: string, incarnation: string): Promise<LauncherOpenResult> {
    if (!uuid(coordinatorEpoch) || !uuid(incarnation)) return Promise.reject(new Error('The Open target is invalid.'));
    return this.request('OpenSession', value => {
      if (!record(value) || !exactKeys(value, ['coordinatorEpoch', 'incarnation', 'status'])
        || !uuid(value.coordinatorEpoch) || !uuid(value.incarnation)
        || launcherEpochIdentity(value.incarnation) !== launcherEpochIdentity(incarnation)
        || typeof value.status !== 'string'
        || !['ActivationRequested', 'RoleGone', 'ControlUnavailable', 'Closing', 'StaleCoordinator', 'ApiFailure', 'Unavailable'].includes(value.status)
        || (value.status !== 'StaleCoordinator' && launcherEpochIdentity(value.coordinatorEpoch) !== launcherEpochIdentity(coordinatorEpoch))) {
        throw new Error('The native Launcher Open result is invalid.');
      }
      return value as unknown as LauncherOpenResult;
    }, { coordinatorEpoch, incarnation });
  }

  subscribeLanguage(listener: (language: LauncherLanguageProjection) => void): () => void {
    this.languageListeners.add(listener);
    return () => { this.languageListeners.delete(listener); };
  }

  subscribeSessions(listener: (sessions: LauncherSessionsSnapshot) => void): () => void {
    this.sessionListeners.add(listener);
    return () => { this.sessionListeners.delete(listener); };
  }

  private request<T>(type: PendingRequest['type'], read: (payload: unknown) => T, payload: object = {}): Promise<T> {
    if (this.retired) return Promise.reject(new Error('The Launcher page has retired.'));
    const requestId = 'launcher_' + crypto.randomUUID().replaceAll('-', '');
    return new Promise((resolve, reject) => {
      // This is a bridge failure deadline, never a simulated domain transition.
      const timeout = setTimeout(() => this.fail(requestId, new Error('The native Launcher request did not respond.')), 10000);
      this.pending.set(requestId, { type, complete: payload => resolve(read(payload)), reject, timeout });
      try {
        this.transport.postMessage({ kind: 'command', version: 1, requestId, type, generation: this.generation, payload });
      } catch (error) { this.fail(requestId, error instanceof Error ? error : new Error('Unable to send the Launcher request.')); }
    });
  }

  dispose(): void {
    if (this.retired) return;
    this.retired = true;
    this.languageListeners.clear();
    this.sessionListeners.clear();
    this.transport.removeEventListener('message', this.onMessage);
    for (const requestId of this.pending.keys()) this.fail(requestId, new Error('The Launcher page has retired.'));
  }

  private fail(requestId: string, error: Error): void {
    const pending = this.pending.get(requestId);
    if (!pending) return;
    clearTimeout(pending.timeout);
    this.pending.delete(requestId);
    pending.reject(error);
  }

  private readonly onMessage = (event: MessageEvent<unknown>): void => {
    if (this.retired) return;
    const value = event.data;
    if (record(value) && value.kind === 'event') {
      if (!exactKeys(value, ['kind', 'version', 'type', 'generation', 'payload']) || value.version !== 1
        || value.generation !== this.generation) return;
      if (value.type === 'LauncherSessionsChanged') {
        let sessions: LauncherSessionsSnapshot;
        try { sessions = readLauncherSessionsSnapshot(value.payload); } catch { return; }
        for (const listener of this.sessionListeners) listener(sessions);
        return;
      }
      if (value.type !== 'LauncherLanguageChanged') return;
      let projection: LauncherLanguageProjection;
      try { projection = readLauncherLanguage(value.payload); } catch { return; }
      for (const listener of this.languageListeners) listener(projection);
      return;
    }
    // A retired document or an unrelated request has no authority over this page.
    if (!record(value) || value.kind !== 'result' || value.generation !== this.generation
      || typeof value.requestId !== 'string' || !this.pending.has(value.requestId)) return;
    const requestId = value.requestId;
    try {
      const pending = this.pending.get(requestId)!;
      if (value.version !== 1 || value.type !== pending.type || typeof value.ok !== 'boolean'
        || !exactKeys(value, ['kind', 'version', 'requestId', 'type', 'generation', 'ok', value.ok ? 'payload' : 'error'])) {
        throw new Error('The native Launcher response is invalid.');
      }
      if (!value.ok) throw new Error(text(value.error, 2048) ? value.error : 'The native Launcher request failed.');
      pending.complete(value.payload);
      clearTimeout(pending.timeout);
      this.pending.delete(requestId);
    } catch (error) { this.fail(requestId, error instanceof Error ? error : new Error('The native Launcher response is invalid.')); }
  };
}
