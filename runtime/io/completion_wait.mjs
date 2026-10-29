// Synchronous compatibility wait on an engine worker only. Main-thread callers
// fail rather than blocking page input. This is a browser transport policy.
export function waitForCompletion(control, token, timeoutMs = 2000) {
  if (
    !(control instanceof Int32Array) ||
    !(control.buffer instanceof SharedArrayBuffer) ||
    control.length < 3 ||
    !Number.isInteger(token) ||
    token <= 0 ||
    token > 2147483647 ||
    !Number.isFinite(timeoutMs) ||
    timeoutMs < 1 ||
    timeoutMs > 60000
  )
    throw Error('invalid completion wait');
  if (typeof window !== 'undefined' && typeof document !== 'undefined')
    throw Error('completion wait requires engine worker');
  const deadline = performance.now() + timeoutMs;
  for (;;) {
    const state = Atomics.load(control, 0);
    if (state === 1) break;
    if (state !== 0) throw Error('invalid completion state');
    const remaining = deadline - performance.now();
    if (remaining <= 0) throw Error('provider timeout');
    Atomics.wait(control, 0, 0, remaining);
  }
  if (Atomics.load(control, 1) !== token) throw Error('completion token mismatch');
  const status = Atomics.load(control, 2);
  if (status !== 0 && status !== 1) throw Error('invalid completion status');
  if (status) throw Error('provider failed');
}
