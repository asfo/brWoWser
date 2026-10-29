export async function createOpacityRuntime(wasm, model, skin) {
  if (
    !(model instanceof Uint8Array) ||
    !(skin instanceof Uint8Array) ||
    model.length > 2097152 ||
    skin.length > 524288
  )
    throw Error('invalid opacity inputs');
  const { instance } = await WebAssembly.instantiate(wasm, {}),
    e = instance.exports,
    d = new DataView(model.buffer, model.byteOffset, model.byteLength),
    gc = d.getUint32(0x14, true),
    go = d.getUint32(0x18, true);
  if (gc <= 10 || go > model.length || gc > (model.length - go) / 4)
    throw Error('invalid global sequence table');
  const global10Duration = d.getUint32(go + 40, true);
  if (!global10Duration) throw Error('invalid global sequence duration');
  new Uint8Array(e.memory.buffer, e.model_ptr(), model.length).set(model);
  new Uint8Array(e.memory.buffer, e.skin_ptr(), skin.length).set(skin);
  return {
    global10Duration,
    sample(sequenceMs, globalMs) {
      if (
        !Number.isInteger(sequenceMs) ||
        sequenceMs < 0 ||
        sequenceMs > 0xffffffff ||
        !Number.isInteger(globalMs) ||
        globalMs < 0 ||
        globalMs >= global10Duration
      )
        throw Error('explicit opacity phase out of bounds');
      const n = e.sample_opacity(model.length, skin.length, sequenceMs, globalMs);
      if (n < 0 || n !== e.count()) throw Error('opacity profile rejected: ' + n);
      return {
        values: new Float32Array(e.memory.buffer, e.values_ptr(), n * 6).slice(),
        visible: new Uint32Array(e.memory.buffer, e.visible_ptr(), n).slice(),
        sequence_ms: sequenceMs,
        global_phase_ms: globalMs,
        owner_opacity: 1,
      };
    },
  };
}
