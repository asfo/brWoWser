// Selected browser login loop policy. Native transition/clock equivalence separate.
export function sceneClocks(elapsed, duration) {
  if (!Number.isFinite(elapsed) || elapsed < 0 || !Number.isSafeInteger(duration) || duration < 1)
    throw Error('Invalid scene clock');
  const globalMs = Math.floor(elapsed);
  if (globalMs > 0xffffffff) throw Error('Global clock range');
  return { sequenceMs: globalMs % duration, globalMs };
}
