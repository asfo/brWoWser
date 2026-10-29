import { readLocalSource } from '../../runtime/io/local_source.mjs';
import { createScene } from '../../runtime/scene/scene.mjs';
import { listen, send } from '../../engine/workers/environment.mjs';
import { openArchive } from '../../runtime/io/member_reader.mjs';
const sha = async (bytes) =>
  Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (value) =>
    value.toString(16).padStart(2, '0'),
  ).join('');
let config,
  control,
  sources,
  blp,
  prepare,
  scene,
  busy = false;
const archives = new Map(),
  retained = new Map(),
  pixels = new Map();
// Keep every sector identity check, but amortize WebCrypto scheduling instead
// of awaiting hundreds of individual 4 KiB digests in sequence.
async function sectorHashes(bytes) {
  const hashes = [];
  for (let base = 0; base < bytes.length; base += 16 * 4096) {
    const batch = [];
    for (let offset = base; offset < Math.min(bytes.length, base + 16 * 4096); offset += 4096)
      batch.push(sha(bytes.subarray(offset, offset + 4096)));
    hashes.push(...(await Promise.all(batch)));
  }
  return hashes;
}
async function archive(path) {
  const key = path.toLowerCase();
  if (archives.has(key)) return archives.get(key);
  const source = sources.get(key);
  if (!source) throw Error('selected archive missing');
  const result = await openArchive({
    size: source.size ?? source.file.size,
    lookupWasm: config.lookupWasm,
    decoderWasm: config.decoderWasm,
    onPhase: (phase) => Atomics.store(control, 4, phase === 'read' ? 2 : 7),
    read: async (offset, count) => {
      return readLocalSource(source, offset, count);
    },
  });
  archives.set(key, result);
  return result;
}
listen(async (message) => {
  if (message.kind === 'init') {
    try {
      config = message;
      control = new Int32Array(message.shared);
      sources = new Map(message.files.map((row) => [row.path.toLowerCase(), row]));
      [blp, prepare] = await Promise.all(
        [config.blpWasm, config.prepareWasm].map(
          async (wasm) => (await WebAssembly.instantiate(wasm, {})).instance.exports,
        ),
      );
      await Promise.all(
        [...new Set(config.models.map((row) => row.path).filter(Boolean))].map(archive),
      );
      send({ kind: 'provider-ready' });
    } catch (error) {
      send({ kind: 'asset-error', message: 'resource provider initialization: ' + error.message });
    }
    return;
  }
  if (message.kind === 'sample') {
    try {
      if (!scene) throw Error('scene not ready');
      send({
        kind: 'sample',
        id: message.id,
        ...scene.sample(message.sequenceMs, message.globalMs),
      });
    } catch (error) {
      send({ kind: 'rpc-error', id: message.id, message: error.message });
    }
    return;
  }
  if (message.kind === 'export-gpu') {
    try {
      const modelJob = config.models.find((row) => row.kind === 'model'),
        skinJob = config.models.find((row) => row.kind === 'skin'),
        preparedJob = config.models.find((row) => row.kind === 'prepare'),
        prepared = retained.get(preparedJob.name.toLowerCase()),
        skin = retained.get(skinJob.name.toLowerCase());
      if (retained.size !== config.models.length || !prepared || !skin)
        throw Error('GPU export before verified CPU completion');
      const { instance } = await WebAssembly.instantiate(config.meshWasm, {}),
        mesh = instance.exports;
      new Uint8Array(mesh.memory.buffer, mesh.skin_ptr(), skin.length).set(skin);
      if (mesh.decode_mesh(skin.length, prepared.length / 48))
        throw Error('GPU index preparation failed');
      const count = new Uint32Array(mesh.memory.buffer, mesh.info_ptr(), 3)[0],
        indices = new Uint8Array(mesh.memory.buffer, mesh.index_ptr(), count * 4).slice();
      if ((await sha(indices)) !== config.indexHash) throw Error('GPU index identity mismatch');
      if (!scene)
        scene = await createScene({
          model: retained.get(modelJob.name.toLowerCase()),
          skin,
          prepared,
          texturePixels: config.models
            .filter((row) => row.kind === 'texture')
            .map((row) => ({ ...row, rgba: pixels.get(row.name.toLowerCase()) })),
          modules: config.modules,
        });
      const textures = config.models
          .filter((row) => row.kind === 'texture')
          .map((row) => ({
            name: row.name,
            width: row.width,
            height: row.height,
            rgba: pixels.get(row.name.toLowerCase()).slice(),
          })),
        buffers = [
          { name: 'prepared-records', role: 'vertex', bytes: prepared.slice() },
          { name: 'indices-u32', role: 'index', bytes: indices },
          {
            name: 'posed-positions',
            role: 'vertex',
            bytes: new Uint8Array(scene.payload.positions).slice(),
          },
        ];
      send({
        kind: 'gpu-payload',
        payload: { textures, buffers },
        scene: {
          metadata: scene.metadata,
          cameraValues: scene.cameraValues,
          payload: scene.payload,
        },
        model: modelJob.name,
      });
    } catch (error) {
      send({ kind: 'gpu-export-error', message: error.message });
    }
    return;
  }
  if (message.kind === 'inspect') {
    send({
      kind: 'retained',
      models: [...retained].map(([name, bytes]) => ({
        name,
        bytes: bytes.length,
        rgba_bytes: pixels.get(name)?.length || 0,
      })),
    });
    return;
  }
  if (message.kind !== 'load' || !config) return;
  const { index, token } = message;
  try {
    if (busy) throw Error('concurrent resource load not admitted');
    busy = true;
    const job = config.models[index];
    Atomics.store(control, 4, 1);
    let bytes;
    if (job.kind === 'prepare') {
      Atomics.store(control, 4, 5);
      const model = retained.get(job.model.toLowerCase()),
        skin = retained.get(job.skin.toLowerCase());
      if (!model || !skin) throw Error('preparation dependencies missing');
      prepare ??= (await WebAssembly.instantiate(config.prepareWasm, {})).instance.exports;
      new Uint8Array(prepare.memory.buffer, prepare.model_ptr(), model.length).set(model);
      new Uint8Array(prepare.memory.buffer, prepare.skin_ptr(), skin.length).set(skin);
      if (prepare.prepare_records(model.length, skin.length))
        throw Error('record preparation failed');
      const count = new Uint32Array(prepare.memory.buffer, prepare.info_ptr(), 4)[1];
      bytes = new Uint8Array(prepare.memory.buffer, prepare.output_ptr(), count).slice();
    } else {
      const source = await archive(job.path);
      Atomics.store(control, 4, 2);
      const member = await source.readMember(job.name);
      if (!member) throw Error('resource not found');
      bytes = member.bytes;
    }
    Atomics.store(control, 4, 3);
    if (bytes.length !== job.logical_bytes || (await sha(bytes)) !== job.logical_sha256)
      throw Error('resource identity mismatch');
    if (job.sector_sha256) {
      const hashes = await sectorHashes(bytes);
      if (JSON.stringify(hashes) !== JSON.stringify(job.sector_sha256))
        throw Error('resource sector identity mismatch');
    }
    if (job.kind === 'texture') {
      Atomics.store(control, 4, 4);
      blp ??= (await WebAssembly.instantiate(config.blpWasm, {})).instance.exports;
      new Uint8Array(blp.memory.buffer, blp.input_ptr(), bytes.length).set(bytes);
      if (blp.decode_blp(bytes.length)) throw Error('texture decode failed');
      const width = blp.image_width(),
        height = blp.image_height();
      if (width !== job.width || height !== job.height) throw Error('texture dimensions mismatch');
      const rgba = new Uint8Array(blp.memory.buffer, blp.rgba_ptr(), width * height * 4).slice();
      if ((await sha(rgba)) !== job.rgba_sha256) throw Error('texture pixel identity mismatch');
      pixels.set(job.name.toLowerCase(), rgba);
    }
    retained.set(job.name.toLowerCase(), bytes);
    Atomics.store(control, 4, 6);
    send({
      kind: 'asset-retained',
      index,
      name: job.name,
      resource_kind: job.kind,
      bytes: bytes.length,
      sha256: job.logical_sha256,
      rgba_bytes: pixels.get(job.name.toLowerCase())?.length || 0,
    });
    Atomics.store(control, 6, bytes.length);
    Atomics.store(control, 1, token);
    Atomics.store(control, 2, 0);
    Atomics.store(control, 0, 1);
    Atomics.notify(control, 0);
  } catch (error) {
    send({
      kind: 'asset-error',
      index,
      message: (config.models[index]?.name || index) + ': ' + error.message,
    });
    Atomics.store(control, 1, token);
    Atomics.store(control, 2, 1);
    Atomics.store(control, 0, 1);
    Atomics.notify(control, 0);
  } finally {
    busy = false;
  }
});
