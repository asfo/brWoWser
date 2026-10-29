// WebSocket messages are transport chunks, not protocol packet boundaries.
export async function connectAuthTransport({
  url = 'ws://127.0.0.1:8788/auth',
  onBytes = () => {},
  onClose = () => {},
  onError = () => {},
  onControl = () => {},
  timeoutMs = 12000,
  signal,
} = {}) {
  signal?.throwIfAborted();
  const endpoint = new URL(url);
  const healthUrl = new URL('/health', endpoint);
  healthUrl.protocol = endpoint.protocol === 'wss:' ? 'https:' : 'http:';
  let health;
  try {
    const response = await fetch(healthUrl, {
      cache: 'no-store',
      signal: signal
        ? AbortSignal.any([signal, AbortSignal.timeout(3000)])
        : AbortSignal.timeout(3000),
    });
    if (!response.ok) throw Error('HTTP ' + response.status);
    health = await response.json();
  } catch {
    signal?.throwIfAborted();
    throw Error(
      'Cannot reach the local bridge at ' +
        healthUrl.origin +
        '. Stop the previous dev/bridge processes and run npm run dev again.',
    );
  }
  if (health.status !== 'BRIDGE_READY')
    throw Error('The service at ' + healthUrl.origin + ' is not the game bridge.');
  const origin = globalThis.location?.origin;
  if (origin && Array.isArray(health.allowed_origins) && !health.allowed_origins.includes(origin))
    throw Error(
      'This page is open at ' +
        origin +
        ', but the bridge expects ' +
        health.allowed_origins.join(' or ') +
        '.',
    );
  return new Promise((resolve, reject) => {
    signal?.throwIfAborted();
    const socket = new WebSocket(url);
    socket.binaryType = 'arraybuffer';
    let ready = false,
      stopped = false;
    function stop(error, closeSocket = true) {
      if (stopped) return;
      stopped = true;
      clearTimeout(timeout);
      signal?.removeEventListener('abort', abort);
      if (closeSocket) socket.close();
      if (!ready) reject(error || Error('Bridge closed before upstream connection'));
      try {
        if (ready && error) onError(error);
      } finally {
        onClose();
      }
    }
    const abort = () => stop(Error('Transport connection cancelled'));
    const timeout = setTimeout(() => {
      stop(Error('Bridge connection timeout'));
    }, timeoutMs);
    signal?.addEventListener('abort', abort, { once: true });
    function receive(event) {
      if (stopped) return;
      if (typeof event.data === 'string') {
        let message;
        try {
          message = JSON.parse(event.data);
        } catch {
          stop(Error('Invalid bridge status'));
          return;
        }
        if (!message || typeof message !== 'object' || typeof message.type !== 'string') {
          stop(Error('Invalid bridge status'));
          return;
        }
        if (message.type === 'connected' && !ready) {
          ready = true;
          clearTimeout(timeout);
          resolve({
            send(bytes) {
              if (stopped || socket.readyState !== WebSocket.OPEN) throw Error('Transport closed');
              if (!(bytes instanceof Uint8Array)) throw TypeError('Expected protocol bytes');
              if (socket.bufferedAmount + bytes.byteLength > 1024 * 1024)
                throw Error('Transport send queue full');
              socket.send(bytes);
            },
            close() {
              stop();
            },
          });
        } else if (message.type === 'world_routes' && ready) {
          onControl(message);
        } else if (message.type === 'upstream_closed') {
          const error = Error('Game server closed the TCP connection');
          error.code = 'UPSTREAM_CLOSED';
          error.transport = {
            sentBytes: Number.isSafeInteger(message.sent_bytes) ? message.sent_bytes : null,
            receivedBytes: Number.isSafeInteger(message.received_bytes)
              ? message.received_bytes
              : null,
          };
          stop(error);
        } else if (message.type === 'error') {
          stop(Error(typeof message.code === 'string' ? message.code : 'Bridge upstream error'));
        }
      } else if (ready) {
        if (!(event.data instanceof ArrayBuffer)) throw Error('Invalid bridge binary message');
        onBytes(new Uint8Array(event.data));
      } else throw Error('Bridge sent protocol data before upstream connection');
    }
    socket.onmessage = (event) => {
      try {
        receive(event);
      } catch (error) {
        stop(error instanceof Error ? error : Error('Transport message handler failed'));
      }
    };
    socket.onerror = () => {
      const error = Error(
        'The bridge is running, but the browser could not open its connection. ' +
          (endpoint.pathname.startsWith('/world/')
            ? 'Log in again to refresh the realm connection.'
            : 'Restart npm run dev and open http://127.0.0.1:8787/?startup&live.'),
      );
      stop(error);
    };
    socket.onclose = (event) => {
      const error = Error(`Bridge WebSocket closed (code ${event.code}, clean ${event.wasClean})`);
      error.code = 'WEBSOCKET_CLOSED';
      stop(error, false);
    };
  });
}
