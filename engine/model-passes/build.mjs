import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const source = await readFile(new URL('passes.c', import.meta.url)),
  exports = [
    'model_ptr',
    'skin_ptr',
    'output_ptr',
    'batch_count',
    'inputs_ptr',
    'rgba_ptr',
    'resolve',
    'combine',
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
    'passes.wasm',
    'passes.c',
  ],
  out = await runClang(args, { 'passes.c': source }),
  sha = (b) => createHash('sha256').update(b).digest('hex');
await writeFile(new URL('build/passes.wasm', import.meta.url), out['passes.wasm']);
await writeFile(
  new URL('build/build-receipt.json', import.meta.url),
  JSON.stringify(
    {
      version,
      args,
      source_sha256: sha(source),
      wasm_sha256: sha(out['passes.wasm']),
      scope:
        'Selected flag8 shader-word rewrite and explicit ps2 record0 texturing arithmetic; no native dynamic permutation proof.',
    },
    null,
    2,
  ) + '\n',
);
