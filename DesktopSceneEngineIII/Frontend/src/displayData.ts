export type IdentityStatus = 'ready' | 'identifyRequired' | 'unavailable';
export type SessionStatus = 'none' | 'starting' | 'ready' | 'closing' | 'disconnected';
export type IdentifyState = 'idle' | 'identifying' | 'success' | 'failure';
export type RefreshState = 'idle' | 'refreshing' | 'success' | 'enumerationFailure' | 'noDisplays';
export type Language = 'en' | 'zh-CN';

export interface Display {
  id: string;
  name: string;
  resolution: { width: number; height: number };
  primary: boolean;
  identityStatus: IdentityStatus;
  sessionStatus: SessionStatus;
}

const mockDisplays: readonly Display[] = [
  { id: 'display-a', name: 'DISPLAY A', resolution: { width: 2560, height: 1440 }, primary: true,
    identityStatus: 'ready', sessionStatus: 'ready' },
  { id: 'display-b', name: 'DISPLAY B', resolution: { width: 1920, height: 1080 }, primary: false,
    identityStatus: 'ready', sessionStatus: 'none' },
  { id: 'display-c', name: 'DISPLAY C', resolution: { width: 1920, height: 1200 }, primary: false,
    identityStatus: 'ready', sessionStatus: 'none' },
  { id: 'display-d', name: 'DISPLAY D', resolution: { width: 3440, height: 1440 }, primary: false,
    identityStatus: 'identifyRequired', sessionStatus: 'none' },
  { id: 'display-e', name: 'DISPLAY E', resolution: { width: 1080, height: 1920 }, primary: false,
    identityStatus: 'unavailable', sessionStatus: 'none' },
];

export const launcherScenarios = [
  ['normal', 'Normal'], ['identify-required', 'Identification required'], ['identify-failure', 'Identify failure'],
  ['refresh-failure', 'Refresh failure · next refresh'], ['no-displays', 'No displays'],
  ['display-disappearance', 'Display disappears · each refresh'], ['starting', 'Starting · replay'],
  ['closing', 'Closing · run sequence'], ['disconnected', 'Disconnected'],
  ['hide-blocked', 'Hide blocked'], ['settings-failure', 'Settings save failure'],
] as const;
export type LauncherScenario = typeof launcherScenarios[number][0];
export function readScenario(value: string | null): LauncherScenario {
  return launcherScenarios.find(([key]) => key === value)?.[0] ?? 'normal';
}

// Aspect/count fixtures stay identical to V1.1. No enumeration or persistence.
export function displayFixtures(): Display[] {
  const count = Number(new URLSearchParams(window.location.search).get('displays') ?? 5);
  return mockDisplays.slice(0, [1, 2, 3, 5].includes(count) ? count : 5).map(display => ({ ...display }));
}

export function displayStatus(display: Display): string {
  if (display.identityStatus === 'identifyRequired') return 'IDENTIFICATION REQUIRED';
  if (display.identityStatus === 'unavailable') return 'UNAVAILABLE';
  return { none: 'AVAILABLE', starting: 'STARTING…', ready: 'SESSION ACTIVE',
    closing: 'CLOSING…', disconnected: 'SESSION DISCONNECTED' }[display.sessionStatus];
}

export function primaryAction(display: Display): { label: string; disabled: boolean } | null {
  if (display.identityStatus === 'unavailable') return null;
  if (display.identityStatus === 'identifyRequired') return { label: 'CREATE SESSION', disabled: true };
  switch (display.sessionStatus) {
    case 'none': return { label: 'CREATE SESSION', disabled: false };
    case 'starting': return { label: 'STARTING…', disabled: true };
    case 'ready': return { label: 'OPEN SESSION', disabled: false };
    case 'closing': return { label: 'CLOSING…', disabled: true };
    case 'disconnected': return null;
  }
}

export function sessionDescription(display: Display): string {
  if (display.identityStatus === 'identifyRequired') return 'This display must be identified before a session can be created.';
  if (display.identityStatus === 'unavailable') return 'This display is currently unavailable.';
  return { none: 'No active session', starting: 'Starting session…', ready: 'Session ready',
    closing: 'Closing session…', disconnected: 'Session disconnected. Recovery is handled by the application.' }[display.sessionStatus];
}
