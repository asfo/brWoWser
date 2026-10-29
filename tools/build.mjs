import { readFile, writeFile, mkdir, readdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
const root = new URL('../', import.meta.url),
  run = promisify(execFile),
  hash = (b) => createHash('sha256').update(b).digest('hex');
const builds = [
  'world',
  'archive-startup',
  'archive-selection',
  'mpq-lookup',
  'mpq-sector-wasm',
  'model-prepare',
  'model-mesh',
  'model-materials',
  'model-camera',
  'model-bones',
  'model-billboards',
  'model-opacity',
  'camera-animation',
  'model-uv',
  'model-passes',
  'particle-runtime',
  'ui-texture',
  'frame-runtime',
  'scene-resource-barrier',
];
const sourceInventory = await run(
  'python3',
  [
    '-c',
    'import sys,json;sys.path.insert(0,"scripts");from repository_policy import inventory;print(json.dumps([r["path"] for r in inventory()]))',
  ],
  { cwd: fileURLToPath(root) },
);
const sources = JSON.parse(sourceInventory.stdout).filter(
  (p) =>
    /\.(c|h|inc)$/.test(p) ||
    /\/(build(?:_order)?|toolchain)\.mjs$/.test(p) ||
    [
      'tools/compiler.mjs',
      'tools/build.mjs',
      'tools/setup.py',
      'scripts/build_auth.mjs',
      'scripts/x86_toolchain.mjs',
      'runtime/network/x86-toolchain.json',
      'package-lock.json',
    ].includes(p),
);
sources.sort();
const fingerprint = hash(
  Buffer.concat(
    await Promise.all(
      sources.map(async (p) =>
        Buffer.concat([Buffer.from(p + '\0'), await readFile(new URL(p, root))]),
      ),
    ),
  ),
);
const receiptURL = new URL('.local/build.json', root);
if (!process.argv.includes('--force'))
  try {
    const prior = JSON.parse(await readFile(receiptURL));
    if (prior.fingerprint === fingerprint) {
      let valid = true;
      for (const [p, h] of Object.entries(prior.outputs))
        if (hash(await readFile(new URL(p, root))) !== h) valid = false;
      if (valid) {
        console.log('brWoWser: WASM modules are current.');
        process.exit(0);
      }
    }
  } catch {
    /* Missing or changed outputs are rebuilt. */
  }
console.log((await run('python3', [fileURLToPath(new URL('tools/setup.py', root))])).stdout.trim());
for (const name of builds) {
  await mkdir(new URL('engine/' + name + '/build/', root), { recursive: true });
  console.log('Building ' + name + '...');
  const file = name === 'archive-selection' ? 'build_order.mjs' : 'build.mjs';
  const { stderr } = await run(
    process.execPath,
    [
      fileURLToPath(new URL('engine/' + name + '/' + file, root)),
      ...(name === 'frame-runtime' ? ['--browser-screen'] : []),
    ],
    { maxBuffer: 8 * 1024 * 1024 },
  );
  if (stderr) process.stderr.write(stderr);
}
console.log('Building network module...');
const network = await run(
  process.execPath,
  [fileURLToPath(new URL('scripts/build_auth.mjs', root))],
  { maxBuffer: 8 * 1024 * 1024 },
);
if (network.stderr) process.stderr.write(network.stderr);
const outputs = {};
for (const name of builds)
  for (const file of await readdir(new URL('engine/' + name + '/build/', root)))
    if (file.endsWith('.wasm')) {
      const p = 'engine/' + name + '/build/' + file;
      outputs[p] = hash(await readFile(new URL(p, root)));
    }
outputs['runtime/network/build/auth.wasm'] = hash(
  await readFile(new URL('runtime/network/build/auth.wasm', root)),
);
await mkdir(new URL('.local/', root), { recursive: true });
await writeFile(receiptURL, JSON.stringify({ fingerprint, outputs }, null, 2) + '\n');
console.log('brWoWser: build complete.');
