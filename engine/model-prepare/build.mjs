import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runClang, version } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  source = await readFile(new URL('prepare.c', dir)),
  render = await readFile(new URL('../../runtime/character/render.c', dir)),
  args = [
    'clang',
    '-O2',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-ffreestanding',
    '-fno-builtin',
    '-nostartfiles',
    '-Wl,--no-entry',
    ...[
      'model_ptr',
      'skin_ptr',
      'output_ptr',
      'info_ptr',
      'prepare_records',
      'character_view_input',
      'character_view_output',
      'character_view',
      'character_placement_input',
      'character_placement_output',
      'character_placement',
      'character_attachment_input',
      'character_attachment_output',
      'character_attachment',
      'character_light_bones',
      'character_light_valid',
      'character_light_output',
      'character_light_count',
      'character_lights',
      'character_skin_begin',
      'character_skin_layer',
      'character_skin_atlas',
      'character_skin_pixels',
      'character_skin_blend',
      'character_skin_end',
    ].map((n) => '-Wl,--export=' + n),
    '-o',
    'prepare.wasm',
    'prepare.c',
    'render.c',
    '-lm',
  ],
  out = await runClang(args, { 'prepare.c': source, 'render.c': render }),
  sha = (b) => createHash('sha256').update(b).digest('hex');
if (!WebAssembly.validate(out['prepare.wasm'])) throw Error('bad wasm');
await writeFile(new URL('build/prepare.wasm', dir), out['prepare.wasm']);
await writeFile(
  new URL('build/build-receipt.json', dir),
  JSON.stringify(
    {
      version,
      args,
      source_sha256: sha(source),
      character_render_sha256: sha(render),
      wasm_sha256: sha(out['prepare.wasm']),
      scope:
        'Selected CPU record copy/slot remap; no full preparation, native runtime or rendering equivalence.',
    },
    null,
    2,
  ) + '\n',
);
