// Keep UI units proportional while the scene camera follows the real aspect.
export function gameViewport(width, height, dpr = 1) {
  if (![width, height, dpr].every(Number.isFinite) || width <= 0 || height <= 0 || dpr <= 0)
    throw Error('invalid game viewport');
  const scale = Math.min(dpr, 2),
    logicalHeight = 768,
    logicalWidth = Math.max(256, Math.min(8192, (768 * width) / height));
  return {
    logicalWidth,
    logicalHeight,
    width: Math.max(1, Math.round(width * scale)),
    height: Math.max(1, Math.round(height * scale)),
  };
}
export function applyGameViewport(canvas, viewport) {
  canvas.width = viewport.width;
  canvas.height = viewport.height;
  canvas.dataset.logicalWidth = String(viewport.logicalWidth);
  canvas.dataset.logicalHeight = String(viewport.logicalHeight);
}
