// Keep archive decompression and image decoding off the rendering thread.
// The worker uses only the client files or the bounded local Data mount.
export async function createCharacterAssetSource(files, config) {
  const worker = new Worker(new URL('../../app/client/assets-worker.mjs', import.meta.url), {
    type: 'module',
  });
  const pending = new Map();
  let audioWorker, audioReady, worldWorker, worldReady;
  const worldPending = new Map();
  const audioPending = new Map();
  let serial = 0,
    closed = false;
  function fail(error) {
    closed = true;
    worker.terminate();
    audioWorker?.terminate();
    worldWorker?.terminate();
    for (const job of worldPending.values()) {
      clearTimeout(job.timer);
      job.reject(error);
    }
    worldPending.clear();
    for (const job of audioPending.values()) {
      clearTimeout(job.timer);
      job.reject(error);
    }
    audioPending.clear();
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
  // Sound reads have their own decompression queue so terrain streaming cannot
  // delay playback. Keep the worker lazy until a world actually needs audio.
  function audioCall(message) {
    if (closed) return Promise.reject(Error('Character asset source is closed'));
    return new Promise((resolve, reject) => {
      const id = ++serial;
      const timer = setTimeout(() => {
        audioPending.delete(id);
        reject(Error('World audio read timeout: ' + (message.path || 'mount initialization')));
      }, 60000);
      audioPending.set(id, { resolve, reject, timer });
      audioWorker.postMessage({ id, ...message });
    });
  }
  async function readAudio(path) {
    if (closed) throw Error('Character asset source is closed');
    if (!audioReady) {
      audioWorker = new Worker(new URL('../../app/client/audio-worker.mjs', import.meta.url), {
        type: 'module',
      });
      audioWorker.onmessage = ({ data }) => {
        const job = audioPending.get(data.id);
        if (!job) return;
        audioPending.delete(data.id);
        clearTimeout(job.timer);
        data.error ? job.reject(Error(data.error)) : job.resolve(data.result);
      };
      audioWorker.onerror = (event) => {
        for (const job of audioPending.values()) {
          clearTimeout(job.timer);
          job.reject(Error(event.message || 'World audio worker failed'));
        }
        audioPending.clear();
      };
      audioReady = audioCall({ kind: 'init', files, ...config });
    }
    await audioReady;
    return new Uint8Array((await audioCall({ kind: 'bytes', path })).bytes);
  }
  async function worldCall(operation, data) {
    if (closed) throw Error('Character asset source is closed');
    function request(message) {
      return new Promise((resolve, reject) => {
        const id = ++serial;
        const timer = setTimeout(() => {
          worldPending.delete(id);
          reject(Error('World resource deadline'));
        }, 60000);
        worldPending.set(id, { resolve, reject, timer });
        worldWorker.postMessage({ id, ...message });
      });
    }
    if (!worldReady) {
      worldWorker = new Worker(new URL('../../app/client/assets-worker.mjs', import.meta.url), {
        type: 'module',
      });
      worldWorker.onmessage = ({ data }) => {
        const job = worldPending.get(data.id);
        if (!job) return;
        worldPending.delete(data.id);
        clearTimeout(job.timer);
        data.error ? job.reject(Error(data.error)) : job.resolve(data.result);
      };
      worldWorker.onerror = (event) => {
        for (const job of worldPending.values()) {
          clearTimeout(job.timer);
          job.reject(Error(event.message || 'World worker failed'));
        }
        worldPending.clear();
      };
      worldReady = request({ kind: 'init', files, ...config });
    }
    await worldReady;
    return request({ kind: 'world', operation, ...data });
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
    readAudio,
    hudCatalog: async (state) => (await call({ kind: 'hud-catalog', state })).records,
    readAsset: async (path) => new Uint8Array((await asset('bytes', path)).bytes),
    decodeTexture: (path) => asset('texture', path),
    world: (operation, data = {}) => worldCall(operation, data),
    dispose() {
      cache.clear();
      fail(Error('Character asset source closed'));
    },
  };
}
