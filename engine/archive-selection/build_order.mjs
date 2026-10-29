import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import { runClang, version } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  source = await readFile(new URL('order.c', dir));
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
  ...['rows_ptr', 'order_ptr', 'rank_candidates', 'first_nonzero'].map((n) => '-Wl,--export=' + n),
  '-o',
  'order.wasm',
  'order.c',
];
const r = await runClang(args, { 'order.c': new Uint8Array(source) }),
  wasm = r['order.wasm'];
assert(WebAssembly.validate(wasm));
const sha = (b) => createHash('sha256').update(b).digest('hex');
await writeFile(new URL('build/order.wasm', dir), wasm);
const receipt = {
  status: 'BUILT_NOT_VALIDATED',
  version,
  args,
  wasm_sha256: sha(wasm),
  source_sha256: sha(source),
  wasm_bytes: wasm.length,
};
await writeFile(
  new URL('build/order-build-receipt.json', dir),
  JSON.stringify(receipt, null, 2) + '\n',
);
console.log(JSON.stringify(receipt));
