import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  source = await readFile(new URL('mesh.c', dir)),
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
    ...['skin_ptr', 'index_ptr', 'range_ptr', 'info_ptr', 'decode_mesh'].map(
      (n) => '-Wl,--export=' + n,
    ),
    '-o',
    'mesh.wasm',
    'mesh.c',
  ],
  out = await runClang(args, { 'mesh.c': source }),
  sha = (b) => createHash('sha256').update(b).digest('hex');
if (!WebAssembly.validate(out['mesh.wasm'])) throw Error('bad wasm');
await writeFile(new URL('build/mesh.wasm', dir), out['mesh.wasm']);
await writeFile(
  new URL('build/build-receipt.json', dir),
  JSON.stringify(
    {
      version,
      args,
      source_sha256: sha(source),
      wasm_sha256: sha(out['mesh.wasm']),
      scope:
        'Bounded level-0 SKIN index/range reader; no native draw selection or GPU equivalence.',
    },
    null,
    2,
  ) + '\n',
);
