import type { SequenceMode } from './sessionPlaybackSequence.ts';

// DSE-owned paths drawn against the supplied reference. Both loop modes use
// this exact base; Random retains the separate curved crossing topology.
const repeatBase = ['M3 11V6.5h18', 'm18.5 4 2.5 2.5-2.5 2.5', 'M21 13v4.5H3', 'm5.5 15-2.5 2.5 2.5 2.5'];
const shufflePaths = ['M3 6.5h3c4 0 8 11 12 11h3', 'M3 17.5h3c4 0 8-11 12-11h3', 'm18.5 4 2.5 2.5-2.5 2.5', 'm18.5 15 2.5 2.5-2.5 2.5'];
export function PlaybackModeIcon({ mode }: { mode: SequenceMode | '' }) {
  return <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor"
    strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" data-playback-mode-icon={mode}>
    {mode === 'random' ? shufflePaths.map(d => <path key={d} d={d} />) : repeatBase.map(d => <path key={d} d={d} data-repeat-base />)}
    {mode === 'repeat-current' && <path d="m10.5 10.5 1.5-1v5m-1.5 0h3" />}
  </svg>;
}
