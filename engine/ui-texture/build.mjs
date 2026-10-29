import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  sha = (b) => createHash('sha256').update(b).digest('hex');
const exports = ['input_ptr', 'rgba_ptr', 'image_width', 'image_height', 'decode_blp'],
  source = await readFile(new URL('../../runtime/graphics/blp.c', dir));
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
    'blp.wasm',
    'blp.c',
  ],
  out = await runClang(args, { 'blp.c': source });
if (!WebAssembly.validate(out['blp.wasm'])) throw Error('invalid WASM');
await writeFile(new URL('build/blp.wasm', dir), out['blp.wasm']);
const receipt = { version, args, source_sha256: sha(source), wasm_sha256: sha(out['blp.wasm']) };
await writeFile(new URL('build/build-receipt.json', dir), JSON.stringify(receipt, null, 2) + '\n');
console.log(JSON.stringify({ status: 'BUILT_BOUNDED_UI_BLP_WASM', ...receipt }));
