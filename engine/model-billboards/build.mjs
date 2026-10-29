import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const source = await readFile(new URL('billboards.c', import.meta.url)),
  exports = [
    'model_ptr',
    'prepared_ptr',
    'world_ptr',
    'matrices_ptr',
    'bone_valid_ptr',
    'positions_ptr',
    'vertex_valid_ptr',
    'bone_count',
    'prepare_billboards',
  ],
  args = [
    'clang',
    '-O2',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-ffreestanding',
    '-fno-builtin',
    '-ffp-contract=off',
    '-nostdlib',
    '-Wl,--no-entry',
    ...exports.map((x) => '-Wl,--export=' + x),
    '-o',
    'billboards.wasm',
    'billboards.c',
  ],
  out = await runClang(args, { 'billboards.c': source }),
  sha = (b) => createHash('sha256').update(b).digest('hex');
await writeFile(new URL('build/billboards.wasm', import.meta.url), out['billboards.wasm']);
await writeFile(
  new URL('build/build-receipt.json', import.meta.url),
  JSON.stringify(
    {
      version,
      args,
      source_sha256: sha(source),
      wasm_sha256: sha(out['billboards.wasm']),
      scope:
        'Selected sourceflag64 root/no-track branch, explicit instance-world matrix and model-local palette. Native billboard oracle pending.',
    },
    null,
    2,
  ) + '\n',
);
