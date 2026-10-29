// Keep archive decompression and image decoding off the rendering thread.
// The worker uses only the client files or the bounded local Data mount.
export async function createCharacterAssetSource(files, config) {
  const worker = new Worker(new URL('../../app/client/assets-worker.mjs', import.meta.url), {
    type: 'module',
  });
  const pending = new Map();
  let serial = 0,
    closed = false;
  function fail(error) {
    closed = true;
    worker.terminate();
    for (const job of pending.values()) {
      clearTimeout(job.timer);
      job.reject(error);
    }
    pending.clear();
  }
  worker.onmessage = ({ data }) => {
    const job = pending.get(data.id);
    if (!job) return;
    pending.delete(data.id);
    clearTimeout(job.timer);
    data.error ? job.reject(Error(data.error)) : job.resolve(data.result);
  };
  worker.onerror = (event) => fail(Error(event.message || 'Character asset worker failed'));
  function call(message) {
    if (closed) return Promise.reject(Error('Character asset source is closed'));
    return new Promise((resolve, reject) => {
      const id = ++serial;
      const timer = setTimeout(() => fail(Error('Character asset read timeout')), 30000);
      pending.set(id, { resolve, reject, timer });
      worker.postMessage({ id, ...message });
    });
  }
  try {
    await call({ kind: 'init', files, ...config });
  } catch (error) {
    fail(error);
    throw error;
  }
  const cache = new Map();
  function asset(kind, path) {
    if (
      typeof path !== 'string' ||
      !/^[\x20-\x7e]{1,255}$/.test(path) ||
      path.split(/[\\/]/).some((part) => !part || part === '.' || part === '..')
    )
      return Promise.reject(Error('Invalid character asset path'));
    const key = kind + ':' + path.toLowerCase().replaceAll('\\', '/');
    if (!cache.has(key)) {
      const promise = call({ kind, path }).catch((error) => {
        cache.delete(key);
        throw error;
      });
      cache.set(key, promise);
    }
    return cache.get(key);
  }
  return {
    readAsset: async (path) => new Uint8Array((await asset('bytes', path)).bytes),
    decodeTexture: (path) => asset('texture', path),
    dispose() {
      cache.clear();
      fail(Error('Character asset source closed'));
    },
  };
}
