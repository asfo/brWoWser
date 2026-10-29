import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const source = await readFile(new URL('bones.c', import.meta.url)),
  exports = [
    'set_normalizer',
    'view_ptr',
    'set_camera',
    'model_ptr',
    'prepared_ptr',
    'matrices_ptr',
    'bone_valid_ptr',
    'positions_ptr',
    'normals_ptr',
    'vertex_valid_ptr',
    'bone_count',
    'pose',
    'skin',
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
    'bones.wasm',
    'bones.c',
  ],
  o = await runClang(args, { 'bones.c': source }),
  sha = (b) => createHash('sha256').update(b).digest('hex');
await writeFile(new URL('build/bones.wasm', import.meta.url), o['bones.wasm']);
await writeFile(
  new URL('build/build-receipt.json', import.meta.url),
  JSON.stringify(
    {
      version,
      args,
      source_sha256: sha(source),
      wasm_sha256: sha(o['bones.wasm']),
      scope:
        'Explicit clocks, subset flags0/512 and descendants; mathematical quaternion normalization; no native oracle.',
    },
    null,
    2,
  ) + '\n',
);
