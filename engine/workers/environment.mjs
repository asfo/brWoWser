const node = typeof process === 'object' && !!process.versions?.node;
const port = node ? (await import('node:worker_threads')).parentPort : null;
export const send = (value) => (node ? port.postMessage(value) : postMessage(value));
export const listen = (handler) =>
  node ? port.on('message', handler) : addEventListener('message', (event) => handler(event.data));
