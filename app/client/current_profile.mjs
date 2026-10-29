// Prepare the supported interface profile from the current owner-local client.
import { readFile, writeFile, mkdir, readdir, open, stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { makePlanner } from '../../engine/archive-startup/planner.mjs';
import { openArchive } from '../../runtime/io/member_reader.mjs';
import { readCharacterMetadata } from '../../runtime/character/metadata.mjs';
import { prepareSceneManifest } from './scene_manifest.mjs';
import { readCompat } from '../../tools/client_image.mjs';
const root = new URL('../../', import.meta.url),
  get = (p) => readFile(new URL(p, root)),
  sha = (b) => createHash('sha256').update(b).digest('hex');
export async function currentProfile(locale) {
  const paths = (await readdir(new URL('Data/', root), { recursive: true }))
    .filter((p) => /\.mpq$/i.test(p))
    .sort();
  const plan = (await makePlanner(await get('engine/archive-startup/build/startup.wasm'))).plan(
    paths,
    locale,
  );
  if (plan.status !== 1) throw Error('Current client archive plan rejected: ' + locale);
  const order = (
    await WebAssembly.instantiate(await get('engine/archive-selection/build/order.wasm'), {})
  ).instance.exports;
  new Uint32Array(order.memory.buffer, order.rows_ptr(), plan.mounts.length * 3).set(
    plan.mounts.flatMap((r) => [r.priority, r.sequence, 0]),
  );
  if (order.rank_candidates(plan.mounts.length) !== 1)
    throw Error('Current archive ranking failed');
  const mounts = Array.from(
      new Uint32Array(order.memory.buffer, order.order_ptr(), plan.mounts.length),
      (i) => plan.mounts[i],
    ),
    archives = new Map(),
    handles = [],
    lookupWasm = await get('engine/mpq-lookup/build/lookup.wasm'),
    decoderWasm = await get('engine/mpq-sector-wasm/build/decoder.wasm');
  async function archive(path) {
    if (archives.has(path)) return archives.get(path);
    const url = new URL('Data/' + path, root),
      file = await open(url, 'r');
    handles.push(file);
    const size = (await file.stat()).size;
    const result = await openArchive({
      size,
      lookupWasm,
      decoderWasm,
      read: async (offset, count) => {
        const bytes = new Uint8Array(count);
        const { bytesRead } = await file.read(bytes, 0, count, offset);
        if (bytesRead !== count) throw Error('Short current archive read');
        return bytes;
      },
    });
    archives.set(path, result);
    return result;
  }
  async function member(name) {
    for (const mount of mounts) {
      const a = await archive(mount.path);
      if (a.lookup(name)) {
        const row = await a.readMember(name);
        return { ...row, path: mount.path };
      }
    }
    throw Error('Current client member missing: ' + name);
  }
  try {
    const folder = new URL('app/client/build/current-' + locale + '/', root);
    await mkdir(folder, { recursive: true });
    const boot = JSON.parse(await get('config/client.json'));
    const buffers = [await readCompat(), (await member('Fonts\\FRIZQT__.TTF')).bytes];
    const data = { ...boot, compat: 0, font: 1, resources: [], locale };
    for (const name of boot.resources) {
      const loaded = await member(name);
      data.resources.push({ name, index: buffers.length, sha256: sha(loaded.bytes) });
      buffers.push(loaded.bytes);
    }
    const toc = new TextDecoder()
      .decode((await member('Interface\\GlueXML\\GlueXML.toc')).bytes)
      .split(/\r?\n/)
      .map((x) => x.trim())
      .filter((x) => x && !x.startsWith('#'))
      .map((x) => (/^Interface[\\/]/i.test(x) ? x : 'Interface\\GlueXML\\' + x));
    if (
      JSON.stringify(toc.map((x) => x.toLowerCase().replaceAll('/', '\\'))) !==
      JSON.stringify(data.order.map((x) => x.toLowerCase().replaceAll('/', '\\')))
    )
      throw Error('Current TOC dispatch differs from supported runtime; requires review');
    const files = [];
    for (let i = 0; i < buffers.length; i++) {
      const p = new URL(i + '.private.bin', folder);
      await writeFile(p, buffers[i]);
      files.push(p);
    }
    data.file_hashes = buffers.map(sha);
    const manifest = await prepareSceneManifest(member);
    const pixelDecoderHash = sha(await get('runtime/graphics/blp.c'));
    const uiAssets = [];
    for (const name of JSON.parse(await get('config/ui-textures.json'))) {
      const { bytes } = await member(/\.blp$/i.test(name) ? name : name + '.blp');
      const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      uiAssets.push({ name, width: v.getUint32(12, true), height: v.getUint32(16, true) });
    }
    for (const row of data.resources) {
      if (!/\.lua$/i.test(row.name)) continue;
      const text = new TextDecoder().decode(buffers[row.index]);
      for (const match of text.matchAll(
        /Set(?:Normal|Pushed|Disabled|Highlight|Checked|DisabledChecked)?Texture\(\s*"([^"]+)"\s*\)/g,
      )) {
        const name = match[1].replaceAll('\\\\', '\\');
        if (
          !name.startsWith('Interface\\') ||
          uiAssets.some((r) => r.name.toLowerCase() === name.toLowerCase())
        )
          continue;
        let loaded;
        try {
          loaded = await member(/\.blp$/i.test(name) ? name : name + '.blp');
        } catch (e) {
          if (e.message.startsWith('Current client member missing:')) continue;
          throw e;
        }
        const bytes = loaded.bytes;
        if (new TextDecoder().decode(bytes.subarray(0, 4)) !== 'BLP2')
          throw Error('Current UI texture is not BLP2');
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength),
          width = view.getUint32(12, true),
          height = view.getUint32(16, true);
        if (!width || !height || width > 4096 || height > 4096)
          throw Error('Current UI texture dimensions rejected');
        uiAssets.push({ name, width, height });
      }
    }
    const characterMetadata = await readCharacterMetadata(
      async (name) => (await member(name)).bytes,
      locale,
    );
    const backgroundCandidates = new Set();
    for (const token of [
      ...characterMetadata.races.map((row) => row.background),
      ...characterMetadata.classes
        .filter((row) => row.token === 'DEATHKNIGHT')
        .map((row) => row.token),
    ])
      backgroundCandidates.add(`Interface\\Glues\\Models\\UI_${token}\\UI_${token}.m2`);
    for (const row of data.resources) {
      if (!/\.lua$/i.test(row.name)) continue;
      const text = new TextDecoder().decode(buffers[row.index]);
      for (const match of text.matchAll(/["'](Interface[\\/][^"']+\.m2)["']/gi))
        backgroundCandidates.add(match[1].replaceAll('\\\\', '\\').replaceAll('/', '\\'));
    }
    const backgroundModels = [];
    for (const path of backgroundCandidates) {
      let available = false;
      for (const mount of mounts) {
        const source = await archive(mount.path);
        const model = source.lookup(path),
          skin = source.lookup(path.replace(/\.m2$/i, '00.skin'));
        if (model && skin) {
          available = true;
          break;
        }
      }
      if (available) backgroundModels.push(path);
    }
    // Cache only dimensions/names, bound to every current archive size/mtime.
    const creditsStamp =
      'missing-tiles-v2:' +
      JSON.stringify(
        await Promise.all(
          paths.map(async (p) => {
            const st = await stat(new URL('Data/' + p, root));
            return [p, st.size, st.mtimeMs];
          }),
        ),
      );
    const creditsCache = new URL('credits-textures.private.json', folder);
    let cachedCredits;
    try {
      const cached = JSON.parse(await readFile(creditsCache));
      if (cached.stamp === creditsStamp) cachedCredits = cached.assets;
    } catch {}
    if (cachedCredits) {
      for (const row of cachedCredits)
        if (!uiAssets.some((a) => a.name.toLowerCase() === row.name.toLowerCase()))
          uiAssets.push(row);
    } else {
      const firstCredit = uiAssets.length;
      const credits = data.resources.find((r) => /creditsframe\.lua$/i.test(r.name));
      if (credits) {
        const text = new TextDecoder().decode(buffers[credits.index]);
        const bases = [...new Set([...text.matchAll(/file="([^"]+)"/g)].map((m) => m[1]))];
        for (const base of bases)
          for (let tile = 1; tile <= 8; tile++) {
            const name = 'Interface\\Glues\\Credits\\' + base + tile;
            if (uiAssets.some((r) => r.name.toLowerCase() === name.toLowerCase())) continue;
            try {
              const { bytes } = await member(name + '.blp');
              const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
              uiAssets.push({ name, width: v.getUint32(12, true), height: v.getUint32(16, true) });
            } catch (e) {
              if (!e.message.startsWith('Current client member missing:')) throw e;
              uiAssets.push({ name, width: 0, height: 0, missing: true });
            }
          }
      }
      await writeFile(
        creditsCache,
        JSON.stringify({ stamp: creditsStamp, assets: uiAssets.slice(firstCredit) }),
      );
    }
    const categoryBytes = (await member('DBFilesClient/Cfg_Categories.dbc')).bytes;
    const categoriesView = new DataView(
      categoryBytes.buffer,
      categoryBytes.byteOffset,
      categoryBytes.byteLength,
    );
    const categoryCount = categoriesView.getUint32(4, true),
      categoryStride = categoriesView.getUint32(12, true),
      categoryStrings = 20 + categoryCount * categoryStride;
    if (
      new TextDecoder().decode(categoryBytes.subarray(0, 4)) !== 'WDBC' ||
      categoryStride !== 84 ||
      categoryCount > 64 ||
      categoryStrings >= categoryBytes.length
    )
      throw Error('Realm category table layout rejected');
    const realmCategories = Array.from({ length: categoryCount }, (_, i) => {
      const at = 20 + i * categoryStride,
        offset = categoriesView.getUint32(at + 16, true);
      if (offset >= categoryBytes.length - categoryStrings)
        throw Error('Realm category label rejected');
      const bytes = categoryBytes.subarray(categoryStrings + offset),
        end = bytes.indexOf(0);
      if (end < 0) throw Error('Realm category label unterminated');
      return {
        id: categoriesView.getUint32(at, true),
        flags: categoriesView.getUint32(at + 12, true),
        label: new TextDecoder().decode(bytes.subarray(0, end)),
      };
    });
    const edit = await member('Fonts\\ARIALN.TTF'),
      editFont = {
        name: 'Fonts\\ARIALN.TTF',
        archive: edit.path,
        bytes: edit.bytes.length,
        sha256: sha(edit.bytes),
      };
    await writeFile(new URL('manifest.private.json', folder), JSON.stringify(manifest));
    const receipt = {
      locale,
      pixel_decoder_sha256: pixelDecoderHash,
      mounts: mounts.map((m) => m.path),
      resources: data.resources.length,
      file_hashes: data.file_hashes,
      scene_jobs: manifest.jobs.length,
      scope:
        'Current local archive-ranked source bytes. Retained native dispatch shape only; no native equivalence for the replacement locale.',
    };
    await writeFile(new URL('receipt.private.json', folder), JSON.stringify(receipt, null, 2));
    characterMetadata.models = [
      ...new Map(
        [...characterMetadata.models, ...backgroundModels].map((path) => [
          path.toLowerCase().replaceAll('/', '\\'),
          path,
        ]),
      ).values(),
    ];
    if (characterMetadata.models.length > 64)
      throw Error('Character model admission capacity exceeded');
    return { files, data, manifest, editFont, uiAssets, realmCategories, characterMetadata };
  } finally {
    await Promise.all(handles.map((f) => f.close()));
  }
}
