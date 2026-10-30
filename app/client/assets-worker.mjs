import { readLocalSource } from '../../runtime/io/local_source.mjs';
import { makePlanner } from '../../engine/archive-startup/planner.mjs';
import { openArchive } from '../../runtime/io/member_reader.mjs';
import { createRangeCache } from '../../runtime/io/range_cache.mjs';
let archives, ordered, lookupWasm, decoderWasm, blp, files;
let world;
async function worldOperation(data) {
  if (data.operation === 'init') {
    world = (await WebAssembly.instantiate(data.wasm, {})).instance.exports;
    return {};
  }
  if (!world) throw Error('World decoder is unavailable');
  const floats = (p, n) => new Float32Array(world.memory.buffer, p, n);
  const words = (p, n) => new Uint32Array(world.memory.buffer, p, n);
  async function load(path) {
    const asset = await bytes(path);
    if (asset.length > world.world_capacity()) throw Error('World asset exceeds decoder capacity');
    new Uint8Array(world.memory.buffer, world.world_input(), asset.length).set(asset);
    return asset.length;
  }
  if (data.operation === 'terrain') {
    const n = await load(data.path);
    const error = world.world_terrain(n, data.bigAlpha ? 1 : 0);
    if (error) throw Error(`Terrain decode rejected (${error}): ${data.path}`);
    const info = words(world.world_meta(), 8).slice();
    const strings = new Uint8Array(world.memory.buffer, world.world_input() + info[3], info[4]);
    const names = new TextDecoder('utf-8', { fatal: true }).decode(strings).split('\0');
    if (names.at(-1) !== '') throw Error('Invalid terrain texture names');
    names.pop();
    const errorObjects = world.world_objects(n);
    if (errorObjects) throw Error(`World structure records rejected (${errorObjects})`);
    const count = words(world.world_meta(), 1)[0];
    const objects = Array.from({ length: count }, (_, i) => {
      const pathBytes = new Uint8Array(
        world.memory.buffer,
        world.world_object_names() + i * 256,
        256,
      );
      return {
        id: words(world.world_object_ids(), count)[i],
        kind: words(world.world_object_kinds(), count)[i] ? 'm2' : 'wmo',
        doodadSet: words(world.world_object_sets(), count)[i],
        path: new TextDecoder().decode(pathBytes.subarray(0, pathBytes.indexOf(0))),
        placement: floats(world.world_object_matrices() + i * 64, 16).slice(),
      };
    });
    return {
      objects,
      vertices: floats(world.world_vertices(), info[1] * 11).slice(),
      indices: words(world.world_indices(), info[2]).slice(),
      chunks: words(world.world_chunks(), info[0] * 8).slice(),
      alpha: new Uint8Array(world.memory.buffer, world.world_alpha(), info[0] * 16384).slice(),
      textures: names,
    };
  }
  if (data.operation === 'static-model') {
    let error = world.world_static_model(await load(data.path));
    if (error) throw Error(`Static world model rejected (${error}): ${data.path}`);
    error = world.world_static_skin(await load(data.path.replace(/\.m2$/i, '00.skin')));
    if (error) throw Error(`Static world skin rejected (${error}): ${data.path}`);
    const info = words(world.world_meta(), 6).slice();
    return {
      vertices: floats(world.world_vertices(), info[0] * 11).slice(),
      indices: words(world.world_indices(), info[1]).slice(),
      batches: words(world.world_chunks(), info[2] * 8).slice(),
      effects: { emitters: info[4], uv: info[5] },
      materials: Array.from({ length: info[3] }, (_, i) => {
        const name = new Uint8Array(
          world.memory.buffer,
          world.world_material_names() + i * 256,
          256,
        );
        return {
          path: new TextDecoder().decode(name.subarray(0, name.indexOf(0))),
          mode: words(world.world_material_modes(), info[3])[i],
        };
      }),
    };
  }
  if (data.operation === 'wmo-root') {
    const error = world.world_wmo_root(await load(data.path));
    if (error) throw Error(`World structure header rejected (${error}): ${data.path}`);
    const info = words(world.world_meta(), 3).slice();
    return {
      groups: words(world.world_exterior_groups(), info[2]).slice(),
      materials: Array.from({ length: info[1] }, (_, i) => {
        const name = new Uint8Array(
          world.memory.buffer,
          world.world_material_names() + i * 256,
          256,
        );
        return {
          path: new TextDecoder().decode(name.subarray(0, name.indexOf(0))),
          mode: words(world.world_material_modes(), info[1])[i],
        };
      }),
    };
  }
  if (data.operation === 'wmo-group') {
    const error = world.world_wmo_group(await load(data.path));
    if (error) throw Error(`World structure geometry rejected (${error}): ${data.path}`);
    const info = words(world.world_meta(), 4).slice();
    return {
      vertices: floats(world.world_vertices(), info[0] * 11).slice(),
      indices: words(world.world_indices(), info[1]).slice(),
      batches: words(world.world_chunks(), info[2] * 8).slice(),
      doodadRefs: words(world.world_group_decor(), info[3]).slice(),
    };
  }
  if (data.operation === 'wmo-decor') {
    if (
      !(data.placement instanceof Float32Array) ||
      data.placement.length !== 16 ||
      !data.placement.every(Number.isFinite)
    )
      throw Error('Invalid world structure placement');
    floats(world.world_parent(), 16).set(data.placement);
    const error = world.world_wmo_decor(await load(data.path), data.doodadSet || 0);
    if (error) throw Error(`World structure decoration rejected (${error}): ${data.path}`);
    const count = words(world.world_meta(), 1)[0];
    return {
      children: Array.from({ length: count }, (_, i) => {
        const name = new Uint8Array(world.memory.buffer, world.world_decor_names() + i * 256, 256);
        return {
          path: new TextDecoder().decode(name.subarray(0, name.indexOf(0))),
          kind: 'm2',
          decorId: words(world.world_decor_ids(), count)[i],
          sourceIndex: words(world.world_decor_sources(), count)[i],
          placement: floats(world.world_decor_matrices() + i * 64, 16).slice(),
        };
      }),
    };
  }
  if (data.operation === 'spawn') {
    if (
      !Array.isArray(data.position) ||
      data.position.length !== 4 ||
      !data.position.every(Number.isFinite)
    )
      throw Error('Invalid world spawn');
    floats(world.world_controls(), 4).set(data.position);
    if (world.world_spawn()) throw Error('World spawn rejected');
    return {};
  }
  if (data.operation === 'environment') {
    const names = ['Light', 'LightParams', 'LightIntBand'];
    for (let i = 0; i < names.length; i++) {
      const error = world.world_environment_load(
        i,
        await load('DBFilesClient\\' + names[i] + '.dbc'),
      );
      if (error) throw Error(`World environment table rejected (${error}): ${names[i]}`);
    }
    return {};
  }
  if (data.operation === 'cinematic') {
    if (
      !Array.isArray(data.origin) ||
      data.origin.length !== 4 ||
      !data.origin.every(Number.isFinite)
    )
      throw Error('Invalid cinematic origin');
    floats(world.world_origin(), 4).set(data.origin);
    const error = world.world_cinematic_load(await load(data.path));
    if (error) throw Error(`Cinematic camera rejected (${error}): ${data.path}`);
    return { duration: world.world_cinematic_duration() };
  }
  if (data.operation === 'sample') {
    if (data.cinematicMs !== undefined) {
      if (
        !Number.isInteger(data.cinematicMs) ||
        data.cinematicMs < 0 ||
        data.cinematicMs > world.world_cinematic_duration()
      )
        throw Error('Invalid cinematic clock');
      if (world.world_cinematic_sample(data.cinematicMs)) throw Error('Cinematic sample rejected');
    } else {
      if (
        !Array.isArray(data.controls) ||
        data.controls.length !== 7 ||
        !data.controls.every(Number.isFinite)
      )
        throw Error('Invalid world controls');
      floats(world.world_controls(), 7).set(data.controls);
      if (world.world_step(data.elapsed)) throw Error('World movement rejected');
      if (world.world_orbit()) throw Error('World camera rejected');
    }
    if (world.world_view(data.aspect)) throw Error('World view rejected');
    world.world_frame_meta();
    world.world_cull();
    world.world_cull_objects();
    world.world_cull_decor();
    const skyStatus = world.world_environment(data.map, data.time, data.globalMs);
    return {
      player: floats(world.world_player(), 8).slice(),
      camera: floats(world.world_camera(), 10).slice(),
      view: floats(world.world_view_ptr(), 83).slice(),
      placement: floats(world.world_placement(), 16).slice(),
      tiles: words(world.world_meta(), 4).slice(),
      visibility: words(world.world_visibility(), 160).slice(),
      objectVisibility: words(world.world_object_visibility(), 2080).slice(),
      decorVisibility: words(world.world_decor_visibility(), 1024).slice(),
      sky: skyStatus ? null : floats(world.world_sky_values(), 58).slice(),
      skybox: skyStatus ? 0 : world.world_sky_id(),
    };
  }
  throw Error('Unsupported world asset operation');
}
const byteCache = new Map();
let cachedBytes = 0;
async function bytes(path) {
  const key = path.toLowerCase().replaceAll('\\', '/');
  if (byteCache.has(key)) {
    const value = byteCache.get(key);
    byteCache.delete(key);
    byteCache.set(key, value);
    return value;
  }
  for (const m of ordered) {
    let a = archives.get(m.path);
    if (!a) {
      const f = files.get(m.path.toLowerCase());
      if (!f) throw Error('missing archive ' + m.path);
      a = await openArchive({
        size: f.size ?? f.file?.size,
        read: createRangeCache((o, n) => readLocalSource(f, o, n), f.size ?? f.file?.size, {
          budget: 4 * 1024 * 1024,
        }),
        lookupWasm,
        decoderWasm,
      });
      archives.set(m.path, a);
    }
    if (a.lookup(path)) {
      const value = (await a.readMember(path)).bytes;
      while (byteCache.size && cachedBytes + value.length > 33554432) {
        const oldest = byteCache.keys().next().value;
        cachedBytes -= byteCache.get(oldest).length;
        byteCache.delete(oldest);
      }
      if (value.length <= 33554432) {
        byteCache.set(key, value);
        cachedBytes += value.length;
      }
      return value;
    }
  }
  throw Error('missing member ' + path);
}
async function init(data) {
  byteCache.clear();
  cachedBytes = 0;
  files = new Map(data.files.map((row) => [row.path.toLowerCase(), row]));
  archives = new Map();
  ({ lookupWasm, decoderWasm } = data);
  const plan = (await makePlanner(data.startup)).plan(
    data.files.map((f) => f.path),
    data.locale,
  );
  if (plan.status !== 1) throw Error('mount planning failed');
  const { instance } = await WebAssembly.instantiate(data.orderWasm, {}),
    e = instance.exports;
  new Uint32Array(e.memory.buffer, e.rows_ptr(), plan.mounts.length * 3).set(
    plan.mounts.flatMap((m) => [m.priority, m.sequence, 0]),
  );
  if (e.rank_candidates(plan.mounts.length) !== 1) throw Error('mount order failed');
  ordered = Array.from(
    new Uint32Array(e.memory.buffer, e.order_ptr(), plan.mounts.length),
    (i) => plan.mounts[i],
  );
  blp = (await WebAssembly.instantiate(data.blpWasm, {})).instance.exports;
  return { mounts: ordered.length };
}
const jobs = [];
let draining = false;
self.onmessage = ({ data }) => {
  if (data.kind === 'world' && data.operation === 'sample') jobs.unshift(data);
  else jobs.push(data);
  drain();
};
async function drain() {
  if (draining) return;
  draining = true;
  while (jobs.length) {
    const data = jobs.shift();
    try {
      let result;
      if (data.kind === 'init') result = await init(data);
      else if (data.kind === 'world') result = await worldOperation(data);
      else if (data.kind === 'texture') {
        const path = /\.[A-Za-z0-9]+$/.test(data.path) ? data.path : data.path + '.blp';
        if (!/\.blp$/i.test(path)) throw Error('unsupported image format ' + path);
        const b = await bytes(path);
        if (b.length > new Uint8Array(blp.memory.buffer, blp.input_ptr()).length)
          throw Error('texture too large');
        new Uint8Array(blp.memory.buffer, blp.input_ptr(), b.length).set(b);
        if (blp.decode_blp(b.length)) throw Error('unsupported BLP ' + path);
        const width = blp.image_width(),
          height = blp.image_height();
        result = {
          width,
          height,
          rgba: new Uint8Array(blp.memory.buffer, blp.rgba_ptr(), width * height * 4).slice(),
        };
      } else result = { bytes: await bytes(data.path) };
      const transfer = [];
      for (const value of Object.values(result))
        if (ArrayBuffer.isView(value)) transfer.push(value.buffer);
      // The byte cache owns its buffers; transfer only newly generated arrays.
      self.postMessage(
        { id: data.id, result },
        data.kind === 'world' || data.kind === 'texture' ? transfer : [],
      );
    } catch (e) {
      self.postMessage({ id: data.id, error: e.message });
    }
  }
  draining = false;
}
