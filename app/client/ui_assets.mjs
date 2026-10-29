export async function loadUIImages(files, config, packet, cache = new Map()) {
  const required = [
    ...new Set([
      ...packet.commands
        .filter((c) => c.kind === 'images')
        .flatMap((c) => c.quads.map((q) => q.asset)),
      ...(packet.preloadAssets || []),
    ]),
  ].filter((path) => path !== '@solid' && !cache.has(path.toLowerCase().replaceAll('\\', '/')));
  if (!required.length) return { images: cache, issues: [] };
  const worker = new Worker(new URL('./assets-worker.mjs', import.meta.url), {
      type: 'module',
    }),
    pending = new Map();
  let next = 0;
  worker.onmessage = ({ data }) => {
    const row = pending.get(data.id);
    if (!row) return;
    pending.delete(data.id);
    clearTimeout(row.timer);
    data.error ? row.reject(Error(data.error)) : row.resolve(data.result);
  };
  worker.onerror = (event) => {
    for (const row of pending.values()) {
      clearTimeout(row.timer);
      row.reject(Error(event.message));
    }
    pending.clear();
  };
  const call = (message) =>
    new Promise((resolve, reject) => {
      const id = ++next,
        timer = setTimeout(() => {
          pending.delete(id);
          reject(Error('UI asset deadline'));
        }, 30000);
      pending.set(id, { resolve, reject, timer });
      worker.postMessage({ id, ...message });
    });
  try {
    await call({ kind: 'init', files, locale: 'esES', ...config });
    const images = new Map(cache),
      issues = [],
      paths = required;
    for (const path of paths) {
      if (path === '@solid') continue;
      try {
        const image = await call({ kind: 'texture', path });
        images.set(path.toLowerCase().replaceAll('\\', '/'), image);
      } catch (error) {
        issues.push({ path, error: error.message });
      }
    }
    return { images, issues };
  } finally {
    worker.terminate();
    for (const row of pending.values()) {
      clearTimeout(row.timer);
      row.reject(Error('UI provider closed'));
    }
    pending.clear();
  }
}
