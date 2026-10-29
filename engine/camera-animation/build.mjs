import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const source = await readFile(new URL('sampler.c', import.meta.url)),
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
    ...['input_ptr', 'output_ptr', 'sample'].map((x) => '-Wl,--export=' + x),
    '-o',
    'sampler.wasm',
    'sampler.c',
  ],
  out = await runClang(args, { 'sampler.c': source }),
  sha = (b) => createHash('sha256').update(b).digest('hex');
await writeFile(new URL('build/sampler.wasm', import.meta.url), out['sampler.wasm']);
await writeFile(
  new URL('build/build-receipt.json', import.meta.url),
  JSON.stringify(
    {
      version,
      args,
      source_sha256: sha(source),
      wasm_sha256: sha(out['sampler.wasm']),
      scope:
        'Bounded camera0 position-track local-time linear sampling; no native clock, pose or x87 bit equivalence.',
    },
    null,
    2,
  ) + '\n',
);
