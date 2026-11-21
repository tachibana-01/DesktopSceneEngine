// UI-only mock projection of Player state. Never an authoritative DSE domain model.
import type { ControllerState, SequenceMode } from './sessionPlaybackSequence.ts';
import type { DisplaySessionState, DisplayEnumeration, DisplayIdentifyStatus } from './sessionDisplayModel.ts';
import type { SessionViewState } from './sessionViewModel.ts';
import type { SessionRoleState } from './sessionLifecycleModel.ts';
export interface SessionItem {
  id: string;
  alias: string;
  source: string;
  kind: 'video' | 'image';
  enabled: boolean;
  duration: number;
  rate: number;
  mediaGain: number;
  presentation: SessionPresentation;
}
export interface SessionPresentation {
  crop: { x: number; y: number; width: number; height: number };
  layout: { positionX: number; positionY: number; scaleX: number; scaleY: number };
  mediaPresentation: { mode: 'native' | 'auto-fill' | 'custom'; customAspect: { numerator: number; denominator: number } };
}
export interface SessionPlaylist {
  id: string;
  alias: string;
  enabled: boolean;
  masterVolume: number;
  itemMode: SequenceMode;
  items: SessionItem[];
}
export interface SessionProject {
  displayOwner: string;
  canvasAspect: [number, number];
  playlistMode: SequenceMode;
  playlists: SessionPlaylist[];
}
export interface SessionFeedback {
  level: 'info' | 'warning' | 'error';
  title: string;
  message: string;
}
export type MockProjectFile = { kind: 'project'; project: SessionProject }
  | { kind: 'invalid' | 'invalid-v3' | 'legacy' | 'unknown-version' | 'unreadable' | 'other' };
export interface SessionProjection {
  hideMock?: { initial?: SessionRoleState; race?: 'show'|'recovery'|'modal'|'expired'|'denied' };
  languageMock?: { committed?: string; catalogAvailable?: boolean; readFails?: boolean };
  feedbackQueue?: SessionFeedback[];
  display?: DisplaySessionState;
  view?: SessionViewState;
  previewMock?: { viewport?: { width: number; height: number }; identifyStatus?: DisplayIdentifyStatus; identifyRefresh?: DisplayEnumeration; returnEnumeration?: DisplayEnumeration };
  project: SessionProject;
  document: { path: string | null; revision: number; cleanRevision: number };
  selectedPlaylistId: string | null;
  selectedItemId: string | null;
  activePlaylistId: string | null;
  activeItemId: string | null;
  runtime: { time: number; paused: boolean; halted: boolean; rate: number; volume: number; items: SessionItem[] } | null;
  sequence: ControllerState;
  outputMuted: boolean;
  timelineInvalidation?: number; // Transient successful native-switch fact; never saved in Project.
  timelineDragCancellation?: number; // Cross-Playlist start cancels before preparation, including failure.
  nextPlaylistOrdinal: string; // Decimal uint64: preserve native suffix precision in this mock projection.
  nextEntityOrdinal: number;
  files: Record<string, MockProjectFile>;
  faults: {
    saveFails: boolean;
    rateFails: boolean;
    seekFails?: boolean;
    navigation: 'none' | 'prepare' | 'bind' | 'rollback';
    reload: 'none' | 'invalid' | 'wrong-owner';
    playback: 'none' | 'start-bind' | 'successor-bind' | 'rebuild-prepare' | 'rebuild-bind' | 'restore-seek' | 'rollback-bind' | 'rollback-seek';
  };
  closed: boolean;
  feedback: SessionFeedback | null;
}
export interface MockSource {
  source: string;
  kind: 'video' | 'image' | null;
  duration: number;
}
export interface OpenCandidate { path: string; project: SessionProject }
export type PendingDocumentIntent = { kind: 'new' | 'close' } | { kind: 'open'; candidate: OpenCandidate };
