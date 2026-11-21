import type { LauncherDisplayCatalog, LauncherRegisteredSession, LauncherSessionsSnapshot } from './ProductionLauncherFacade.ts';

export type LauncherSessionStatus = 'ACTIVE' | 'DISCONNECTED' | 'CLOSING' | 'REGISTERED';
export interface LauncherSessionPresentation {
  facts: LauncherRegisteredSession;
  status: LauncherSessionStatus;
  displayResolution: 'resolved' | 'unresolved' | 'unknown';
  controlUnavailable: boolean;
  closeUncertain: boolean;
}

// Registered roles, never admission availability. ACTIVE says only that the
// verified registered incarnation's exact bound Display currently resolves.
export function launcherSessionPresentation(snapshot: LauncherSessionsSnapshot, catalog: LauncherDisplayCatalog | null): Map<string, LauncherSessionPresentation> {
  const presentations = new Map<string, LauncherSessionPresentation>();
  for (const facts of snapshot.registeredSessions) {
    const displayResolution = catalog?.status === 'Success'
      ? catalog.displays.some(display => display.id === facts.boundDisplayId) ? 'resolved' : 'unresolved'
      : 'unknown';
    const closing = facts.closeOperation && ['Received', 'Pending', 'CloseAccepted'].includes(facts.closeOperation.result);
    presentations.set(facts.incarnation, {
      facts, displayResolution,
      status: closing ? 'CLOSING' : displayResolution === 'unresolved' ? 'DISCONNECTED'
        : displayResolution === 'resolved' ? 'ACTIVE' : 'REGISTERED',
      controlUnavailable: !facts.controlConnected,
      closeUncertain: facts.closeOperation?.result === 'Uncertain',
    });
  }
  return presentations;
}

export interface LauncherDisplayAction {
  session?: LauncherSessionPresentation;
  operation?: LauncherSessionsSnapshot['createOperations'][number];
  status: LauncherSessionStatus | 'STARTING' | 'noRegisteredSession';
  kind: 'Create' | 'Open';
  enabled: boolean;
}
// Both the primary action and status read the same native facts. A receipt cannot
// manufacture a live role, and an existing verified role outranks stale Pending.
export function launcherDisplayAction(snapshot: LauncherSessionsSnapshot, catalog: LauncherDisplayCatalog | null,
  displayId: string): LauncherDisplayAction {
  const session = [...launcherSessionPresentation(snapshot, catalog).values()].find(item => item.facts.boundDisplayId === displayId);
  const operation = snapshot.createOperations.find(item => item.displayId === displayId);
  if (session) return { session, operation, status: session.status, kind: 'Open',
    enabled: session.facts.controlConnected && session.status !== 'CLOSING' };
  const pending = operation?.state === 'Pending';
  const display = catalog?.status === 'Success' ? catalog.displays.find(item => item.id === displayId) : undefined;
  return { operation, status: pending ? 'STARTING' : 'noRegisteredSession', kind: 'Create',
    enabled: Boolean(display?.persistentOwnerCapable) && !pending };
}
