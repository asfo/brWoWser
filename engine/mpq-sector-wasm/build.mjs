import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import { runClang, version } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  source = await readFile(new URL('decoder.c', dir));
const exports = ['input_ptr', 'output_ptr', 'output_size', 'decode_zlib', 'decode_mpq'];
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
  ...exports.map((n) => '-Wl,--export=' + n),
  '-o',
  'decoder.wasm',
  'decoder.c',
];
const files = await runClang(args, { 'decoder.c': new Uint8Array(source) });
const wasm = files['decoder.wasm'];
assert(WebAssembly.validate(wasm));
assert.deepEqual(WebAssembly.Module.imports(new WebAssembly.Module(wasm)), []);
const sha = (b) => createHash('sha256').update(b).digest('hex');
await writeFile(new URL('build/decoder.wasm', dir), wasm);
const receipt = {
  status: 'BUILT_NOT_VALIDATED',
  compiler: '@yowasp/clang',
  version,
  args,
  source_sha256: sha(source),
  wasm_sha256: sha(wasm),
  wasm_bytes: wasm.length,
  scope:
    'Test-owned bounded DEFLATE/zlib implementation and selected MPQ sector envelope; no original game code or assets embedded.',
};
await writeFile(new URL('build/build-receipt.json', dir), JSON.stringify(receipt, null, 2) + '\n');
console.log(JSON.stringify(receipt));
