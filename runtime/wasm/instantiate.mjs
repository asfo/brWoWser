// Share immutable compiled code while preserving each consumer's private memory.
const compiled = new WeakMap();
export async function instantiate(wasm, imports = {}) {
  if (wasm instanceof WebAssembly.Module)
    return { instance: await WebAssembly.instantiate(wasm, imports) };
  let module = compiled.get(wasm);
  if (!module) {
    module = WebAssembly.compile(wasm);
    compiled.set(wasm, module);
    module.catch(() => compiled.delete(wasm));
  }
  return { instance: await WebAssembly.instantiate(await module, imports) };
}
