import type { SessionPresentation } from './sessionTypes.ts';
import type { TimelineHoverSource } from './sessionTimelineHover.ts';

export type HoverRectangle = { x: number; y: number; width: number; height: number };
export type HoverComposition = { sourceUv: HoverRectangle; destination: HoverRectangle };
export type HoverMockFrame = { width: number; height: number; sourceId: string; sourcePath: string; identity: string; seed: number };

// Explicit synthetic source dimensions, never media metadata or a decode result.
// The portrait still and landscape video make containment/cover differences visible.
export function createHoverMockFrame(source: TimelineHoverSource, imageId: string): HoverMockFrame {
  const identity = JSON.stringify([source.itemId, source.source, source.mediaType]);
  let seed = 0;
  for (const character of identity + imageId) seed = (seed * 31 + character.charCodeAt(0)) >>> 0;
  return { width: source.mediaType === 'image' ? 240 : 320, height: source.mediaType === 'image' ? 320 : 180,
    sourceId: source.itemId, sourcePath: source.source, identity, seed };
}

const f = Math.fround;
const positive = (value: number) => Number.isFinite(value) && value > 0;
const positiveInt = (value: number) => Number.isInteger(value) && value > 0 && value <= 2147483647;
const full = (): HoverRectangle => ({ x: 0, y: 0, width: 1, height: 1 });

// Literal float32 port of PresentationGeometry.h. The final pixel mapping uses
// doubles, matching composeTimelineCanvasPreview's normalized-rectangle conversion.
function fitAspect(contentAspect: number, canvasAspect: number): HoverRectangle {
  if (contentAspect > canvasAspect) {
    const height = f(canvasAspect / contentAspect);
    return { x: 0, y: f(f(1 - height) * .5), width: 1, height };
  }
  const width = f(contentAspect / canvasAspect);
  return { x: f(f(1 - width) * .5), y: 0, width, height: 1 };
}
function cropToAspect(crop: HoverRectangle, sourceAspect: number, targetAspect: number): HoverRectangle {
  const croppedAspect = f(f(sourceAspect * crop.width) / crop.height);
  if (croppedAspect > targetAspect) {
    const width = f(f(crop.height * targetAspect) / sourceAspect);
    return { x: f(crop.x + f(f(crop.width - width) * .5)), y: crop.y, width, height: crop.height };
  }
  const height = f(f(crop.width * sourceAspect) / targetAspect);
  return { x: crop.x, y: f(crop.y + f(f(crop.height - height) * .5)), width: crop.width, height };
}
function applyLayout(baseline: HoverRectangle, layout: SessionPresentation['layout']): HoverRectangle {
  const width = f(baseline.width * layout.scaleX), height = f(baseline.height * layout.scaleY);
  return {
    x: f(f(baseline.x + f(f(baseline.width - width) * .5)) + layout.positionX),
    y: f(f(baseline.y + f(f(baseline.height - height) * .5)) - layout.positionY),
    width, height,
  };
}
export function resolveHoverComposition(
  sourceWidth: number, sourceHeight: number, canvasWidth: number, canvasHeight: number,
  presentation: SessionPresentation,
): HoverComposition | null {
  const crop = { x: f(presentation.crop.x), y: f(presentation.crop.y), width: f(presentation.crop.width), height: f(presentation.crop.height) };
  const layout = { positionX: f(presentation.layout.positionX), positionY: f(presentation.layout.positionY), scaleX: f(presentation.layout.scaleX), scaleY: f(presentation.layout.scaleY) };
  if (![sourceWidth, sourceHeight, canvasWidth, canvasHeight].every(positiveInt)
    || !Object.values(crop).every(Number.isFinite) || !positive(crop.width) || !positive(crop.height)
    || crop.x < 0 || crop.y < 0 || f(crop.x + crop.width) > 1 || f(crop.y + crop.height) > 1
    || !Number.isFinite(layout.positionX) || !Number.isFinite(layout.positionY) || !positive(layout.scaleX) || !positive(layout.scaleY)) return null;
  const sourceAspect = f(f(sourceWidth) / f(sourceHeight)), canvasAspect = f(f(canvasWidth) / f(canvasHeight));
  if (!positive(sourceAspect) || !positive(canvasAspect)) return null;
  switch (presentation.mediaPresentation.mode) {
    case 'native':
      return { sourceUv: crop, destination: applyLayout(fitAspect(f(f(sourceAspect * crop.width) / crop.height), canvasAspect), layout) };
    case 'auto-fill':
      return { sourceUv: cropToAspect(crop, sourceAspect, canvasAspect), destination: applyLayout(full(), layout) };
    case 'custom': {
      const { numerator, denominator } = presentation.mediaPresentation.customAspect;
      if (!positiveInt(numerator) || !positiveInt(denominator)) return null;
      const customAspect = f(f(numerator) / f(denominator));
      if (!positive(customAspect)) return null;
      return { sourceUv: cropToAspect(crop, sourceAspect, customAspect), destination: applyLayout(fitAspect(customAspect, canvasAspect), layout) };
    }
  }
  return null;
}
