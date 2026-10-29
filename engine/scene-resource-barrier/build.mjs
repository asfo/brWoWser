import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { runClang } from '../../tools/compiler.mjs';
const dir = new URL('./', import.meta.url),
  files = {};
for (const [name, path] of Object.entries({
  'adapter.c': 'adapter.c',
  'wait_all.c': '../../runtime/io/wait_all.c',
  'wait_all.h': '../../runtime/io/wait_all.h',
}))
  files[name] = await readFile(new URL(path, dir));
const result = await runClang(
  [
    'clang',
    '-O2',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-ffreestanding',
    '-nostdlib',
    '-Wl,--no-entry',
    ...['add_job', 'run', 'done', 'remaining_bytes_value', 'progress_size', 'progress_value'].map(
      (n) => '-Wl,--export=' + n,
    ),
    '-o',
    'resources.wasm',
    'adapter.c',
    'wait_all.c',
  ],
  files,
);
if (!WebAssembly.validate(result['resources.wasm'])) throw Error('Invalid resource module');
await mkdir(new URL('build/', dir), { recursive: true });
await writeFile(new URL('build/resources.wasm', dir), result['resources.wasm']);
