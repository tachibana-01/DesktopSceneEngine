import { useState, type MouseEvent } from 'react';
import { PlaylistMasterControl } from './PlaylistMasterControl';
import { findPlaylist } from './sessionReadProjection.ts';
import type { SessionReadState } from './sessionReadProjection.ts';
import type { SequenceMode } from './sessionPlaybackSequence.ts';
import { PlaybackModeSelector } from './PlaybackModeSelector';
import { useSessionText } from './sessionLanguage';

export interface EntityMenuTarget { entity: 'playlist' | 'item'; id: string; x: number; y: number }
export function MediaIcon({ kind }: { kind: 'video' | 'image' }) {
  return <svg className="media-file-icon" width="18" height="20" viewBox="0 0 18 20" fill="none" aria-hidden="true">
    <path d="M3 1.5h8L15.5 6v12.5h-13v-17ZM11 1.5V6h4.5" />
    {kind === 'video' ? <path d="m7 9 4 3-4 3V9Z" /> : <><path d="m5 15 3-4 2 2 2-2 1 4H5Z" /><circle cx="6" cy="8" r=".6" /></>}
  </svg>;
}
export function SessionLibrary({ state, onSelect, onPlay, onMenu, onAddPlaylist, onAddMedia, onProjectMode, onItemMode, pendingMode, onMasterValue, onMasterToggle, readOnly = false, playbackAvailable = true, masterEpoch = '', masterDisabled = readOnly, masterToggleDisabled }: {
  state: SessionReadState; readOnly?: boolean; playbackAvailable?: boolean; masterEpoch?: string; masterDisabled?: boolean;
  onSelect: (entity: 'playlist' | 'item', id: string) => void;
  onPlay: (entity: 'playlist' | 'item', id: string) => void;
  onMenu: (target: EntityMenuTarget) => void;
  onAddPlaylist: () => void; onAddMedia: () => void;
  onProjectMode: (mode: SequenceMode) => void; onItemMode: (id: string, mode: SequenceMode) => void;
  pendingMode?: (playlistId: string | null) => SequenceMode | undefined;
  masterToggleDisabled?: (playlistId: string) => boolean;
  onMasterValue: (id: string, percent: number) => void | Promise<boolean>; onMasterToggle: (id: string) => void;
}) {
  const t = useSessionText();
  const [volumeOwner, setVolumeOwner] = useState<string | null>(null);
  const [modeOwner, setModeOwner] = useState<'project' | 'playlist' | null>(null);
  const selectedPlaylist = findPlaylist(state, state.selectedPlaylistId);
  function menu(entity: 'playlist' | 'item', id: string, element: HTMLElement, point?: { x: number; y: number }) {
    if (readOnly) return;
    onSelect(entity, id);
    const bounds = element.getBoundingClientRect();
    onMenu({ entity, id, x: point?.x ?? bounds.right, y: point?.y ?? bounds.bottom });
  }
  function row(entity: 'playlist' | 'item', id: string, alias: string, selected: boolean, active: boolean, enabled: boolean, index: number, kind?: 'video' | 'image') {
    const contextMenu = (event: MouseEvent<HTMLElement>) => { event.preventDefault(); menu(entity, id, event.currentTarget, { x: event.clientX, y: event.clientY }); };
    const volumeKey = masterEpoch + ':' + id;
    return <div key={id} className={'entity-row ' + (entity === 'playlist' ? 'playlist-entity' : 'item-entity')}
      data-playlist-id={entity === 'playlist' ? id : undefined} data-item-id={entity === 'item' ? id : undefined}
      data-selected={selected} data-active={active} data-enabled={enabled} onContextMenu={contextMenu}>
      <button type="button" className="entity-select" aria-label={t(entity === 'playlist' ? 'Select playlist' : 'Select item') + ' ' + alias}
        disabled={readOnly} aria-pressed={selected} title={alias} onClick={() => onSelect(entity, id)} onDoubleClick={playbackAvailable ? () => onPlay(entity, id) : undefined}
        onKeyDown={event => {
          if (event.key === 'ContextMenu' || (event.shiftKey && event.key === 'F10')) {
            event.preventDefault(); menu(entity, id, event.currentTarget);
          }
        }}>
        {kind ? <MediaIcon kind={kind} /> : <span className="entity-index">{String(index + 1).padStart(2, '0')}</span>}
        <span className="entity-alias">{alias}</span>
        {active && <span className="entity-active" aria-label={t('Active')}>{t('ACTIVE')}</span>}
        {!enabled && <span className="sr-only">{t('Disabled')}</span>}
      </button>
      {entity === 'playlist' && <PlaylistMasterControl key={volumeKey} id={id} alias={alias} master={findPlaylist(state, id)!.masterVolume}
        disabled={masterDisabled} toggleDisabled={masterToggleDisabled?.(id)} open={!masterDisabled && volumeOwner === volumeKey} onOpen={() => setVolumeOwner(volumeKey)} onClose={() => setVolumeOwner(owner => owner === volumeKey ? null : owner)}
        onValue={onMasterValue} onToggle={onMasterToggle} />}
      <button type="button" className="entity-more" aria-label={t(entity === 'playlist' ? 'Commands for playlist' : 'Commands for item') + ' ' + alias}
        disabled={readOnly} aria-haspopup="menu" onClick={event => menu(entity, id, event.currentTarget)}>
        <svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true"><circle cx="3" cy="8" r=".8" /><circle cx="8" cy="8" r=".8" /><circle cx="13" cy="8" r=".8" /></svg>
      </button>
    </div>;
  }
  return <aside className="session-library" aria-label={t('Playlists and Items')}>
    <section className="media-panel" aria-labelledby="playlists-heading">
      <div className="panel-heading"><h2 id="playlists-heading">{t('PLAYLISTS')}</h2>
        <span className="section-count">{String(state.project.playlists.length).padStart(2, '0')}</span>
        <button className="panel-icon-button" type="button" aria-label={t('Add Playlist')} title={t('Add Playlist')} disabled={readOnly} onClick={onAddPlaylist}>
          <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="M8 3v10M3 8h10" /></svg>
        </button>
      </div>
      <div className="library-policy"><span>{t('PROJECT')}</span><PlaybackModeSelector scope="project" owner="project" label="Project sequence policy"
        open={modeOwner === 'project'} popupActive={modeOwner !== null} onOpen={() => setModeOwner('project')}
        onClose={() => setModeOwner(owner => owner === 'project' ? null : owner)}
        value={state.project.playlistMode} pendingValue={()=>pendingMode?.(null)} disabled={readOnly} onValue={onProjectMode} /></div>
      <div className="media-list" role="group" aria-label={t('Project Playlists')}>
        {state.project.playlists.map((playlist, index) => row('playlist', playlist.id, playlist.alias,
          state.selectedPlaylistId === playlist.id, state.activePlaylistId === playlist.id, playlist.enabled, index))}
        {!state.project.playlists.length && <p className="library-empty">{t('No playlists')}</p>}
      </div>
    </section>
    <section className="playlist-panel" aria-labelledby="items-heading">
      <div className="panel-heading"><h2 id="items-heading">{t('ITEMS')}</h2>
        <span className="section-count">{String(selectedPlaylist?.items.length ?? 0).padStart(2, '0')}</span>
        <button className="panel-icon-button" type="button" aria-label={t('Add Media')} title={t('Add Media')} disabled={readOnly} onClick={onAddMedia}>
          <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="M8 3v10M3 8h10" /></svg>
        </button>
      </div>
      <div className="library-policy"><span>{t('PLAYLIST')}</span><PlaybackModeSelector scope="playlist" owner={selectedPlaylist?.id ?? null} label="Playlist Item sequence policy"
        open={modeOwner === 'playlist'} popupActive={modeOwner !== null} onOpen={() => setModeOwner('playlist')}
        onClose={() => setModeOwner(owner => owner === 'playlist' ? null : owner)}
        value={selectedPlaylist?.itemMode ?? ''} pendingValue={()=>selectedPlaylist?pendingMode?.(selectedPlaylist.id):undefined} disabled={readOnly || !selectedPlaylist}
        onValue={mode => selectedPlaylist && onItemMode(selectedPlaylist.id, mode)} /></div>
      {selectedPlaylist && <p className="items-owner" title={selectedPlaylist.alias}>{selectedPlaylist.alias}</p>}
      <div className="playlist-list" role="group" aria-label={t('Selected Playlist Items')}>
        {selectedPlaylist?.items.map((item, index) => row('item', item.id, item.alias, state.selectedItemId === item.id,
          state.activeItemId === item.id, selectedPlaylist.enabled && item.enabled, index, item.kind))}
        {(!selectedPlaylist || !selectedPlaylist.items.length) && <p className="library-empty">{t(selectedPlaylist ? 'No items' : 'No playlist selected')}</p>}
      </div>
    </section>
  </aside>;
}
