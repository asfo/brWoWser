// Read owner font through the same ranked MPQ byte transport used by audio.
export async function loadLocalEditFont(WorkerClass, files, config, identity) {
  const worker = new WorkerClass(new URL('./audio-worker.mjs', import.meta.url), {
    type: 'module',
  });
  let next = 0;
  const pending = new Map();
  const rejectAll = (error) => {
    for (const row of pending.values()) {
      clearTimeout(row.timer);
      row.reject(error);
    }
    pending.clear();
  };
  const message = (data) => {
    const row = pending.get(data.id);
    if (!row) return;
    pending.delete(data.id);
    clearTimeout(row.timer);
    data.error ? row.reject(Error(data.error)) : row.resolve(data.result);
  };
  if (typeof worker.on === 'function') {
    worker.on('message', message);
    worker.on('error', rejectAll);
  } else {
    worker.onmessage = (event) => message(event.data);
    worker.onerror = (event) => rejectAll(Error(event.message));
  }
  const call = (value) =>
    new Promise((resolve, reject) => {
      const id = ++next,
        timer = setTimeout(() => {
          pending.delete(id);
          reject(Error('Edit font read timeout'));
        }, 30000);
      pending.set(id, { resolve, reject, timer });
      worker.postMessage({ id, ...value });
    });
  try {
    await call({ kind: 'init', files, ...config });
    const result = await call({ kind: 'bytes', path: identity.name }),
      bytes = result.bytes;
    const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (x) =>
      x.toString(16).padStart(2, '0'),
    ).join('');
    if (bytes.byteLength !== identity.bytes || hash !== identity.sha256)
      throw Error('Edit font identity mismatch');
    return { name: identity.name, bytes, sha256: hash, archive: result.archive };
  } finally {
    rejectAll(Error('Font transport closed'));
    await worker.terminate();
  }
}
