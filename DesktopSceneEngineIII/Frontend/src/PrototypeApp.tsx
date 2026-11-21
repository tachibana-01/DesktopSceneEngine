// This module is loaded only by Vite's explicit development entry.
import { LauncherShell } from './LauncherShell';
import { SessionShell } from './SessionShell';
import { PrototypeBackgroundMusicShell } from './PrototypeBackgroundMusicShell';

export default function PrototypeApp() {
  const view = new URLSearchParams(window.location.search).get('view');
  return view === 'background-music' ? <PrototypeBackgroundMusicShell /> : view === 'session' ? <SessionShell /> : <LauncherShell />;
}
