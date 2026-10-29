import { listen, send } from '../workers/environment.mjs';
import { waitForCompletion } from '../../runtime/io/completion_wait.mjs';
listen(async ({ wasm, shared, models }) => {
  const control = new Int32Array(shared),
    events = [];
  let token = 0;
  try {
    const { instance } = await WebAssembly.instantiate(wasm, {
        provider: {
          load(index) {
            if (index >= models.length) throw Error('unexpected model request');
            const mine = ++token;
            Atomics.store(control, 0, 0);
            Atomics.store(control, 3, 1);
            Atomics.store(control, 4, 0);
            send({ kind: 'load', index, token: mine });
            events.push({ kind: 'issue', index, token: mine });
            try {
              waitForCompletion(control, mine, 10000);
            } catch (error) {
              const phase =
                [
                  'dispatch',
                  'archive',
                  'local read',
                  'identity verification',
                  'texture decode',
                  'vertex preparation',
                  'complete',
                  'decompression',
                ][Atomics.load(control, 4)] || 'unknown';
              throw Error(error.message + ': ' + models[index].name + ' (' + phase + ')');
            }
            const bytes = Atomics.load(control, 6);
            if (bytes !== models[index].logical_bytes)
              throw Error('model completion size mismatch');
            Atomics.store(control, 3, 0);
            events.push({ kind: 'completed', index, token: mine, bytes });
            return bytes;
          },
        },
      }),
      e = instance.exports;
    for (const model of models)
      if (e.add_job(model.logical_bytes)) throw Error('job admission failed');
    if (e.run() !== 0) throw Error('model barrier failed');
    send({
      kind: 'done',
      completed: e.done(),
      remaining_bytes: e.remaining_bytes_value(),
      progress: Array.from({ length: e.progress_size() }, (_, index) => e.progress_value(index)),
      events,
    });
  } catch (error) {
    send({ kind: 'error', message: error.message, events });
  }
});
