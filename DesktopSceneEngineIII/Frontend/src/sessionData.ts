// Shared formatting only. Authored Item occurrences are projected separately;
// the former global Media/flat Playlist and invented properties were retired.
export function formatTime(seconds: number | null): string {
  if (seconds === null) return '—';
  return String(Math.floor(seconds / 60)).padStart(2, '0') + ':' + String(seconds % 60).padStart(2, '0');
}
