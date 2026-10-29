// Canvas coordinates only. Hit selection, scrolling and callbacks belong to C.
export function canvasAction(canvas, event, kind) {
  const box = canvas.getBoundingClientRect();
  if (!(box.width > 0 && box.height > 0)) throw Error('Invalid canvas bounds');
  const logicalWidth = Number(canvas.dataset?.logicalWidth) || 1024,
    logicalHeight = Number(canvas.dataset?.logicalHeight) || 768;
  let x = ((event.clientX - box.left) * logicalWidth) / box.width,
    y = ((box.bottom - event.clientY) * logicalHeight) / box.height;
  if (!Number.isFinite(x) || !Number.isFinite(y)) return null;
  if (kind === 6 || kind === 7) {
    x = Math.max(0, Math.min(logicalWidth, x));
    y = Math.max(0, Math.min(logicalHeight, y));
  } else if (x < 0 || x > logicalWidth || y < 0 || y > logicalHeight) return null;
  const delta = kind === 2 ? -Math.sign(event.deltaY) : 0;
  if (kind === 2 && !delta) return null;
  return { kind, x, y, delta };
}
export function connectCanvasInput(
  canvas,
  send,
  onResult,
  onError,
  getItems = () => [],
  presentPressed = () =>
    new Promise((resolve) => {
      // Two animation frames guarantee that the down surface survives one paint,
      // even when pointerup arrived while C was preparing the down packet.
      if (typeof requestAnimationFrame !== 'function') {
        resolve();
        return;
      }
      requestAnimationFrame(() => requestAnimationFrame(resolve));
    }),
) {
  let closed = false,
    queue = Promise.resolve(),
    pendingWheel = null,
    pendingMove = null,
    dragging = false,
    suppressClick = false,
    activePointer = null,
    pressedHit = null,
    lastHover = null;
  const push = (action) => {
    // Keep click ordering, but combine waiting wheel deltas rather than rendering
    // the entire legal document once for every high-frequency trackpad event.
    if (!action) return;
    if (
      action.kind === 2 &&
      pendingWheel &&
      pendingWheel.x === action.x &&
      pendingWheel.y === action.y &&
      Math.abs(pendingWheel.delta + action.delta) <= 16
    ) {
      pendingWheel.delta += action.delta;
      return;
    }
    if (action.kind === 6 && pendingMove) {
      pendingMove.x = action.x;
      pendingMove.y = action.y;
      return;
    }
    pendingMove = action.kind === 6 ? action : null;
    pendingWheel = action.kind === 2 ? action : null;
    queue = queue
      .then(async () => {
        if (pendingWheel === action) pendingWheel = null;
        if (pendingMove === action) pendingMove = null;
        if (closed || (action.kind === 2 && !action.delta)) return;
        const result = await send(action);
        await onResult(result);
        if (action.kind === 5) await presentPressed();
      })
      .catch((error) => {
        onError(error);
      });
  };
  const click = (event) => {
    if (closed || event.button !== 0) return;
    if (suppressClick) {
      suppressClick = false;
      return;
    }
    try {
      const action = canvasAction(canvas, event, 1);
      if (action) {
        event.preventDefault();
        canvas.focus?.({ preventScroll: true });
        push(action);
      }
    } catch (error) {
      onError(error);
    }
  };
  const wheel = (event) => {
    if (closed) return;
    try {
      const action = canvasAction(canvas, event, 2);
      if (action) {
        event.preventDefault();
        push(action);
      }
    } catch (error) {
      onError(error);
    }
  };
  const hit = (event) => {
    const action = canvasAction(canvas, event, 1);
    if (!action) return null;
    return getItems()
      .filter(
        (r) =>
          r.visible &&
          r.enabled &&
          ([2, 3, 4, 6].includes(r.type) || r.mouseEnabled) &&
          !r.geometryStatus &&
          action.x >= r.rect[0] &&
          action.x < r.rect[0] + r.rect[2] &&
          action.y >= r.rect[1] &&
          action.y < r.rect[1] + r.rect[3] &&
          (!r.clip ||
            (action.x >= r.clip[0] &&
              action.x < r.clip[0] + r.clip[2] &&
              action.y >= r.clip[1] &&
              action.y < r.clip[1] + r.clip[3])),
      )
      .sort((a, b) => b.stratum - a.stratum || b.level - a.level || b.object - a.object)[0];
  };
  const down = (event) => {
    if (closed || event.button !== 0) return;
    const row = hit(event);
    if (!row || (![2, 3, 6].includes(row.type) && !row.mouseEnabled)) return;
    event.preventDefault();
    canvas.focus?.({ preventScroll: true });
    dragging = true;
    pressedHit = row;
    suppressClick = ![2, 3].includes(row.type);
    activePointer = event.pointerId;
    canvas.setPointerCapture?.(event.pointerId);
    push(canvasAction(canvas, event, 5));
  };
  const move = (event) => {
    if (closed) return;
    if (dragging) {
      event.preventDefault();
      push(canvasAction(canvas, event, 6));
      return;
    }
    const row = hit(event);
    if (canvas.style && !canvas.dataset?.softwareCursor)
      canvas.style.cursor = row?.type === 4 ? 'text' : row ? 'pointer' : 'default';
    const hovered = [2, 3].includes(row?.type) ? row.object : null;
    if (hovered !== lastHover) {
      lastHover = hovered;
      const action = canvasAction(canvas, event, 8);
      if (action) push(action);
    }
  };
  const up = (event) => {
    if (!dragging || closed) return;
    dragging = false;
    if ([2, 3].includes(pressedHit?.type)) suppressClick = hit(event)?.object !== pressedHit.object;
    pressedHit = null;
    push(canvasAction(canvas, event, 7));
    if (canvas.hasPointerCapture?.(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
    activePointer = null;
  };
  const cancelDrag = () => {
    if (closed) return;
    const wasDragging = dragging;
    dragging = false;
    pressedHit = null;
    suppressClick = false;
    lastHover = null;
    push({ kind: 8, x: -1, y: -1, delta: 0 });
    if (wasDragging) push({ kind: 7, x: 0, y: 0, delta: 0 });
    if (activePointer !== null && canvas.hasPointerCapture?.(activePointer))
      canvas.releasePointerCapture(activePointer);
    activePointer = null;
  };
  const lostCapture = () => {
    if (dragging) cancelDrag();
  };
  const leave = () => {
    if (!dragging && lastHover !== null) {
      lastHover = null;
      push({ kind: 8, x: -1, y: -1, delta: 0 });
    }
  };
  canvas.addEventListener('pointerleave', leave);
  canvas.addEventListener('blur', cancelDrag);
  canvas.addEventListener('lostpointercapture', lostCapture);
  canvas.addEventListener('pointerdown', down);
  canvas.addEventListener('pointermove', move);
  canvas.addEventListener('pointerup', up);
  canvas.addEventListener('pointercancel', cancelDrag);
  canvas.addEventListener('click', click);
  canvas.addEventListener('wheel', wheel, { passive: false });
  return {
    idle: () => queue,
    dispose() {
      closed = true;
      canvas.removeEventListener('pointerleave', leave);
      canvas.removeEventListener('blur', cancelDrag);
      canvas.removeEventListener('lostpointercapture', lostCapture);
      canvas.removeEventListener('pointerdown', down);
      canvas.removeEventListener('pointermove', move);
      canvas.removeEventListener('pointerup', up);
      canvas.removeEventListener('pointercancel', cancelDrag);
      canvas.removeEventListener('click', click);
      canvas.removeEventListener('wheel', wheel);
    },
  };
}
