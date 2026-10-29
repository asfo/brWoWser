export async function createResourceSession(WorkerClass, args) {
  const engine = new WorkerClass(
      new URL('../../engine/scene-resource-barrier/engine-worker.mjs', import.meta.url),
      { type: 'module' },
    ),
    provider = new WorkerClass(new URL('./resource-worker.mjs', import.meta.url), {
      type: 'module',
    }),
    shared = new SharedArrayBuffer(32),
    control = new Int32Array(shared),
    assets = [],
    pending = new Map();
  let ticks = 0,
    pendingTicks = 0,
    closed = false,
    loaded,
    failed,
    exported,
    exportFailed,
    startupDeadline,
    nextId = 0;
  const attach = (worker, handler, reject) => {
    if (typeof worker.on === 'function') {
      worker.on('message', handler);
      worker.on('error', reject);
    } else {
      worker.onmessage = (event) => handler(event.data);
      worker.onerror = (event) => reject(Error(event.message));
    }
  };
  const timer = setInterval(() => {
      ticks++;
      if (Atomics.load(control, 3)) pendingTicks++;
    }, 1),
    rejectAll = (error) => {
      clearTimeout(startupDeadline);
      failed?.(error);
      exportFailed?.(error);
      for (const entry of pending.values()) {
        clearTimeout(entry.timeout);
        entry.reject(error);
      }
      pending.clear();
    };
  const close = async () => {
    if (closed) return;
    closed = true;
    clearInterval(timer);
    rejectAll(Error('resource session closed'));
    await engine.terminate();
    await provider.terminate();
  };
  try {
    const result = await new Promise((resolve, reject) => {
      loaded = resolve;
      failed = reject;
      attach(
        provider,
        (message) => {
          if (message.kind === 'provider-ready') {
            clearTimeout(startupDeadline);
            engine.postMessage({ wasm: args.wasm, models: args.models, shared });
          } else if (message.kind === 'asset-retained') assets.push(message);
          else if (message.kind === 'asset-error') rejectAll(Error(message.message));
          else if (message.kind === 'gpu-payload') exported?.(message);
          else if (message.kind === 'gpu-export-error') exportFailed?.(Error(message.message));
          else if (message.id) {
            const entry = pending.get(message.id);
            if (entry) {
              pending.delete(message.id);
              clearTimeout(entry.timeout);
              message.kind === 'rpc-error'
                ? entry.reject(Error(message.message))
                : entry.resolve(message);
            }
          }
        },
        rejectAll,
      );
      attach(
        engine,
        (message) => {
          if (message.kind === 'load') provider.postMessage(message);
          else if (message.kind === 'done') loaded(message);
          else if (message.kind === 'error') rejectAll(Error(message.message));
        },
        rejectAll,
      );
      startupDeadline = setTimeout(
        () => rejectAll(Error('resource provider initialization deadline')),
        30000,
      );
      provider.postMessage({ kind: 'init', ...args, shared });
    });
    const data = await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(Error('resource export deadline')), 30000);
      exported = (value) => {
        clearTimeout(timeout);
        resolve(value);
      };
      exportFailed = (error) => {
        clearTimeout(timeout);
        reject(error);
      };
      provider.postMessage({ kind: 'export-gpu' });
    });
    return {
      batch: result,
      assets,
      payload: data.payload,
      scene: data.scene,
      metrics: () => ({ heartbeat_ticks: ticks, heartbeat_during_pending: pendingTicks }),
      sample(sequenceMs, globalMs = sequenceMs) {
        if (closed) return Promise.reject(Error('resource session closed'));
        const id = ++nextId;
        return new Promise((resolve, reject) => {
          const timeout = setTimeout(() => {
            pending.delete(id);
            reject(Error('scene sample timeout'));
          }, 10000);
          pending.set(id, { resolve, reject, timeout });
          provider.postMessage({ kind: 'sample', id, sequenceMs, globalMs });
        });
      },
      close,
    };
  } catch (error) {
    await close();
    throw error;
  }
}
