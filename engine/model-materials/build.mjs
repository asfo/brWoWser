import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  sha = (b) => createHash('sha256').update(b).digest('hex'),
  records = [];
for (const [name, exports] of [
  [
    'materials',
    [
      'model_ptr',
      'skin_ptr',
      'rows_ptr',
      'stages_ptr',
      'info_ptr',
      'decode_materials',
      'appearance_ptr',
      'sample_appearance',
      'order_ptr',
      'sort_batches',
    ],
  ],
  ['blp', ['input_ptr', 'rgba_ptr', 'image_width', 'image_height', 'decode_blp']],
]) {
  const source = await readFile(new URL(name + '.c', dir)),
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
      ...exports.map((n) => '-Wl,--export=' + n),
      '-o',
      name + '.wasm',
      name + '.c',
    ],
    out = await runClang(args, { [name + '.c']: source });
  if (!WebAssembly.validate(out[name + '.wasm'])) throw Error('invalid wasm');
  await writeFile(new URL('build/' + name + '.wasm', dir), out[name + '.wasm']);
  records.push({ name, args, source_sha256: sha(source), wasm_sha256: sha(out[name + '.wasm']) });
}
await writeFile(
  new URL('build/build-receipt.json', dir),
  JSON.stringify({ version, records }, null, 2) + '\n',
);
console.log(JSON.stringify({ status: 'BUILT', records }));
