export async function runFrameSession(WorkerClass, args, onWait, onMovie, onAudio, onRefresh) {
  const worker = new WorkerClass(new URL('./frame_worker.mjs', import.meta.url), {
      type: 'module',
    }),
    shared = new SharedArrayBuffer(32),
    control = new Int32Array(shared);
  let ticks = 0,
    pendingTicks = 0,
    busy = false,
    deadline,
    retained = false,
    closed = false,
    next = 0,
    rejectInitial,
    closeReply,
    fatal;
  const waits = [],
    pending = new Map();
  const input = (action) => {
    if (closed) return Promise.reject(Error('frame session closed'));
    if (fatal) return Promise.reject(fatal);
    return new Promise((resolve, reject) => {
      const id = ++next,
        timer = setTimeout(() => {
          pending.delete(id);
          reject(Error('UI input deadline'));
        }, 60000);
      pending.set(id, { resolve, reject, timer });
      worker.postMessage({ kind: 'ui-input', id, action });
    });
  };
  const movieAction = (action) =>
    input(action)
      .then((state) => onRefresh?.(state))
      .catch((error) => onRefresh?.({ error: error.message }));
  const timer = setInterval(() => {
    ticks++;
    if (Atomics.load(control, 3)) pendingTicks++;
  }, 1);
  const fail = (error) => {
    fatal = error;
    rejectInitial?.(error);
    for (const row of pending.values()) {
      clearTimeout(row.timer);
      row.reject(error);
    }
    pending.clear();
  };
  const close = async () => {
    if (closed) return;
    closed = true;
    fail(Error('frame session closed'));
    if (retained) {
      await new Promise((resolve) => {
        const timer = setTimeout(resolve, 2000);
        closeReply = () => {
          clearTimeout(timer);
          resolve();
        };
        worker.postMessage({ kind: 'ui-close' });
      });
    }
    await worker.terminate();
  };
  try {
    const result = await new Promise((resolve, reject) => {
      rejectInitial = reject;
      deadline = setTimeout(
        () => reject(Error('frame integration deadline')),
        args.startup ? 360000 : 120000,
      );
      const message = async (value) => {
        try {
          if (value.kind === 'ui-closed') {
            closeReply?.();
            return;
          }
          if (value.kind === 'ui-result') {
            const row = pending.get(value.id);
            if (!row) return;
            pending.delete(value.id);
            clearTimeout(row.timer);
            if (value.error) {
              if (value.recovery) await onRefresh?.(value.recovery);
              row.reject(Error(value.error));
            } else row.resolve(value.result);
            return;
          }
          if (value.kind === 'audio-batch') {
            if (!onAudio) throw Error('audio host missing');
            const acknowledgements = await onAudio(value.requests, value.volumes, value.packet);
            worker.postMessage({ kind: 'audio-ack', acknowledgements });
            return;
          }
          if (value.kind === 'movie-request') {
            if (!onMovie) throw Error('movie host missing');
            await onMovie(
              value.request,
              (token) =>
                retained
                  ? movieAction({ kind: 12, token })
                  : worker.postMessage({ kind: 'movie-finished', token }),
              (key) =>
                retained
                  ? movieAction({ kind: 11, key })
                  : worker.postMessage({ kind: 'movie-key', key }),
            );
            return;
          }
          if (value.kind === 'screen-wait') {
            if (busy) throw Error('concurrent screen transition');
            busy = true;
            let failure = null;
            try {
              const ack = await onWait(value);
              if (ack?.ready !== true) throw Error('screen backend not ready');
            } catch (error) {
              failure = error.message;
            }
            waits.push({
              token: value.token,
              ready: !failure,
              error: failure,
              requests: value.requests,
            });
            Atomics.store(control, 1, value.token);
            Atomics.store(control, 2, failure ? 1 : 0);
            Atomics.store(control, 0, 1);
            Atomics.notify(control, 0);
            busy = false;
          } else if (value.kind === 'done' || value.kind === 'error') {
            retained = value.retained === true;
            resolve(value);
          }
        } catch (error) {
          fail(error);
        }
      };
      if (typeof worker.on === 'function') {
        worker.on('message', message);
        worker.on('error', fail);
      } else {
        worker.onmessage = (event) => message(event.data);
        worker.onerror = (event) => fail(Error(event.message));
      }
      worker.postMessage({ kind: 'init', ...args, shared });
    });
    const session = retained ? { input, close } : undefined;
    return {
      ...result,
      session,
      waits,
      heartbeat_ticks: ticks,
      heartbeat_during_pending: pendingTicks,
    };
  } finally {
    clearInterval(timer);
    clearTimeout(deadline);
    rejectInitial = null;
    if (!retained) await close();
  }
}
