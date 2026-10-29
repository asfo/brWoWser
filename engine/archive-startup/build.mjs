import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import { runClang, version } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  source = await readFile(new URL('startup.c', dir)),
  table = await readFile(new URL('base-table.h', dir));
const args = [
  'clang',
  '-O2',
  '-Wall',
  '-Wextra',
  '-Werror',
  '-ffreestanding',
  '-fno-builtin',
  '-nostdlib',
  '-Wl,--no-entry',
  ...['files_ptr', 'locale_ptr', 'mounts_ptr', 'mount_count', 'plan'].map(
    (n) => '-Wl,--export=' + n,
  ),
  '-o',
  'startup.wasm',
  'startup.c',
];
const r = await runClang(args, {
    'startup.c': new Uint8Array(source),
    'base-table.h': new Uint8Array(table),
  }),
  wasm = r['startup.wasm'];
assert(WebAssembly.validate(wasm));
const sha = (b) => createHash('sha256').update(b).digest('hex');
await writeFile(new URL('build/startup.wasm', dir), wasm);
const receipt = {
  status: 'BUILT_NOT_VALIDATED',
  version,
  args,
  wasm_sha256: sha(wasm),
  wasm_bytes: wasm.length,
  source_sha256: sha(source),
  table_sha256: sha(table),
};
await writeFile(new URL('build/build-receipt.json', dir), JSON.stringify(receipt, null, 2) + '\n');
console.log(JSON.stringify(receipt));
