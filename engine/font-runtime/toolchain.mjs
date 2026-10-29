import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
export async function freetypeInputs() {
  const dir = new URL('./', import.meta.url),
    sha = (b) => createHash('sha256').update(b).digest('hex'),
    receipt = JSON.parse(await readFile(new URL('build/source-receipt.json', dir))),
    files = {};
  for (const row of receipt.files) {
    const b = await readFile(new URL('build/public-source/' + row.path, dir));
    if (sha(b) !== row.sha256) throw Error('FreeType source identity');
    files['ft/' + row.path] = b;
  }
  files['bw-ftmodule.h'] = new TextEncoder().encode(
    'FT_USE_MODULE(FT_Driver_ClassRec,tt_driver_class)\nFT_USE_MODULE(FT_Module_Class,sfnt_module_class)\nFT_USE_MODULE(FT_Renderer_Class,ft_smooth_renderer_class)\nFT_USE_MODULE(FT_Renderer_Class,ft_raster1_renderer_class)\n',
  );
  const options = new TextDecoder()
    .decode(files['ft/include/freetype/config/ftoption.h'])
    .replace(/^#define FT_CONFIG_OPTION_USE_(ZLIB|BZIP2|PNG|HARFBUZZ|BROTLI).*$/gm, '');
  files['bw-ftoption.h'] = new TextEncoder().encode(options);
  return {
    files,
    receipt,
    sources: [
      'base/ftbase.c',
      'base/ftinit.c',
      'base/ftsystem.c',
      'base/ftdebug.c',
      'base/ftbitmap.c',
      'base/ftmm.c',
      'truetype/truetype.c',
      'sfnt/sfnt.c',
      'smooth/smooth.c',
      'raster/raster.c',
      'gzip/ftgzip.c',
    ].map((p) => 'ft/src/' + p),
    args: [
      '-DFT2_BUILD_LIBRARY',
      '-DFT_CONFIG_MODULES_H="bw-ftmodule.h"',
      '-DFT_CONFIG_OPTIONS_H="bw-ftoption.h"',
      '-I.',
      '-Ift/include',
    ],
  };
}
export function fileTree(files) {
  const tree = {};
  for (const [path, bytes] of Object.entries(files)) {
    const parts = path.split('/');
    let node = tree;
    for (const part of parts.slice(0, -1)) node = node[part] ??= {};
    node[parts.at(-1)] = bytes;
  }
  return tree;
}
