export function formatRuntimeTime(seconds: number): string {
  const total = Math.floor(Number.isFinite(seconds) && seconds >= 0 ? Math.min(seconds, 9_223_372_036_854_775_807) : 0);
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor(total / 60) % 60;
  const remainder = total % 60;
  const pad = (value: number) => String(value).padStart(2, '0');
  return `${hours > 0 ? `${pad(hours)}:` : ''}${pad(minutes)}:${pad(remainder)}`;
}
