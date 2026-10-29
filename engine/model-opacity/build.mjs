import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  source = await readFile(new URL('opacity.c', dir)),
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
    ...['model_ptr', 'skin_ptr', 'values_ptr', 'visible_ptr', 'count', 'sample_opacity'].map(
      (n) => '-Wl,--export=' + n,
    ),
    '-o',
    'opacity.wasm',
    'opacity.c',
  ],
  out = await runClang(args, { 'opacity.c': source }),
  sha = (b) => createHash('sha256').update(b).digest('hex');
if (!WebAssembly.validate(out['opacity.wasm'])) throw Error('bad wasm');
await writeFile(new URL('build/opacity.wasm', dir), out['opacity.wasm']);
await writeFile(
  new URL('build/build-receipt.json', dir),
  JSON.stringify(
    {
      version,
      args,
      source_sha256: sha(source),
      wasm_sha256: sha(out['opacity.wasm']),
      scope:
        'Selected sequence0 constant texture weights and clamped color-alpha interpolation; owner opacity1. No native clock or evaluated runtime equivalence.',
    },
    null,
    2,
  ) + '\n',
);
