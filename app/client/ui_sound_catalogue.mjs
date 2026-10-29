// Resolve literal original PlaySound calls against the current local SoundEntries
// DBC through the existing ranked MPQ worker. No guessed kit IDs or silent sounds.
import { Worker } from 'node:worker_threads';
import { readFile, readdir, stat } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
export async function prepareUISounds(locale, sourceFiles, manifest) {
  const root = new URL('../../', import.meta.url),
    get = (p) => readFile(new URL(p, root)),
    kits = new Set(),
    ambienceKits = new Set();
  for (const source of sourceFiles) {
    const text = (await readFile(source)).toString();
    for (const m of text.matchAll(/GlueCreditsSoundKits\[\d+\]\s*=\s*["']([^"']+)["']/g))
      kits.add(m[1]);
    for (const m of text.matchAll(/PlaySound\(\s*["']([^"']+)["']/g)) kits.add(m[1]);
    for (const m of text.matchAll(/GlueAmbienceTracks\[["'][^"']+["']\]\s*=\s*["']([^"']+)["']/g)) {
      kits.add(m[1]);
      ambienceKits.add(m[1]);
    }
  }
  const files = await Promise.all(
    (await readdir(new URL('Data/', root), { recursive: true }))
      .filter((p) => /\.mpq$/i.test(p))
      .map(async (path) => {
        const diskPath = fileURLToPath(new URL('Data/' + path, root));
        return { path, diskPath, size: (await stat(diskPath)).size };
      }),
  );
  const worker = new Worker(new URL('./audio-worker.mjs', import.meta.url), { type: 'module' }),
    pending = new Map();
  let next = 0;
  const preparedBytes = new Map();
  worker.on('message', (data) => {
    const row = pending.get(data.id);
    if (!row) return;
    pending.delete(data.id);
    clearTimeout(row.timer);
    data.error ? row.reject(Error(data.error)) : row.resolve(data.result);
  });
  worker.on('error', (error) => {
    for (const row of pending.values()) {
      clearTimeout(row.timer);
      row.reject(error);
    }
    pending.clear();
  });
  const call = (message) =>
    new Promise((resolve, reject) => {
      const id = ++next,
        timer = setTimeout(() => {
          pending.delete(id);
          const label = message.kind === 'bytes' ? message.path : message.kind;
          reject(Error('UI sound preparation timeout: ' + label));
        }, 60000);
      pending.set(id, { resolve, reject, timer });
      worker.postMessage({ id, ...message });
    });
  try {
    await call({
      kind: 'init',
      files,
      locale,
      startup: await get('engine/archive-startup/build/startup.wasm'),
      orderWasm: await get('engine/archive-selection/build/order.wasm'),
      lookupWasm: await get('engine/mpq-lookup/build/lookup.wasm'),
      decoderWasm: await get('engine/mpq-sector-wasm/build/decoder.wasm'),
    });
    const { bytes } = await call({ kind: 'bytes', path: 'DBFilesClient\\SoundEntries.dbc' }),
      view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength),
      count = view.getUint32(4, true),
      fields = view.getUint32(8, true),
      stride = view.getUint32(12, true),
      strings = 20 + count * stride;
    if (
      new TextDecoder().decode(bytes.subarray(0, 4)) !== 'WDBC' ||
      fields !== 30 ||
      stride !== 120 ||
      strings + view.getUint32(16, true) !== bytes.length
    )
      throw Error('Invalid current SoundEntries table');
    const text = (offset) => {
        const start = strings + offset,
          end = bytes.indexOf(0, start);
        if (start < strings || start >= bytes.length || end < 0)
          throw Error('Invalid sound string offset');
        return new TextDecoder().decode(bytes.subarray(start, end));
      },
      entries = new Map();
    for (let i = 0; i < count; i++) {
      const at = 20 + i * stride,
        name = text(view.getUint32(at + 8, true));
      entries.set(name.toLowerCase(), { at, name });
    }
    const result = structuredClone(manifest);
    for (const kit of [...kits].sort()) {
      if (result.assets.some((a) => a.kit === kit)) continue;
      const entry = entries.get(kit.toLowerCase());
      if (!entry) throw Error('Original UI sound kit absent from DBC: ' + kit);
      const { at } = entry,
        id = view.getUint32(at, true),
        variants = [];
      for (let i = 3; i < 13; i++) {
        const offset = view.getUint32(at + i * 4, true);
        if (offset) variants.push(text(offset));
      }
      if (!variants.length) throw Error('UI sound kit has no files: ' + kit);
      const name = text(view.getUint32(at + 23 * 4, true)).replace(/\\$/, '') + '\\' + variants[0],
        audio = await call({ kind: 'bytes', path: name }),
        body = audio.bytes,
        magic = new TextDecoder().decode(body.subarray(0, 4));
      preparedBytes.set(`${audio.archive.toLowerCase()}\0${name.toLowerCase()}`, body);
      let media_type;
      if (magic === 'RIFF') media_type = 'audio/wav';
      else if (magic === 'OggS') media_type = 'audio/ogg';
      else if (magic.startsWith('ID3') || (body[0] === 255 && (body[1] & 224) === 224))
        media_type = 'audio/mpeg';
      else throw Error('Unsupported UI sound encoding: ' + name);
      result.assets.push({
        channel: ambienceKits.has(kit) ? 1 : /^Menu-Credits0[123]$/.test(kit) ? 0 : 2,
        id,
        kit,
        name,
        archive: audio.archive,
        bytes: body.length,
        sha256: createHash('sha256').update(body).digest('hex'),
        media_type,
        variant_policy: 'first original file; native random selection not reconstructed',
      });
    }
    if (result.assets.length > 128) throw Error('UI sound catalogue exceeds selected C capacity');
    const preparedFiles = new Map();
    for (const asset of [...result.assets, ...(result.effects || [])]) {
      if (!/^[a-f\d]{64}$/i.test(asset.sha256)) throw Error('Invalid local audio identity');
      const key = `${asset.archive.toLowerCase()}\0${asset.name.toLowerCase()}`;
      let body = preparedBytes.get(key);
      if (!body) {
        const audio = await call({ kind: 'bytes', path: asset.name, archive: asset.archive });
        body = audio.bytes;
      }
      if (
        (asset.bytes !== undefined && body.length !== asset.bytes) ||
        createHash('sha256').update(body).digest('hex') !== asset.sha256
      )
        throw Error('Local audio identity mismatch: ' + asset.name);
      const source_url = '/local-audio/' + asset.sha256.toLowerCase();
      asset.source_url = source_url;
      preparedFiles.set(source_url, body);
    }
    return { manifest: result, files: preparedFiles };
  } finally {
    await worker.terminate();
    for (const row of pending.values()) {
      clearTimeout(row.timer);
      row.reject(Error('UI sound preparer closed'));
    }
  }
}
