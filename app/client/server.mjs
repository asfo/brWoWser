import { prepareUISounds } from './ui_sound_catalogue.mjs';
import { currentProfile } from './current_profile.mjs';
import { createLocalDataMount } from './local_data_server.mjs';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import http from 'node:http';
import { readFile, lstat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { modulePaths } from './inputs.mjs';
const startupMode = true,
  localDataMode = true;
const freshMode = process.argv.includes('--fresh');
const dir = new URL('./', import.meta.url),
  root = new URL('../../', dir),
  get = (p) => readFile(new URL(p, dir)),
  sha = (b) => createHash('sha256').update(b).digest('hex'),
  assets = new Map(),
  moduleURLs = {};
let clientImage;
if (localDataMode) {
  const source = new URL('Wow.exe', root),
    info = await lstat(source);
  if (!info.isFile() || info.isSymbolicLink() || info.size < 64 || info.size > 16 * 1024 * 1024)
    throw Error('Expected a regular owner Wow.exe for the guest client image');
  const bytes = await readFile(source);
  clientImage = { url: '/private-client-image.bin', bytes: bytes.length, sha256: sha(bytes) };
  assets.set(clientImage.url, bytes);
}
const localProfile = JSON.parse(
  (
    await promisify(execFile)('python3', [
      '-c',
      'import sys,json;from pathlib import Path;sys.path.insert(0,sys.argv[1]);from client_profile import discover;print(json.dumps(discover(Path(sys.argv[2]))))',
      fileURLToPath(new URL('../../scripts/', dir)),
      fileURLToPath(root),
    ])
  ).stdout,
);
await promisify(execFile)(
  'python3',
  [fileURLToPath(new URL('../../engine/particle-draw/prepare_rng.py', dir))],
  { maxBuffer: 1048576 },
);
const {
    files,
    data,
    manifest: currentManifest,
    editFont,
    realmCategories,
    characterMetadata,
    uiAssets: currentUIAssets,
  } = await currentProfile(localProfile.locale),
  fileURLs = files.map((_, i) => '/private-source/' + i);
for (let i = 0; i < files.length; i++) assets.set(fileURLs[i], await readFile(files[i]));
for (const [k, p] of Object.entries(modulePaths)) {
  moduleURLs[k] = '/modules/' + k + '.wasm';
  assets.set(moduleURLs[k], await get(p));
}
const preparedAudio = startupMode
  ? await prepareUISounds(
      localProfile.locale,
      files,
      JSON.parse(await readFile(new URL('../../config/audio.json', dir))),
    )
  : null;
const audioManifest = preparedAudio?.manifest ?? null;
for (const [path, bytes] of preparedAudio?.files ?? []) assets.set(path, bytes);
const movieManifest = startupMode
  ? JSON.parse(
      (
        await promisify(execFile)(
          'python3',
          [
            fileURLToPath(new URL('../../engine/movie-runtime/prepare_local.py', dir)),
            '--locale',
            localProfile.locale,
          ],
          { maxBuffer: 1048576, timeout: 600000 },
        )
      ).stdout,
    )
  : null;
const movieCatalogue = startupMode
  ? JSON.parse(
      (
        await promisify(execFile)(
          'python3',
          [
            fileURLToPath(new URL('../../engine/movie-runtime/prepare_local.py', dir)),
            '--catalogue',
            '--locale',
            localProfile.locale,
          ],
          { maxBuffer: 1048576 },
        )
      ).stdout,
    ).movies
  : [];
if (startupMode) {
  const movieBytes = await readFile(new URL('../../' + movieManifest.output, dir));
  if (sha(movieBytes) !== movieManifest.output_sha256) throw Error('Private movie cache identity');
  assets.set('/private-movie.webm', movieBytes);
}
const localMount = localDataMode
  ? await createLocalDataMount(fileURLToPath(new URL('../../Data/', dir)))
  : null;
if (localMount)
  assets.set('/local-inventory.json', Buffer.from(JSON.stringify(localMount.inventory)));
let preferredRealm = '';
try {
  const saved = JSON.parse(await get('./build/preferred-realm.private.json'));
  if (
    saved.version !== 1 ||
    typeof saved.realm !== 'string' ||
    !/^[\p{L}\p{N} .'-]{1,255}$/u.test(saved.realm)
  )
    throw Error('Invalid local realm preference');
  preferredRealm = saved.realm;
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
assets.set(
  '/config.json',
  Buffer.from(
    JSON.stringify({
      locale: localProfile.locale,
      realmCategories,
      characterMetadata,
      clientImage,
      preferredRealm,
      localData: !!localMount,
      editFont,
      audio: audioManifest,
      movie: movieManifest,
      movies: movieCatalogue,
      fileURLs,
      fixture: data,
      uiAssets: currentUIAssets,
      indexHash: currentManifest.indexHash,
      moduleURLs,
    }),
  ),
);
for (const [path, p] of Object.entries({
  '/auth.wasm': '../../runtime/network/build/auth.wasm',
  '/frames.wasm': '../../engine/frame-runtime/build/frames-screen.wasm',
  '/resources.wasm': '../../engine/scene-resource-barrier/build/resources.wasm',
  '/lookup.wasm': '../../engine/mpq-lookup/build/lookup.wasm',
  '/decoder.wasm': '../../engine/mpq-sector-wasm/build/decoder.wasm',
  '/blp.wasm': '../../engine/model-materials/build/blp.wasm',
  '/prepare.wasm': '../../engine/model-prepare/build/prepare.wasm',
  '/startup.wasm': '../../engine/archive-startup/build/startup.wasm',
  '/order.wasm': '../../engine/archive-selection/build/order.wasm',
  '/ui-blp.wasm': '../../engine/ui-texture/build/blp.wasm',
}))
  assets.set(path, await get(p));
assets.set('/manifest.json', Buffer.from(JSON.stringify(currentManifest)));
async function source(url) {
  const path = '/' + fileURLToPath(url).slice(fileURLToPath(root).length);
  if (assets.has(path)) return;
  const bytes = await readFile(url);
  assets.set(path, bytes);
  for (const m of bytes
    .toString()
    .matchAll(/(?:from\s*|import\s*)['"]([^'"]+)['"]|new URL\(['"]([^'"]+)['"]/g)) {
    const relative = m[1] || m[2];
    if (relative.startsWith('.') && relative.endsWith('.mjs')) await source(new URL(relative, url));
  }
}
await source(new URL('browser-main.mjs', dir));
await source(new URL('frame_worker.mjs', dir));
await source(new URL('resource-worker.mjs', dir));
await source(new URL('audio-worker.mjs', dir));
await source(new URL('./assets-worker.mjs', dir));
await source(new URL('../../engine/scene-resource-barrier/engine-worker.mjs', dir));
assets.set(
  '/',
  Buffer.from(
    '<!doctype html><meta charset="utf-8"><title>brWoWser</title><style>html,body{margin:0;background:#080b10;color:#ddd;font:14px sans-serif}canvas{display:block;width:min(100vw,1024px);height:auto;aspect-ratio:4/3;outline:none}html.live,html.live body{width:100%;height:100%;overflow:hidden}html.live canvas{position:fixed;inset:0;width:100vw;height:100dvh;aspect-ratio:auto}html.live pre{position:fixed;left:8px;top:8px;max-width:90vw;background:#111c;z-index:2}video[hidden]{display:none}pre{white-space:pre-wrap}button,input{margin:8px}</style><input type="file" webkitdirectory multiple><canvas width="1024" height="768"></canvas><pre>Select the client folder (or Data). Scene and UI run through C/WASM; pending contracts are reported in the result.</pre><script type="module" src="/app/client/browser-main.mjs"></script>',
  ),
);
if (startupMode) {
  const page = assets
    .get('/')
    .toString()
    .replace('<canvas', '<button id="start-startup">Start</button><canvas');
  assets.set('/', Buffer.from(page));
}
const conversions = new Map();
async function serveCinematic(req, res) {
  const match = new URL(req.url, 'http://127.0.0.1').pathname.match(
    /^\/movie-cache\/([A-Za-z0-9_]+)\.(json|webm)$/,
  );
  if (!match) return false;
  const [, name, kind] = match;
  if (!movieCatalogue.some((row) => row.name === name)) {
    res.writeHead(404);
    res.end();
    return true;
  }
  try {
    if (!conversions.has(name))
      conversions.set(
        name,
        promisify(execFile)(
          'python3',
          [
            fileURLToPath(new URL('../../engine/movie-runtime/prepare_local.py', dir)),
            '--locale',
            localProfile.locale,
            '--movie',
            name,
          ],
          { maxBuffer: 1048576, timeout: 600000 },
        )
          .then((result) => JSON.parse(result.stdout))
          .catch((error) => {
            conversions.delete(name);
            throw error;
          }),
      );
    const manifest = await conversions.get(name),
      bytes =
        kind === 'json'
          ? Buffer.from(JSON.stringify(manifest))
          : await readFile(new URL('../../' + manifest.output, dir));
    if (kind === 'webm' && sha(bytes) !== manifest.output_sha256)
      throw Error('cinematic cache identity');
    res.writeHead(200, {
      'Content-Type': kind === 'json' ? 'application/json' : 'video/webm',
      'Cross-Origin-Resource-Policy': 'same-origin',
    });
    res.end(bytes);
  } catch (error) {
    res.writeHead(500, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: error.message }));
  }
  return true;
}
const server = http.createServer(async (req, res) => {
  if (await serveCinematic(req, res)) return;
  if (localMount?.handle(req, res)) return;
  const bytes = assets.get(new URL(req.url, 'http://127.0.0.1').pathname);
  if (!bytes) {
    res.writeHead(404);
    res.end();
    return;
  }
  res.writeHead(200, {
    'Content-Type': req.url.endsWith('.webm')
      ? 'video/webm'
      : req.url.endsWith('.mjs')
        ? 'text/javascript'
        : req.url.endsWith('.wasm')
          ? 'application/wasm'
          : req.url.endsWith('.json')
            ? 'application/json'
            : req.url.endsWith('.bin')
              ? 'application/octet-stream'
              : 'text/html',
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp',
    'Cache-Control': 'no-store',
  });
  res.end(bytes);
});
await new Promise((resolve, reject) => {
  server.once('error', reject);
  server.listen(Number(process.env.BRWOWSER_WEB_PORT || 8787), '127.0.0.1', resolve);
});
console.log(
  'brWoWser: http://127.0.0.1:' +
    server.address().port +
    '/?startup&live' +
    (freshMode ? '&fresh' : ''),
);
for (const signal of ['SIGINT', 'SIGTERM'])
  process.once(signal, () => server.close(() => process.exit(0)));
