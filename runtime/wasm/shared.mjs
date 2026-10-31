import { instantiate } from './instantiate.mjs';
// One synchronous workspace per compiled decoder. Callers must copy returned
// views and rebind their own model/state before using a shared workspace.
const workspaces = new WeakMap();
export function sharedExports(wasm) {
  if (!wasm || (typeof wasm !== 'object' && typeof wasm !== 'function'))
    throw Error('WASM decoder is unavailable');
  let pending = workspaces.get(wasm);
  if (!pending) {
    pending = instantiate(wasm).then(({ instance }) => instance.exports);
    workspaces.set(wasm, pending);
    pending.catch(() => workspaces.delete(wasm));
  }
  return pending;
}
