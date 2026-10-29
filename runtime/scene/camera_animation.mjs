export async function createCameraPositionSampler(wasm, model) {
  const { instance } = await WebAssembly.instantiate(wasm, {}),
    e = instance.exports;
  if (model.byteLength > 2097152) throw Error('model capacity');
  new Uint8Array(e.memory.buffer, e.input_ptr(), model.byteLength).set(model);
  return (ms) => {
    if (!Number.isInteger(ms) || ms < 0 || ms > 0xffffffff) throw Error('invalid sample time');
    const status = e.sample(model.byteLength, ms);
    if (status) throw Error('camera sample rejected: ' + status);
    return Array.from(new Float32Array(e.memory.buffer, e.output_ptr(), 3));
  };
}
