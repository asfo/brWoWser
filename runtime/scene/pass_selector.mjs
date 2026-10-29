export async function createPassRuntime(wasm, model, skin) {
  if (model.byteLength > 2097152 || skin.byteLength > 262144) throw Error('pass input capacity');
  const { instance } = await WebAssembly.instantiate(wasm, {}),
    e = instance.exports;
  new Uint8Array(e.memory.buffer, e.model_ptr(), model.byteLength).set(model);
  new Uint8Array(e.memory.buffer, e.skin_ptr(), skin.byteLength).set(skin);
  const status = e.resolve(model.byteLength, skin.byteLength);
  if (status) throw Error('unsupported pass profile ' + status);
  const resolved = new Uint32Array(e.memory.buffer, e.output_ptr(), e.batch_count() * 4).slice();
  return {
    resolved,
    combine(kind, t0, t1, diffuse) {
      if ([t0, t1, diffuse].some((a) => a.length !== 4)) throw Error('RGBA dimensions');
      new Float32Array(e.memory.buffer, e.inputs_ptr(), 12).set([...t0, ...t1, ...diffuse]);
      const code = e.combine(kind);
      if (code) throw Error('combiner rejected ' + code);
      return new Float32Array(e.memory.buffer, e.rgba_ptr(), 4).slice();
    },
  };
}
export function combinerKind(word, count) {
  const a = (word >>> 4) & 7,
    b = word & 7;
  if (count === 1) return a === 0 ? 0 : a === 1 ? 1 : -1;
  if (count === 2) {
    if (a === 0 && b === 4) return 2;
    if (a === 1 && b === 4) return 3;
    if (a === 1 && b === 1) return 4;
  }
  return -1;
}
