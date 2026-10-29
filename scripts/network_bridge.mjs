import { createWorldRouteGate, isMachineHost } from '../runtime/network/world_routes.mjs';
import http from 'node:http';
import net from 'node:net';
import { WebSocketServer, WebSocket } from 'ws';

// Fixed upstream; never expose an arbitrary TCP proxy to page input.
const host = process.env.BRWOWSER_AUTH_HOST || '127.0.0.1';
if (!isMachineHost(host)) throw Error('The bridge only connects to an auth server on this machine');
const port = Number(process.env.BRWOWSER_AUTH_PORT || 3724);
const listenPort = Number(process.env.BRWOWSER_BRIDGE_PORT || 8788);
const allowedOrigin = process.env.BRWOWSER_WEB_ORIGIN || 'http://127.0.0.1:8787';
const allowedOrigins = new Set([allowedOrigin]);
const localOrigin = new URL(allowedOrigin);
if (localOrigin.hostname === '127.0.0.1' || localOrigin.hostname === 'localhost') {
  for (const hostname of ['127.0.0.1', 'localhost']) {
    const alias = new URL(localOrigin);
    alias.hostname = hostname;
    allowedOrigins.add(alias.origin);
  }
}
if (![port, listenPort].every((value) => Number.isInteger(value) && value > 0 && value <= 65535))
  throw Error('Invalid bridge port');
const server = http.createServer((request, response) => {
  response.writeHead(request.url === '/health' ? 200 : 404, {
    'Content-Type': 'application/json',
    'Cache-Control': 'no-store',
    ...(request.url === '/health' ? { 'Access-Control-Allow-Origin': '*' } : {}),
  });
  response.end(
    JSON.stringify(
      request.url === '/health'
        ? {
            status: 'BRIDGE_READY',
            protocol: 2,
            upstream: { host, port },
            routes: ['/auth', '/world/:ticket'],
            allowed_origins: [...allowedOrigins],
            authentication: false,
          }
        : { error: 'Not found' },
    ),
  );
});
const sockets = new Set();
const worldRoutes = createWorldRouteGate();
const wss = new WebSocketServer({ noServer: true, maxPayload: 65536, perMessageDeflate: false });
server.on('upgrade', (request, socket, head) => {
  const worldMatch = /^\/world\/([a-f0-9]{48})$/.exec(request.url || '');
  if (!allowedOrigins.has(request.headers.origin) || (request.url !== '/auth' && !worldMatch)) {
    socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');
    return;
  }
  const upstream = worldMatch ? worldRoutes.consume(worldMatch[1]) : { host, port };
  if (!upstream) {
    socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');
    return;
  }
  wss.handleUpgrade(request, socket, head, (ws) =>
    wss.emit('connection', ws, { ...upstream, auth: !worldMatch }),
  );
});
wss.on('connection', (ws, upstream) => {
  const observer = upstream.auth
    ? worldRoutes.observe(ws, (message) => ws.send(JSON.stringify(message)))
    : null;
  const tcp = net.createConnection({ host: upstream.host, port: upstream.port });
  sockets.add(tcp);
  let sentBytes = 0,
    receivedBytes = 0;
  const startedAt = performance.now();
  let lastSentAt = startedAt,
    lastReceivedAt = startedAt;
  function closureDetail() {
    const now = performance.now();
    return {
      connection: upstream.auth ? 'auth' : 'world',
      elapsed_ms: Math.round(now - startedAt),
      sent_bytes: sentBytes,
      received_bytes: receivedBytes,
      send_idle_ms: Math.round(now - lastSentAt),
      receive_idle_ms: Math.round(now - lastReceivedAt),
    };
  }
  let ready = false,
    closed = false;
  const timer = setTimeout(() => fail('UPSTREAM_CONNECT_TIMEOUT'), 10000);
  function stop() {
    if (closed) return;
    closed = true;
    clearTimeout(timer);
    observer?.close();
    sockets.delete(tcp);
    tcp.destroy();
  }
  function fail(code) {
    if (closed) return;
    console.warn(JSON.stringify({ status: 'BRIDGE_CONNECTION_FAILED', code, ...closureDetail() }));
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ type: 'error', code }));
      ws.close(1011, code);
    }
    stop();
  }
  tcp.setNoDelay(true);
  tcp.once('connect', () => {
    clearTimeout(timer);
    ready = true;
    ws.send(JSON.stringify({ type: 'connected', transport: 'tcp', authentication: false }));
  });
  tcp.on('data', (bytes) => {
    lastReceivedAt = performance.now();
    receivedBytes += bytes.length;
    if (ws.readyState !== WebSocket.OPEN) return;
    if (ws.bufferedAmount + bytes.length > 1024 * 1024) {
      fail('WEBSOCKET_BACKPRESSURE_LIMIT');
      return;
    }
    observer?.server(bytes);
    ws.send(bytes, { binary: true }, (error) => {
      if (error) fail('WEBSOCKET_WRITE_FAILED');
      else if (!closed && ws.bufferedAmount <= 65536) tcp.resume();
    });
    if (ws.bufferedAmount > 262144) tcp.pause();
  });
  tcp.on('error', () => fail('UPSTREAM_CONNECTION_FAILED'));
  tcp.on('end', () => {
    console.info(JSON.stringify({ status: 'BRIDGE_UPSTREAM_CLOSED', ...closureDetail() }));
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(
        JSON.stringify({
          type: 'upstream_closed',
          ...closureDetail(),
        }),
      );
      ws.close(1000, 'UPSTREAM_CLOSED');
    }
    stop();
  });
  ws.on('message', (bytes, binary) => {
    if (!ready || !binary) {
      ws.close(1008, 'BINARY_AFTER_CONNECTED_REQUIRED');
      stop();
      return;
    }
    // Bound queued writes even when the upstream stops consuming.
    if (tcp.writableLength + bytes.length > 1024 * 1024) {
      fail('UPSTREAM_BACKPRESSURE_LIMIT');
      return;
    }
    observer?.client(bytes);
    sentBytes += bytes.length;
    lastSentAt = performance.now();
    if (!tcp.write(bytes)) {
      ws.pause();
      tcp.once('drain', () => {
        if (!closed) ws.resume();
      });
    }
  });
  ws.on('error', stop);
  ws.on('close', stop);
});
server.listen(listenPort, '127.0.0.1', () =>
  console.log(
    JSON.stringify({
      status: 'BRIDGE_LISTENING',
      url: `ws://127.0.0.1:${listenPort}/auth`,
      upstream: `${host}:${port}`,
      origin: allowedOrigin,
      authentication: false,
    }),
  ),
);
function shutdown() {
  for (const client of wss.clients) client.terminate();
  for (const socket of sockets) socket.destroy();
  server.close();
}
process.once('SIGINT', shutdown);
process.once('SIGTERM', shutdown);
