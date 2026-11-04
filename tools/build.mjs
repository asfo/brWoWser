import { readFile, writeFile, mkdir, readdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { execFile, spawn } from 'node:child_process';
import { acquireBuildLock } from './build_lock.mjs';
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
const buildPlan = builds.map((name) => ({
  name,
  script: 'engine/' + name + '/' + (name === 'archive-selection' ? 'build_order.mjs' : 'build.mjs'),
  args: name === 'frame-runtime' ? ['--browser-screen'] : [],
}));
const networkBuild = { script: 'scripts/build_auth.mjs', args: [] };
let stopping = false,
  activeChild;
for (const signal of ['SIGINT', 'SIGTERM'])
  process.once(signal, () => {
    stopping = true;
    activeChild?.kill(signal);
  });
function runBuild(args, label) {
  if (stopping) throw Error('Build cancelled');
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, args, {
      cwd: fileURLToPath(root),
      stdio: ['ignore', 'pipe', 'inherit'],
    });
    let diagnosticOutput = '';
    child.stdout.setEncoding('utf8');
    child.stdout.on('data', (text) => {
      diagnosticOutput = (diagnosticOutput + text).slice(-65536);
    });
    activeChild = child;
    const started = Date.now();
    const timer = setInterval(
      () =>
        console.log(
          'Compiling ' + label + ': ' + Math.round((Date.now() - started) / 1000) + 's elapsed...',
        ),
      15000,
    );
    child.once('error', (error) => {
      clearInterval(timer);
      activeChild = null;
      reject(error);
    });
    child.once('exit', (code, signal) => {
      clearInterval(timer);
      activeChild = null;
      if (code === 0) resolve();
      else {
        if (diagnosticOutput) process.stderr.write(diagnosticOutput);
        reject(Error('Build ' + label + ' stopped: ' + (signal || code)));
      }
    });
  });
}
async function main() {
  const release = await acquireBuildLock(root, () => stopping);
  if (!release) return;
  try {
    const sourceInventory = await run(
      'python3',
      [
        '-c',
        'import sys,json;sys.path.insert(0,"scripts");from repository_policy import inventory;print(json.dumps([r["path"] for r in inventory()]))',
      ],
      { cwd: fileURLToPath(root) },
    );
    // Orchestration changes do not alter WASM. Native inputs, module build scripts,
    // compiler/toolchain and dependency versions remain part of the fingerprint.
    const sources = JSON.parse(sourceInventory.stdout).filter(
      (p) =>
        p !== 'tools/build.mjs' &&
        (/\.(c|h|inc)$/.test(p) ||
          /\/(build(?:_order)?|toolchain)\.mjs$/.test(p) ||
          [
            'tools/compiler.mjs',
            'tools/setup.py',
            'scripts/build_auth.mjs',
            'scripts/x86_toolchain.mjs',
            'runtime/network/x86-toolchain.json',
            'package-lock.json',
          ].includes(p)),
    );
    sources.sort();
    const fingerprint = hash(
      Buffer.concat([
        Buffer.from(JSON.stringify({ buildPlan, networkBuild }) + '\0'),
        ...(await Promise.all(
          sources.map(async (p) =>
            Buffer.concat([Buffer.from(p + '\0'), await readFile(new URL(p, root))]),
          ),
        )),
      ]),
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
            return;
          }
        }
      } catch {
        /* Missing or changed outputs are rebuilt. */
      }
    console.log(
      (await run('python3', [fileURLToPath(new URL('tools/setup.py', root))])).stdout.trim(),
    );
    for (const job of buildPlan) {
      if (stopping) return;
      await mkdir(new URL('engine/' + job.name + '/build/', root), { recursive: true });
      console.log('Building ' + job.name + '...');
      await runBuild([fileURLToPath(new URL(job.script, root)), ...job.args], job.name);
    }
    console.log('Building network module...');
    await runBuild(
      [fileURLToPath(new URL(networkBuild.script, root)), ...networkBuild.args],
      'network',
    );
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
  } finally {
    release();
  }
}
try {
  await main();
} catch (error) {
  console.error(stopping ? 'WASM build cancelled.' : error.message);
  process.exitCode = stopping ? 130 : 1;
}
