export async function createBillboard64Runtime(wasm, model, prepared) {
  if (model.length > 2097152 || prepared.length % 48 || prepared.length > 65536 * 48)
    throw Error('billboard capacity');
  const { instance } = await WebAssembly.instantiate(wasm, {}),
    e = instance.exports,
    count = prepared.length / 48;
  new Uint8Array(e.memory.buffer, e.model_ptr(), model.length).set(model);
  new Uint8Array(e.memory.buffer, e.prepared_ptr(), prepared.length).set(prepared);
  return (world) => {
    if (world.length !== 16 || !world.every(Number.isFinite)) throw Error('invalid world matrix');
    new Float32Array(e.memory.buffer, e.world_ptr(), 16).set(world);
    const status = e.prepare_billboards(model.length, count);
    if (status) throw Error('billboard profile rejected ' + status);
    const n = e.bone_count();
    return {
      positions: new Float32Array(e.memory.buffer, e.positions_ptr(), count * 3).slice(),
      vertexValid: new Uint8Array(e.memory.buffer, e.vertex_valid_ptr(), count).slice(),
      matrices: new Float32Array(e.memory.buffer, e.matrices_ptr(), n * 16).slice(),
      boneValid: new Uint8Array(e.memory.buffer, e.bone_valid_ptr(), n).slice(),
      scope:
        'Root flag64/static source profile and explicit world basis; native billboard matrix/pixel comparison pending.',
    };
  };
}
