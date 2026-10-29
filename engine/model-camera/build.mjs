import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  source = await readFile(new URL('camera.c', dir)),
  args = [
    'clang',
    '-O2',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-ffreestanding',
    '-fno-builtin',
    '-nostdlib',
    '-Wl,--no-entry',
    ...['input_ptr', 'values_ptr', 'counts_ptr', 'decode_camera_zero'].map(
      (n) => '-Wl,--export=' + n,
    ),
    '-o',
    'camera.wasm',
    'camera.c',
  ],
  out = await runClang(args, { 'camera.c': source }),
  sha = (b) => createHash('sha256').update(b).digest('hex');
if (!WebAssembly.validate(out['camera.wasm'])) throw Error('bad wasm');
await writeFile(new URL('build/camera.wasm', dir), out['camera.wasm']);
await writeFile(
  new URL('build/build-receipt.json', dir),
  JSON.stringify(
    {
      version,
      args,
      source_sha256: sha(source),
      wasm_sha256: sha(out['camera.wasm']),
      scope:
        'Bounded camera0 first-key decode only; no general interpolation or native runtime equivalence.',
    },
    null,
    2,
  ) + '\n',
);
