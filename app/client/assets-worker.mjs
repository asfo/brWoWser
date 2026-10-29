import { readLocalSource } from '../../runtime/io/local_source.mjs';
import { makePlanner } from '../../engine/archive-startup/planner.mjs';
import { openArchive } from '../../runtime/io/member_reader.mjs';
let archives, ordered, lookupWasm, decoderWasm, blp, files;
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
        read: (o, n) => readLocalSource(f, o, n),
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
let queue = Promise.resolve();
self.onmessage = ({ data }) => {
  queue = queue.then(async () => {
    try {
      let result;
      if (data.kind === 'init') result = await init(data);
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
      self.postMessage({ id: data.id, result });
    } catch (e) {
      self.postMessage({ id: data.id, error: e.message });
    }
  });
};
