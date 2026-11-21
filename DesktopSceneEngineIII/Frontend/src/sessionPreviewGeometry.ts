// Fixed transparent breathing room for TS-owned outside chrome, in both states.
// It is not a visible legacy neutral frame or a port of Qt drawing.
export const FRAME_MARGIN = 4;

export type PreviewRect = { x: number; y: number; width: number; height: number };
export type PreviewSize = { width: number; height: number };
export type PreviewGeometry = {
  visible: boolean;
  frame: PreviewRect;
  native: PreviewRect;
  surfaceInFrame: PreviewRect;
};

const INT_MAX = 2147483647;
const emptyRect = (): PreviewRect => ({ x: 0, y: 0, width: 0, height: 0 });

// ResizeObserver may measure fractional CSS pixels. QSize uses integer logical
// pixels; round the adapter measurement down before applying production fit.
function logicalExtent(value: number): number {
  return Number.isFinite(value) && value >= 0 && value <= INT_MAX ? Math.floor(value) : 0;
}

// CanvasPresentationGeometry.h: constrained extent rounds down and odd slack
// stays at the bottom/right. BigInt preserves the C++ int64 intermediate math.
export function fitCanvasInsidePreviewViewport(viewport: PreviewSize, aspect: readonly [number, number]): PreviewRect {
  const width = logicalExtent(viewport.width), height = logicalExtent(viewport.height);
  if (width <= 0 || height <= 0 || aspect.some(value => !Number.isInteger(value) || value <= 0 || value > INT_MAX)) return emptyRect();

  const canvasWidth = BigInt(aspect[0]), canvasHeight = BigInt(aspect[1]);
  const viewportWidth = BigInt(width), viewportHeight = BigInt(height);
  let fittedWidth = width, fittedHeight = height;
  if (viewportWidth * canvasHeight > viewportHeight * canvasWidth) {
    fittedWidth = Number(viewportHeight * canvasWidth / canvasHeight);
  } else {
    fittedHeight = Number(viewportWidth * canvasHeight / canvasWidth);
  }
  return {
    x: Math.floor((width - fittedWidth) / 2),
    y: Math.floor((height - fittedHeight) / 2),
    width: fittedWidth,
    height: fittedHeight,
  };
}

// PreviewViewportHost subtracts the permanent chrome margins before fitting,
// then expands the frame around that fitted content. Both returned frame and
// native rectangles use viewport coordinates; surfaceInFrame is child-local.
// Hidden rectangles have no render authority, so return safe zero bounds.
export function previewGeometry(viewport: PreviewSize, aspect: readonly [number, number]): PreviewGeometry {
  const fitted = fitCanvasInsidePreviewViewport({
    width: logicalExtent(viewport.width) - 2 * FRAME_MARGIN,
    height: logicalExtent(viewport.height) - 2 * FRAME_MARGIN,
  }, aspect);
  if (fitted.width <= 0 || fitted.height <= 0) {
    return { visible: false, frame: emptyRect(), native: emptyRect(), surfaceInFrame: emptyRect() };
  }
  return {
    visible: true,
    frame: { ...fitted, width: fitted.width + 2 * FRAME_MARGIN, height: fitted.height + 2 * FRAME_MARGIN },
    native: { ...fitted, x: fitted.x + FRAME_MARGIN, y: fitted.y + FRAME_MARGIN },
    surfaceInFrame: { ...fitted, x: FRAME_MARGIN, y: FRAME_MARGIN },
  };
}
