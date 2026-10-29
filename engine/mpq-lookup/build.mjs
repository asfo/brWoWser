import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { runClang } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  source = await readFile(new URL('lookup.c', dir));
const out = await runClang(
  [
    'clang',
    '-O2',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-ffreestanding',
    '-fno-builtin',
    '-nostdlib',
    '-Wl,--no-entry',
    ...[
      'hash_ptr',
      'block_ptr',
      'name_ptr',
      'result_ptr',
      'load_tables',
      'lookup',
      'hash_name',
    ].map((n) => '-Wl,--export=' + n),
    '-o',
    'lookup.wasm',
    'lookup.c',
  ],
  { 'lookup.c': source },
);
if (!WebAssembly.validate(out['lookup.wasm'])) throw Error('Invalid MPQ lookup module');
await mkdir(new URL('build/', dir), { recursive: true });
await writeFile(new URL('build/lookup.wasm', dir), out['lookup.wasm']);
