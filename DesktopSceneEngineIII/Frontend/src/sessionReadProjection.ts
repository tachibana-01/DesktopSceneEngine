import type { SessionItem, SessionPlaylist, SessionProject } from './sessionTypes.ts';

// Shared rendering facts only. Production never constructs the prototype's
// document, sequence controller, files, fault injection or playback authority.
export interface SessionReadState {
  project: SessionProject;
  selectedPlaylistId: string | null;
  selectedItemId: string | null;
  activePlaylistId: string | null;
  activeItemId: string | null;
  runtime: null | {
    time: number; paused: boolean; rate: number; volume?: number;
    duration?: number; sceneIndex?: number; sceneCount?: number; itemIds?: string[]; mediaKind?: 'image' | 'video' | 'unknown';
    // DEV's already accepted projection carries bound Item values. Production
    // consumes the narrower native runtime summary, without manufacturing Items.
    items?: SessionItem[]; halted?: boolean;
  };
  outputMuted: boolean;
}
export function findPlaylist(state: Pick<SessionReadState, 'project'>, id: string | null): SessionPlaylist | undefined {
  return state.project.playlists.find(playlist => playlist.id === id);
}
export function findItem(state: Pick<SessionReadState, 'project'>, id: string | null): { playlist: SessionPlaylist; item: SessionItem } | undefined {
  for (const playlist of state.project.playlists) {
    const item = playlist.items.find(candidate => candidate.id === id);
    if (item) return { playlist, item };
  }
}
