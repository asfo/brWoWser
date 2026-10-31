import { sharedExports } from '../wasm/shared.mjs';
const owners = new WeakMap();
export async function createBonePoseRuntime(wasm, model, prepared, { normalizer = 0 } = {}) {
  if (model.byteLength > 8388608 || prepared.byteLength % 48 || prepared.byteLength > 65536 * 48)
    throw Error('pose input capacity');
  const e = await sharedExports(wasm),
    count = prepared.byteLength / 48;
  let modelLength = model.byteLength,
    upper = [1, 0],
    closed = false;
  const context = {};
  function bind() {
    if (closed) throw Error('Bone pose is closed');
    if (owners.get(e) !== context) {
      new Uint8Array(e.memory.buffer, e.model_ptr(), model.length).set(model);
      new Uint8Array(e.memory.buffer, e.prepared_ptr(), prepared.length).set(prepared);
      owners.set(e, context);
    }
    if (e.set_normalizer(normalizer) || e.set_upper_body(...upper))
      throw Error('Pose context rejected');
  }
  if (e.set_normalizer(normalizer)) throw Error('invalid normalizer mode');
  bind();
  return {
    setUpperBody(cosine = 1, sine = 0) {
      if (e.set_upper_body(cosine, sine)) throw Error('Upper-body orientation rejected');
      upper = [cosine, sine];
    },
    setModel(bytes) {
      if (!(bytes instanceof Uint8Array) || bytes.length < 0x138 || bytes.length > 8388608)
        throw Error('pose model capacity');
      model = bytes;
      modelLength = bytes.length;
      if (owners.get(e) === context) owners.delete(e);
    },
    dispose() {
      closed = true;
      if (owners.get(e) === context) owners.delete(e);
      model = prepared = null;
    },
    sample(sequenceMs, globalMs, view) {
      bind();
      for (const ms of [sequenceMs, globalMs])
        if (!Number.isInteger(ms) || ms < 0 || ms > 0xffffffff) throw Error('invalid pose time');
      if (view) {
        if (view.length !== 16 || !Array.from(view).every(Number.isFinite))
          throw Error('invalid pose camera');
        new Float32Array(e.memory.buffer, e.view_ptr(), 16).set(view);
      }
      if (e.set_camera?.(view ? 1 : 0)) throw Error('pose camera rejected');
      const p = e.pose(modelLength, sequenceMs, globalMs);
      if (p) throw Error('pose rejected ' + p);
      const s = e.skin(count);
      if (s) throw Error('skin rejected ' + s);
      const nb = e.bone_count();
      return {
        positions: new Float32Array(e.memory.buffer, e.positions_ptr(), count * 3).slice(),
        normals: e.normals_ptr
          ? new Float32Array(e.memory.buffer, e.normals_ptr(), count * 3).slice()
          : null,
        vertexValid: new Uint8Array(e.memory.buffer, e.vertex_valid_ptr(), count).slice(),
        matrices: new Float32Array(e.memory.buffer, e.matrices_ptr(), nb * 16).slice(),
        boneValid: new Uint8Array(e.memory.buffer, e.bone_valid_ptr(), nb).slice(),
        scope:
          'Subset flags0/512, explicit sequence/global clocks; unsupported bones and their vertices are masked. Native matrix/pixel equivalence pending.',
      };
    },
  };
}
