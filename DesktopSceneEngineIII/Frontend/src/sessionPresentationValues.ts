export function positiveNativeInt(value: number): boolean {
  return Number.isInteger(value) && value > 0 && value <= 2_147_483_647;
}

// AspectRatio::from reduces integer components. The Inspector UI imposes its
// narrower 1..10000 input bounds; the underlying value type accepts int32.
export function normalizedAspect(numerator: number, denominator: number): { numerator: number; denominator: number } | null {
  if (!positiveNativeInt(numerator) || !positiveNativeInt(denominator)) return null;
  let left = numerator;
  let right = denominator;
  while (right) [left, right] = [right, left % right];
  return { numerator: numerator / left, denominator: denominator / left };
}
