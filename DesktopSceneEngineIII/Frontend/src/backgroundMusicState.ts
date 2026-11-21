// UI-trial fixtures and state only. No audio, files, persistence or host calls.
export interface MusicTrack { id: string; name: string; seconds: number }
export interface MusicScopeState {
  tracks: MusicTrack[];
  currentTrackId: string | null;
  selectedTrackId: string | null;
  volume: number;
  nextTrackNumber: number;
  notice: string;
}
export type ProjectId = 'alpha' | 'beta';
export type MusicScopeAction =
  | { type: 'select'; id: string }
  | { type: 'volume'; value: number }
  | { type: 'apply' }
  | { type: 'remove' }
  | { type: 'add'; prefix: string };

export interface BackgroundMusicState {
  playlist: MusicScopeState;
  projects: Record<ProjectId, MusicScopeState>;
  activeProjectId: ProjectId | null;
}
type Action = { type: 'project'; id: ProjectId | null }
  | { type: 'scope'; scope: 'playlist' | 'project'; action: MusicScopeAction };

export const projectNames: Record<ProjectId, string> = { alpha: 'Project Alpha', beta: 'Project Beta' };
export const unavailableMusic: MusicScopeState = { tracks: [], currentTrackId: null, selectedTrackId: null, volume: 0, nextTrackNumber: 1, notice: '' };

function collection(prefix: string, tracks: MusicTrack[], current: string | null, volume: number, long: boolean): MusicScopeState {
  const extraNames = ['after-the-rain', 'dawn-study', 'distant-city', 'forest-room', 'gentle-motion', 'late-evening',
    'light-through-glass', 'low-tide', 'midnight-air', 'morning-paper', 'north-window', 'open-water', 'quiet-street',
    'returning-home', 'slow-light', 'soft-horizon', 'still-garden', 'summer-room', 'urban-dusk', 'warm-silence', 'winter-station', 'woodland'];
  const result = [...tracks];
  if (long) for (let i = 0; result.length < 24; i++) result.push({ id: prefix + '-extra-' + i, name: extraNames[i] + '.flac', seconds: 180 + i * 7 });
  return { tracks: result, currentTrackId: current, selectedTrackId: current, volume, nextTrackNumber: 1, notice: '' };
}

export function initialBackgroundMusic(long: boolean): BackgroundMusicState {
  return {
    playlist: collection('playlist', [
      { id: 'playlist-night', name: 'ambience-night.flac', seconds: 222 },
      { id: 'playlist-city', name: 'city-rain.mp3', seconds: 258 },
      { id: 'playlist-room', name: 'quiet-room.ogg', seconds: 176 },
      { id: 'playlist-long', name: 'Blue_Archive_OST_Theme_128_Anniversary_Full_Version_Master.flac', seconds: 268 },
    ], 'playlist-night', 72, long),
    projects: {
      alpha: collection('alpha', [
        { id: 'alpha-piano', name: 'distant-piano.flac', seconds: 206 },
        { id: 'alpha-soft', name: 'soft-light.ogg', seconds: 194 },
        { id: 'alpha-garden', name: 'evening-garden.mp3', seconds: 227 },
      ], 'alpha-piano', 42, long),
      beta: collection('beta', [
        { id: 'beta-morning', name: 'slow-morning.flac', seconds: 241 },
        { id: 'beta-hall', name: 'empty-hall.wav', seconds: 187 },
      ], null, 58, long),
    },
    activeProjectId: 'alpha',
  };
}

function updateScope(state: MusicScopeState, action: MusicScopeAction): MusicScopeState {
  switch (action.type) {
    case 'select':
      return state.tracks.some(track => track.id === action.id) ? { ...state, selectedTrackId: action.id, notice: '' } : state;
    case 'volume':
      return Number.isFinite(action.value) ? { ...state, volume: Math.max(0, Math.min(100, action.value)) } : state;
    case 'apply':
      return state.tracks.some(track => track.id === state.selectedTrackId)
        ? { ...state, currentTrackId: state.selectedTrackId, notice: 'Background set to ' + state.tracks.find(track => track.id === state.selectedTrackId)!.name } : state;
    case 'remove': {
      const index = state.tracks.findIndex(track => track.id === state.selectedTrackId);
      if (index < 0) return state;
      const removed = state.tracks[index];
      const tracks = state.tracks.filter(track => track.id !== removed.id);
      const clearsCurrent = state.currentTrackId === removed.id;
      return { ...state, tracks, currentTrackId: clearsCurrent ? null : state.currentTrackId,
        selectedTrackId: tracks[Math.min(index, tracks.length - 1)]?.id ?? null,
        notice: removed.name + ' removed.' + (clearsCurrent ? ' Background assignment cleared.' : '') };
    }
    case 'add': {
      const number = state.nextTrackNumber;
      const track = { id: action.prefix + '-added-' + number, name: 'New-Track-' + String(number).padStart(2, '0') + '.flac', seconds: 204 };
      return { ...state, tracks: [...state.tracks, track], selectedTrackId: track.id, nextTrackNumber: number + 1,
        notice: track.name + ' added and selected. Not applied as background.' };
    }
  }
}

export function backgroundMusicReducer(state: BackgroundMusicState, action: Action): BackgroundMusicState {
  if (action.type === 'project') return { ...state, activeProjectId: action.id };
  if (action.scope === 'playlist') return { ...state, playlist: updateScope(state.playlist, action.action) };
  const id = state.activeProjectId;
  if (id === null) return state;
  return { ...state, projects: { ...state.projects, [id]: updateScope(state.projects[id], action.action) } };
}
