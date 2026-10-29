import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
const root = new URL('../../', import.meta.url);
const hash = (b) => createHash('sha256').update(b).digest('hex');
export async function prepareSceneManifest(member) {
  const model = 'Interface\\Glues\\Models\\UI_MainMenu_Northrend\\UI_MainMenu_Northrend.m2';
  const skin = model.replace(/\.m2$/i, '00.skin');
  const modules = {};
  for (const [key, path] of Object.entries({
    prepare: 'model-prepare/build/prepare.wasm',
    mesh: 'model-mesh/build/mesh.wasm',
    blp: 'ui-texture/build/blp.wasm',
  }))
    modules[key] = (
      await WebAssembly.instantiate(await readFile(new URL('engine/' + path, root)), {})
    ).instance.exports;
  const jobs = [];
  const add = async (name, kind) => {
    const item = await member(name),
      bytes = item.bytes;
    jobs.push({
      name,
      kind,
      path: item.path,
      logical_bytes: bytes.length,
      logical_sha256: hash(bytes),
    });
    return bytes;
  };
  const m = await add(model, 'model'),
    s = await add(skin, 'skin'),
    v = new DataView(m.buffer, m.byteOffset, m.byteLength);
  const p = modules.prepare;
  new Uint8Array(p.memory.buffer, p.model_ptr(), m.length).set(m);
  new Uint8Array(p.memory.buffer, p.skin_ptr(), s.length).set(s);
  if (p.prepare_records(m.length, s.length)) throw Error('Login scene vertex preparation failed');
  const info = new Uint32Array(p.memory.buffer, p.info_ptr(), 4);
  const prepared = new Uint8Array(p.memory.buffer, p.output_ptr(), info[1]).slice();
  const count = v.getUint32(80, true),
    at = v.getUint32(84, true),
    names = new Set();
  if (count > 256 || at > m.length - count * 16) throw Error('Invalid login texture table');
  for (let i = 0; i < count; i++) {
    const row = at + i * 16,
      kind = v.getUint32(row, true),
      size = v.getUint32(row + 8, true),
      offset = v.getUint32(row + 12, true);
    if (kind) continue;
    if (!size || size > 512 || offset > m.length - size) throw Error('Invalid login texture path');
    names.add(new TextDecoder().decode(m.subarray(offset, offset + size - 1)));
  }
  for (const name of names) {
    const bytes = await add(name, 'texture'),
      e = modules.blp;
    new Uint8Array(e.memory.buffer, e.input_ptr(), bytes.length).set(bytes);
    if (e.decode_blp(bytes.length)) throw Error('Login texture decode failed: ' + name);
    const width = e.image_width(),
      height = e.image_height();
    Object.assign(jobs.at(-1), {
      width,
      height,
      rgba_sha256: hash(new Uint8Array(e.memory.buffer, e.rgba_ptr(), width * height * 4)),
    });
  }
  jobs.push({
    name: model + '.prepared',
    kind: 'prepare',
    model,
    skin,
    logical_bytes: prepared.length,
    logical_sha256: hash(prepared),
  });
  const e = modules.mesh;
  new Uint8Array(e.memory.buffer, e.skin_ptr(), s.length).set(s);
  if (e.decode_mesh(s.length, prepared.length / 48)) throw Error('Login mesh decode failed');
  const indices = new Uint8Array(
    e.memory.buffer,
    e.index_ptr(),
    new Uint32Array(e.memory.buffer, e.info_ptr(), 3)[0] * 4,
  );
  return { jobs, indexHash: hash(indices) };
}
