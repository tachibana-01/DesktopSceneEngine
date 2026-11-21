import { findItem, findPlaylist, type SessionReadState } from './sessionReadProjection.ts';
import { formatRuntimeTime } from './sessionTime.ts';
import './sessionPlayback.css';
import type { displayFacts } from './sessionDisplayModel.ts';
import { useSessionText } from './sessionLanguage';

function PlaybackIcon({ action }: { action: 'previous' | 'next' | 'play' | 'pause' }) {
  return <svg width="16" height="16" viewBox="0 0 18 18" fill="none" aria-hidden="true">
    {action === 'previous' && <><path d="M4 4v10" /><path d="m13 4-7 5 7 5V4Z" /></>}
    {action === 'next' && <><path d="M14 4v10" /><path d="m5 4 7 5-7 5V4Z" /></>}
    {action === 'play' && <path d="m6 3 8 6-8 6V3Z" />}
    {action === 'pause' && <path d="M6 4v10M12 4v10" />}
  </svg>;
}
export function SessionPlaybackRailView({ state, notice, display, onPlayback, readOnly = false, eligibility }: {
  state: SessionReadState; notice: string; display: ReturnType<typeof displayFacts>;
  onPlayback?: (action: {type:'previous'|'next'|'toggle-pause'|'toggle-mute'})=>unknown; readOnly?:boolean;
  eligibility?:{canPlayPause:boolean;canPrevious:boolean;canNext:boolean;canMute:boolean};
}) {
  const t = useSessionText();
  const runtime = state.runtime;
  const current = runtime ? findItem(state, state.activeItemId) : undefined;
  const playlist = runtime ? findPlaylist(state, state.activePlaylistId) : undefined;
  const loaded = runtime?.items?.find(item => item.id === state.activeItemId);
  const sceneIndex = runtime?.sceneIndex ?? runtime?.items?.findIndex(item => item.id === state.activeItemId) ?? -1;
  const sceneCount = runtime?.sceneCount ?? runtime?.itemIds?.length ?? runtime?.items?.length ?? 0;
  const playing = Boolean(runtime && !runtime.paused);
  const duration = runtime?.duration ?? (loaded && Number.isFinite(loaded.duration) && loaded.duration > 0 ? loaded.duration : 0);
  const time = formatRuntimeTime(duration ? runtime?.time ?? 0 : 0), total = formatRuntimeTime(duration);
  const master = Math.round((playlist?.masterVolume ?? 1) * 100);
  const context = current ? current.playlist.alias + ' · ' + current.item.alias : t('No media loaded');
  const summary = runtime ? context + ' · ' + t('Item') + ' ' + (sceneIndex + 1) + '/' + sceneCount
    + ' · ' + t(runtime.mediaKind ?? loaded?.kind ?? 'Unknown') + ' · ' + t(playing ? 'Playing' : 'Paused') + ' · ' + time + ' / ' + total
    + ' · ' + runtime.rate.toFixed(1) + '× · ' + t('Master') + ' ' + master + '%' + (state.outputMuted ? ' · ' + t('Muted') : '') : t('No media loaded');
  const displayLabel = display.descriptor ? t('Display') + ' ' + display.descriptor.displayNumber
    + (display.descriptor.friendlyName ? ' — ' + display.descriptor.friendlyName : '') + ' — '
    + display.descriptor.pixelWidth + '×' + display.descriptor.pixelHeight + (display.descriptor.primary ? ' (' + t('Primary') + ')' : '') : t(display.label);
  return <footer className="playback-rail playback-phase-c" aria-label={t('Session playback controls')} data-runtime-configured={Boolean(runtime)}
    data-active-playlist-id={runtime ? state.activePlaylistId ?? '' : ''} data-active-item-id={runtime ? state.activeItemId ?? '' : ''}
    data-actual-rate={eligibility&&runtime?.mediaKind!=='video'?'':runtime?.rate ?? 1} data-embedded-gain={eligibility?undefined:runtime?.volume ?? 1} data-output-muted={state.outputMuted}
    data-effective-output={eligibility?undefined:state.outputMuted ? 0 : runtime?.volume ?? 1} data-halted={runtime?.halted ?? false}>
    <div className="rail-display"><span title={displayLabel}>{t('DISPLAY')} {display.descriptor?.displayNumber ?? '—'}<span className="rail-dot">·</span><span className="rail-resolution">{display.descriptor ? display.descriptor.pixelWidth+' × '+display.descriptor.pixelHeight : t(display.availability === 'enumeration-failed' ? 'ENUMERATION FAILED' : 'UNAVAILABLE')}</span></span>
      <span className="session-active-context" title={context}>{context}</span>
      <span className="session-feedback" role="status" aria-live="polite" aria-atomic="true" title={t(notice)}>{t(notice)}</span>
    </div>
    <div className="playback-buttons" role="group" aria-label={t(readOnly ? 'Playback (read-only)' : eligibility?'Session playback controls':'Mock playback')}>
      <button type="button" className="playback-button" aria-label={t('Previous')} disabled={readOnly||eligibility?.canPrevious===false} onClick={() => onPlayback?.({ type: 'previous' })}><PlaybackIcon action="previous" /></button>
      <button type="button" className="playback-button playback-button--primary" aria-label={t(playing ? 'Pause' : 'Play')} disabled={readOnly||eligibility?.canPlayPause===false} onClick={() => onPlayback?.({ type: 'toggle-pause' })}><PlaybackIcon action={playing ? 'pause' : 'play'} /></button>
      <button type="button" className="playback-button" aria-label={t('Next')} disabled={readOnly||eligibility?.canNext===false} onClick={() => onPlayback?.({ type: 'next' })}><PlaybackIcon action="next" /></button>
    </div>
    <div className="rail-status" aria-label={summary} title={summary}>
      <div className="rail-runtime-status"><span className="playback-time">{time}<span>/</span>{total}</span>
        <span className="rail-scene">{runtime ? t('Item') + ' ' + (sceneIndex + 1) + '/' + sceneCount : ''}</span>
        <span className="rail-fps">{t(runtime?.mediaKind?.toUpperCase() ?? loaded?.kind.toUpperCase() ?? '')}</span>
        <span className={'playback-state' + (playing ? ' is-playing' : '')}>{t(runtime ? playing ? 'PLAYING' : 'PAUSED' : 'NO MEDIA')}</span>
        <span className="rail-rate">{eligibility?runtime?.mediaKind==='video'?runtime.rate.toFixed(1)+'×':'':runtime?runtime.rate.toFixed(1)+'×':''}</span>
      </div>
      <div className="rail-audio" hidden={!runtime&&!eligibility} data-playlist-id={playlist?.id ?? ''}>
        <button type="button" className="rail-mute" aria-label={t(state.outputMuted ? 'Unmute' : 'Mute')} aria-pressed={state.outputMuted}
          title={t(state.outputMuted ? 'Unmute output' : 'Mute output')} disabled={readOnly||eligibility?.canMute===false} onClick={() => onPlayback?.({ type: 'toggle-mute' })}>
          <svg width="15" height="15" viewBox="0 0 18 18" fill="none" aria-hidden="true"><path d="M3 7h3l4-3v10l-4-3H3V7Z" />
            {state.outputMuted ? <path d="m13 7 3 4m0-4-3 4" /> : <path d="M13 6c2 2 2 4 0 6" />}</svg>
          <span>{t(state.outputMuted ? 'Muted' : 'Mute')}</span>
        </button>
      </div>
    </div>
  </footer>;
}
