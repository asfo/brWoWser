import { instantiate } from '../wasm/instantiate.mjs';
export async function createUVTranslationRuntime(wasm, model) {
  if (model.byteLength < 0x138 || model.byteLength > 2097152) throw Error('UV model capacity');
  const { instance } = await instantiate(wasm),
    e = instance.exports;
  new Uint8Array(e.memory.buffer, e.model_ptr(), model.byteLength).set(model);
  const v = new DataView(model.buffer, model.byteOffset, model.byteLength),
    globals = v.getUint32(20, true);
  if (globals > 256) throw Error('UV clock capacity');
  function finish(status) {
    if (status) throw Error('UV profile rejected ' + status);
    const info = new Uint32Array(e.memory.buffer, e.info_ptr(), 3);
    return {
      matrices: new Float32Array(e.memory.buffer, e.matrices_ptr(), info[0] * 16).slice(),
      count: info[0],
      globalCount: info[1],
      scope:
        'Selected global translation-only tracks from original local M2; explicit clock values, native runtime phase/output unverified.',
    };
  }
  return {
    sample(ms) {
      if (!Number.isInteger(ms) || ms < 0 || ms > 0xffffffff) throw Error('invalid UV time');
      return finish(e.sample(model.byteLength, ms));
    },
    sampleScroll(ms) {
      if (!Number.isInteger(ms) || ms < 0 || ms > 0xffffffff) throw Error('invalid UV time');
      const status = e.sample_scroll(model.byteLength, ms);
      if (status) return null;
      const info = new Uint32Array(e.memory.buffer, e.info_ptr(), 3);
      return new Float32Array(e.memory.buffer, e.matrices_ptr(), info[0] * 16).slice();
    },
    sampleClocks(values) {
      if (
        values.length !== globals ||
        !Array.from(values).every((x) => Number.isInteger(x) && x >= 0 && x <= 0xffffffff)
      )
        throw Error('invalid UV clocks');
      new Uint32Array(e.memory.buffer, e.clocks_ptr(), globals).set(values);
      return finish(e.sample_clocks(model.byteLength));
    },
  };
}
