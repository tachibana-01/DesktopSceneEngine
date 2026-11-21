import { createRoot } from 'react-dom/client';
import { lazy, Suspense } from 'react';
import './spike.css';
import { launcherEntryMode } from './ProductionLauncherFacade.ts';
import { LauncherEntryFailure, ProductionLauncherShell } from './ProductionLauncherShell';
import { ProductionSessionShell } from './ProductionSessionShell';
import { ProductionBackgroundMusicShell } from './ProductionBackgroundMusicShell';
import { isModelCSessionPage } from './sessionComposition.ts';
import './sessionComposition.css';

// Vite removes this import entirely from production assets.
const PrototypeApp = import.meta.env.DEV ? lazy(() => import('./PrototypeApp')) : null;

const rootElement = document.getElementById('root');
if (rootElement === null) throw new Error('The application root element is missing.');
const mode = launcherEntryMode(window.location.search, import.meta.env.DEV);
const view = new URLSearchParams(window.location.search).get('view');
// Set before the first React paint. A URL flag alone cannot make a prototype,
// Launcher, or nonnative Session transparent.
if(isModelCSessionPage(window.location.search))document.documentElement.dataset.sessionComposition='model-c-production';
else delete document.documentElement.dataset.sessionComposition;
document.title = mode === 'prototype'
  ? 'DesktopSceneEngine — ' + (view === 'background-music' ? 'Background Music' : view === 'session' ? 'Main Session' : 'Launcher') + ' Prototype'
  : view === 'background-music' ? 'DesktopSceneEngine — Background Music' : view === 'session' ? 'DesktopSceneEngine — Main Session' : 'DesktopSceneEngine — Launcher';
createRoot(rootElement).render(mode === 'production' ? view === 'background-music' ? <ProductionBackgroundMusicShell /> : view === 'session' ? <ProductionSessionShell /> : <ProductionLauncherShell /> : mode === 'prototype' && PrototypeApp
  ? <Suspense fallback={null}><PrototypeApp /></Suspense>
  : <LauncherEntryFailure />);
