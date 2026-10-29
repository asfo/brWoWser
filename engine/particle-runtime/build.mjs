import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { runClang } from '../../tools/compiler.mjs';
const files = {},
  root = new URL('../../', import.meta.url);
for (const path of [
  'runtime/particle_runtime/runtime.c',
  'runtime/particle_runtime/binding.c',
  'runtime/particle_runtime/binding.h',
  'runtime/particle_generation/generation.c',
  'runtime/particle_generation/generation.h',
  'runtime/particle_shape/shape.c',
  'runtime/particle_shape/shape.h',
  'runtime/particle_assets/assets.c',
  'runtime/particle_draw/head.c',
  'runtime/scene/particles_motion.c',
]) {
  const name = path.split('/').at(-1);
  let source = await readFile(new URL(path, root), 'utf8');
  source = source
    .replaceAll('../particle_generation/generation.h', 'generation.h')
    .replaceAll('../particle_shape/shape.h', 'shape.h');
  files[name] = source;
}
const names = [
  'input_ptr',
  'runtime_bones_ptr',
  'runtime_bone_valid_ptr',
  'runtime_view_ptr',
  'runtime_table_ptr',
  'runtime_vertices_ptr',
  'runtime_batches_ptr',
  'runtime_stats_ptr',
  'runtime_reset',
  'runtime_step',
  'runtime_draw',
];
const args = [
  'clang',
  '-O2',
  '-Wall',
  '-Wextra',
  '-Werror',
  '-ffp-contract=off',
  '-fno-strict-aliasing',
  '-nostartfiles',
  '-Wl,--no-entry',
  '-Wl,-z,stack-size=262144',
  ...names.map((n) => '-Wl,--export=' + n),
  '-o',
  'runtime.wasm',
  'runtime.c',
  'binding.c',
  'generation.c',
  'shape.c',
  'assets.c',
  'head.c',
  'particles_motion.c',
  '-lm',
];
const out = await runClang(args, files);
if (!WebAssembly.validate(out['runtime.wasm'])) throw Error('invalid runtime wasm');
await mkdir(new URL('build/', import.meta.url), { recursive: true });
await writeFile(new URL('build/runtime.wasm', import.meta.url), out['runtime.wasm']);
