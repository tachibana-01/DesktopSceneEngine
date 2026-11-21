import type { MockProjectFile, OpenCandidate, PendingDocumentIntent, SessionFeedback, SessionProject, SessionProjection } from './sessionTypes';
import { controllerClear } from './sessionPlaybackSequence.ts';
import { createSessionDisplay, displayFacts, displayTransition } from './sessionDisplayModel.ts';

// UI-only file transaction projection of Player/MainWindow.cpp:811–1080.
// The in-memory files represent authored file snapshots, never real persistence.
export function isDirty(state: SessionProjection): boolean {
  return state.document.revision !== state.document.cleanRevision;
}

export function documentName(state: SessionProjection): string {
  return state.document.path?.split(/[\\/]/).at(-1) || 'Untitled';
}

function openError(path: string, cause: string): SessionFeedback {
  return {
    level: 'error', title: 'Open Project Failed',
    message: `Unable to open project:\n${path}\n\n${cause}`,
  };
}

function readError(file: MockProjectFile | undefined): string {
  if (!file || file.kind === 'unreadable') return 'Unable to open project file for reading.';
  if (file.kind === 'legacy') return 'Authored project uses an unsupported pre-release format.';
  if (file.kind === 'unknown-version') return 'Unsupported authored project JSON version.';
  if (file.kind === 'invalid-v3') return 'Invalid Authored Project JSON.';
  if (file.kind === 'other') return 'Authored project JSON type must be DesktopSceneEngineProject.';
  return 'Unable to parse authored project JSON.';
}

export function loadOpenCandidate(state: SessionProjection, path: string): {
  candidate: OpenCandidate | null; error: SessionFeedback | null;
} {
  const file = state.files[path];
  if (file?.kind !== 'project') return { candidate: null, error: openError(path, readError(file)) };
  if (file.project.displayOwner !== state.project.displayOwner) {
    return {
      candidate: null,
      error: { level: 'error', title: 'Open Project Failed', message: 'The project belongs to a different display session.' },
    };
  }
  return { candidate: { path, project: structuredClone(file.project) }, error: null };
}

function saveAsDestinationError(state: SessionProjection, path: string): string | null {
  const destination = state.files[path];
  // ProjectFile.cpp:548–627: unrelated bytes and pre-release files may be
  // overwritten. Recognizable current/future DSE files require admission.
  if (destination?.kind === 'project' && destination.project.displayOwner !== state.project.displayOwner) {
    return 'The destination belongs to a different display session.';
  }
  if (destination?.kind === 'invalid-v3') return 'Invalid Authored Project JSON at destination.';
  if (destination?.kind === 'unknown-version') return 'Unsupported authored project destination version.';
  return null;
}

export function saveToMockPath(state: SessionProjection, path: string, adoptPath: boolean): {
  state: SessionProjection; result: 'saved' | 'failed';
} {
  const admissionError = adoptPath ? saveAsDestinationError(state, path) : null;
  const cannotWrite = state.faults.saveFails || !path || /[\\/]$/.test(path)
    || path === 'C:/MockProjects/missing-parent/project.json'
    || state.files[path]?.kind === 'unreadable';
  if (admissionError || cannotWrite) {
    return {
      state: {
        ...state,
        feedback: {
          level: 'error', title: 'Save Project Failed',
          message: `Unable to save project:\n${path}\n\n${admissionError ?? 'Unable to write the authored project file.'}`,
        },
      },
      result: 'failed',
    };
  }
  return {
    state: {
      ...state,
      files: { ...state.files, [path]: { kind: 'project', project: structuredClone(state.project) } },
      document: {
        ...state.document,
        path: adoptPath ? path : state.document.path,
        cleanRevision: state.document.revision,
      },
      feedback: null,
    },
    result: 'saved',
  };
}

function nextPlaylistOrdinal(project: SessionProject): string {
  let next = 0n;
  const maximumNativeOrdinal = (1n << 64n) - 1n;
  for (const playlist of project.playlists) {
    const suffix = /^Playlist ([A-Z]+)$/.exec(playlist.alias)?.[1];
    if (!suffix) continue;
    let oneBasedOrdinal = 0n;
    for (const letter of suffix) {
      oneBasedOrdinal = oneBasedOrdinal * 26n + BigInt(letter.charCodeAt(0) - 64);
      if (oneBasedOrdinal > maximumNativeOrdinal) break;
    }
    if (oneBasedOrdinal <= maximumNativeOrdinal && oneBasedOrdinal > next) next = oneBasedOrdinal;
  }
  return next.toString();
}

function replaceDocument(state: SessionProjection, project: SessionProject, path: string | null): SessionProjection {
  const revision = state.document.revision + 1;
  const sequence = structuredClone(state.sequence);
  controllerClear(sequence);
  return {
    ...state,
    project: structuredClone(project),
    document: { path, revision, cleanRevision: revision },
    selectedPlaylistId: null, selectedItemId: null,
    activePlaylistId: null, activeItemId: null, runtime: null,
    nextPlaylistOrdinal: nextPlaylistOrdinal(project),
    sequence,
    feedback: null,
  };
}

export function continueDocumentIntent(state: SessionProjection, pending: PendingDocumentIntent, decision: 'discard' | 'saved'): SessionProjection {
  if (pending.kind !== 'open') {
    if (pending.kind === 'close') {
      const sequence = structuredClone(state.sequence); controllerClear(sequence);
      return { ...state, closed: true, activePlaylistId: null, activeItemId: null, runtime: null, sequence, feedback: null };
    }
    // New refreshes exact-bound metadata once; loaded Canvas is never reseeded.
    const refreshed = state.display ? { ...state, display: displayTransition(state.display, { type: 'enumerate', result: state.display.enumeration }) } : state;
    const descriptor = displayFacts(refreshed.display ?? createSessionDisplay(state.project.displayOwner)).descriptor;
    let canvasAspect: [number, number] = [16, 9];
    if (descriptor && [descriptor.pixelWidth, descriptor.pixelHeight].every(value => Number.isInteger(value) && value > 0 && value <= 2147483647)) {
      let left = descriptor.pixelWidth, right = descriptor.pixelHeight;
      while (right) [left, right] = [right, left % right];
      canvasAspect = [descriptor.pixelWidth / left, descriptor.pixelHeight / left];
    }
    return replaceDocument(refreshed, { displayOwner: state.project.displayOwner, canvasAspect, playlistMode: 'repeat-current', playlists: [] }, null);
  }

  let candidate = pending.candidate;
  if (decision === 'saved') {
    // A successful Save may overwrite the candidate path. Re-load and re-admit
    // its owner; never commit stale preloaded content or roll back legal Save.
    const reloadState = state.faults.reload === 'none' ? state : {
      ...state,
      files: {
        ...state.files,
        [candidate.path]: state.faults.reload === 'invalid'
          ? { kind: 'invalid' as const }
          : { kind: 'project' as const, project: { ...candidate.project, displayOwner: 'mock-reload-other-display' } },
      },
    };
    const reloaded = loadOpenCandidate(reloadState, candidate.path);
    if (!reloaded.candidate) return { ...state, feedback: reloaded.error };
    candidate = reloaded.candidate;
  }
  return replaceDocument(state, candidate.project, candidate.path);
}
