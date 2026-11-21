import type { SessionPresentation } from './sessionTypes.ts';

// Existing authored defaults, copied into each independent UI mock occurrence.
export function defaultPresentation(): SessionPresentation {
  return { crop: { x: 0, y: 0, width: 1, height: 1 },
    layout: { positionX: 0, positionY: 0, scaleX: 1, scaleY: 1 },
    mediaPresentation: { mode: 'auto-fill', customAspect: { numerator: 16, denominator: 9 } } };
}
