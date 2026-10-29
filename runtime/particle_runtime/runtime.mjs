/* Transport and browser clock admission only. Simulation, original records,
 * attachment construction, random consumption and generated quads are C/WASM. */
export async function createParticleRuntime({
  wasm,
  model,
  randomTable,
  seed = 0x42525753,
  boneCount,
  sequenceDurationMs,
}) {
  if (
    !(model instanceof Uint8Array) ||
    !(randomTable instanceof Uint8Array) ||
    randomTable.length !== 256 ||
    !Number.isInteger(sequenceDurationMs) ||
    sequenceDurationMs < 1
  )
    throw Error('invalid particle resources');
  const {
    instance: { exports: e },
  } = await WebAssembly.instantiate(wasm, {});
  new Uint8Array(e.memory.buffer, e.input_ptr(), model.length).set(model);
  new Uint8Array(e.memory.buffer, e.runtime_table_ptr(), 256).set(randomTable);
  let previousGlobal = null,
    lastSequence = null;
  function reset() {
    const status = e.runtime_reset(model.length, seed, boneCount);
    if (status) throw Error('particle reset rejected ' + status);
    previousGlobal = null;
    lastSequence = null;
  }
  reset();
  return {
    sample({ sequenceMs, globalMs, matrices, boneValid, view }) {
      if (
        !Number.isInteger(sequenceMs) ||
        sequenceMs < 0 ||
        !Number.isInteger(globalMs) ||
        globalMs < 0 ||
        !(matrices instanceof Float32Array) ||
        matrices.length !== boneCount * 16 ||
        boneValid.length !== boneCount ||
        view.length !== 16
      )
        throw Error('invalid particle sample');
      if (
        previousGlobal !== null &&
        (globalMs < previousGlobal || globalMs - previousGlobal > 1000)
      )
        reset();
      new Float32Array(e.memory.buffer, e.runtime_bones_ptr(), matrices.length).set(matrices);
      new Uint8Array(e.memory.buffer, e.runtime_bone_valid_ptr(), boneCount).set(boneValid);
      new Float32Array(e.memory.buffer, e.runtime_view_ptr(), 16).set(view);
      // Large discontinuities deliberately restart instead of simulating history
      // with a single current palette. Ordinary animation wraps retain the pool.
      let remaining = previousGlobal === null ? 0 : globalMs - previousGlobal,
        elapsed = 0;
      if (previousGlobal === null) {
        const status = e.runtime_step(sequenceMs, 0);
        if (status) throw Error('particle initial step rejected ' + status);
      }
      while (remaining > 0) {
        const step = Math.min(remaining, 100);
        elapsed += step;
        const clock = (lastSequence + elapsed) % sequenceDurationMs,
          status = e.runtime_step(clock, step / 1000);
        if (status) throw Error('particle step rejected ' + status);
        remaining -= step;
      }
      const code = e.runtime_draw();
      if (code) throw Error('particle draw rejected ' + code);
      previousGlobal = globalMs;
      lastSequence = sequenceMs;
      const s = Array.from(new Uint32Array(e.memory.buffer, e.runtime_stats_ptr(), 8)),
        raw = new Uint32Array(e.memory.buffer, e.runtime_batches_ptr(), s[1] * 5),
        batches = Array.from({ length: s[1] }, (_, i) => ({
          emitter: raw[i * 5],
          texture: raw[i * 5 + 1],
          blend: raw[i * 5 + 2],
          start: raw[i * 5 + 3],
          count: raw[i * 5 + 4],
        }));
      return {
        vertices: new Float32Array(e.memory.buffer, e.runtime_vertices_ptr(), s[0] * 9).slice()
          .buffer,
        batches,
        stats: {
          vertices: s[0],
          batches: s[1],
          live: s[2],
          omitted: s[3],
          emitters: s[4],
          pool_capacity_per_emitter: 256,
          storage_policy: 'fresh-zero-low-word',
          appearance_policy: 'original-flat-curves',
          scope:
            'Selected original record driven C/WASM simulation; allocator/entropy, packed appearance optimization and whole native scene pixels remain unvalidated.',
        },
      };
    },
    reset,
  };
}
