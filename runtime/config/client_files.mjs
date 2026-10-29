// Normalize an authorized client-folder or Data-folder selection. No file bytes
// are read here; extra project/cache/tool folders never become archive inputs.
export function clientFiles(files) {
  const rows = Array.from(files, (file) => ({
      file,
      path: String(file.webkitRelativePath || file.name)
        .split('/')
        .slice(1)
        .join('/'),
    })),
    hasData = rows.some((row) => /^data\//i.test(row.path)),
    data = new Map();
  let config = null;
  for (const row of rows) {
    if (hasData && /^wtf\/config\.wtf$/i.test(row.path)) {
      if (config) throw Error('Ambiguous Config.wtf selection');
      config = row.file;
    }
    const path = hasData ? (/^data\//i.test(row.path) ? row.path.slice(5) : null) : row.path;
    if (path === null || !path) continue;
    if (path.split('/').some((part) => !part || part === '.' || part === '..'))
      throw Error('Invalid client path');
    const key = path.toLowerCase();
    if (data.has(key)) throw Error('Ambiguous client data path');
    data.set(key, { file: row.file, path });
  }
  return { data, config, archives: [...data.values()].filter((row) => /\.mpq$/i.test(row.path)) };
}
