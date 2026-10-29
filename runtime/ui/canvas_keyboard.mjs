// Browser input boundary only. Text state, selection and handlers stay in C.
// Initial ASCII keyboard/paste profile; IME/composition is explicitly pending.
const keys = {
  Backspace: 2,
  Delete: 3,
  ArrowLeft: 4,
  ArrowRight: 5,
  Home: 6,
  End: 7,
  Tab: 8,
  Enter: 9,
  Escape: 10,
};
export function keyboardAction(event) {
  if (
    event.isComposing ||
    event.altKey ||
    (event.metaKey && event.key.toLowerCase() !== 'a') ||
    (event.ctrlKey && event.key.toLowerCase() !== 'a')
  )
    return null;
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'a')
    return { kind: 4, operation: 11 };
  if (keys[event.key]) return { kind: 4, operation: keys[event.key] };
  if (event.key.length === 1 && event.key.charCodeAt(0) >= 32 && event.key.charCodeAt(0) <= 126)
    return { kind: 3, text: event.key };
  return null;
}
export function connectCanvasKeyboard(canvas, send, onError) {
  let closed = false,
    queue = Promise.resolve();
  const push = (action) => {
    queue = queue
      .then(() => (closed ? undefined : send(action)))
      .catch((error) => {
        onError(error);
      });
  };
  const keydown = (event) => {
    if (closed) return;
    const action = keyboardAction(event);
    if (action) {
      event.preventDefault();
      push(action);
    }
  };
  const paste = (event) => {
    if (closed) return;
    const text = event.clipboardData?.getData('text/plain');
    if (typeof text !== 'string') return;
    event.preventDefault();
    if (text.length > 4096 || /[^\x20-\x7e]/.test(text)) {
      onError(Error('Selected input profile accepts ASCII text only'));
      return;
    }
    push({ kind: 3, text });
  };
  canvas.addEventListener('keydown', keydown);
  canvas.addEventListener('paste', paste);
  canvas.tabIndex = 0;
  return {
    idle: () => queue,
    dispose() {
      closed = true;
      canvas.removeEventListener('keydown', keydown);
      canvas.removeEventListener('paste', paste);
    },
  };
}
