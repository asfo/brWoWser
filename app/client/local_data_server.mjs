import { readdir, realpath, stat } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { join, relative, sep } from 'node:path';
export function parseLocalRange(value, size) {
  const match = /^bytes=(\d+)-(\d+)$/.exec(value || '');
  if (!match) throw Error('Explicit local byte range required');
  const start = Number(match[1]),
    end = Number(match[2]);
  if (
    !Number.isSafeInteger(start) ||
    !Number.isSafeInteger(end) ||
    start < 0 ||
    end < start ||
    end >= size ||
    end - start + 1 > 268435456
  )
    throw Error('Local range rejected');
  return { start, end, length: end - start + 1 };
}
export async function createLocalDataMount(directory) {
  const root = await realpath(directory),
    records = [];
  for (const path of (await readdir(root, { recursive: true })).sort()) {
    if (
      !/\.mpq$/i.test(path) &&
      !/^[a-z]{2}[A-Z]{2}\/(?:eula\.html|tos\.html|termination\.html|Credits(?:_BC|_LK)?\.html|(?:Interface\/Cinematics\/)?(?:Logo|WOW_Intro(?:_BC|_LK)?)_(?:800|1024)\.avi)$/i.test(
        path,
      )
    )
      continue;
    const target = await realpath(join(root, path)),
      rel = relative(root, target);
    if (rel === '..' || rel.startsWith('..' + sep)) throw Error('Local data symlink leaves mount');
    const info = await stat(target);
    if (!info.isFile()) continue;
    records.push({ path: path.split(sep).join('/'), size: info.size, target });
  }
  const inventory = records.map(({ path, size }, id) => ({ path, size, url: '/local-data/' + id }));
  return {
    inventory,
    handle(req, res) {
      const match = /^\/local-data\/(\d+)$/.exec(new URL(req.url, 'http://127.0.0.1').pathname);
      if (!match) return false;
      const row = records[Number(match[1])];
      if (!row || req.method !== 'GET') {
        res.writeHead(404);
        res.end();
        return true;
      }
      let range;
      try {
        range = parseLocalRange(req.headers.range, row.size);
      } catch {
        res.writeHead(416, { 'Content-Range': 'bytes */' + row.size });
        res.end();
        return true;
      }
      res.writeHead(206, {
        'Content-Type': 'application/octet-stream',
        'Content-Length': range.length,
        'Content-Range': `bytes ${range.start}-${range.end}/${row.size}`,
        'Accept-Ranges': 'bytes',
        'Cross-Origin-Resource-Policy': 'same-origin',
        'Cache-Control': 'no-store',
      });
      const stream = createReadStream(row.target, { start: range.start, end: range.end });
      stream.on('error', () => res.destroy());
      req.on('aborted', () => stream.destroy());
      res.on('close', () => stream.destroy());
      stream.pipe(res);
      return true;
    },
  };
}
