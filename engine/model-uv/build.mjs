import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const source = await readFile(new URL('uv.c', import.meta.url)),
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
    ...[
      'model_ptr',
      'matrices_ptr',
      'clocks_ptr',
      'info_ptr',
      'sample',
      'sample_clocks',
      'sample_scroll',
    ].map((x) => '-Wl,--export=' + x),
    '-o',
    'uv.wasm',
    'uv.c',
  ],
  out = await runClang(args, { 'uv.c': source }),
  sha = (b) => createHash('sha256').update(b).digest('hex');
await writeFile(new URL('build/uv.wasm', import.meta.url), out['uv.wasm']);
await writeFile(
  new URL('build/build-receipt.json', import.meta.url),
  JSON.stringify(
    {
      version,
      args,
      source_sha256: sha(source),
      wasm_sha256: sha(out['uv.wasm']),
      scope:
        'Original-file global translation-only texture transforms; explicit global clocks, no native output or initial phase equivalence.',
    },
    null,
    2,
  ) + '\n',
);
