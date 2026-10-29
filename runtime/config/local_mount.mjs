import { readLocalSource } from '../io/local_source.mjs';
export function inventoryFromMount(rows) {
  const data = new Map();
  for (const row of rows) {
    if (
      !row.path ||
      row.path.split('/').some((p) => !p || p === '.' || p === '..') ||
      !Number.isSafeInteger(row.size) ||
      row.size < 0 ||
      !/^\/local-data\/\d+$/.test(row.url)
    )
      throw Error('Invalid local mount inventory');
    const key = row.path.toLowerCase();
    if (data.has(key)) throw Error('Ambiguous local mount');
    data.set(key, {
      ...row,
      file: /\.mpq$/i.test(row.path)
        ? undefined
        : { size: row.size, arrayBuffer: () => readLocalSource(row, 0, row.size) },
    });
  }
  return {
    data,
    config: null,
    archives: [...data.values()]
      .filter((row) => /\.mpq$/i.test(row.path))
      .map(({ file, ...row }) => row),
  };
}
