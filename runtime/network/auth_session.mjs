import { loadClientImage } from './client_image.mjs';
import { connectWorldSession } from './world_session.mjs';
import { readRealms } from './realm_protocol.mjs';
import { buildLogonChallenge, createChallengeReader } from './logon_challenge.mjs';
import { connectAuthTransport } from './transport.mjs';

const resultNames = new Map([
  [3, 'Account banned'],
  [4, 'Account name or password rejected'],
  [5, 'Account name or password rejected'],
  [8, 'Authentication server unavailable'],
  [9, 'Client version rejected'],
  [12, 'Account suspended'],
  [13, 'Account access denied'],
  [16, 'Account locked'],
]);
const rejectCode = (code) => Error(resultNames.get(code) || `Authentication rejected (${code})`);
// One user-initiated attempt. The worker owns the WASM instance; no storage/logging.
export async function authenticateAccount({
  username,
  password,
  locale = 'enUS',
  signal,
  url,
  executeWardenLua,
  clientImage,
  diagnosticWardenAudit = false,
}) {
  const request = buildLogonChallenge(username, { locale });
  if (typeof password !== 'string' || !password.length) throw Error('Enter your account password.');
  if (password.length > 16)
    throw Error('The account password exceeds the supported 16-character limit.');
  if (/[^\x20-\x7e]/.test(password))
    throw Error('This client does not support non-ASCII password characters yet.');
  if (signal?.aborted) throw Error('Authentication cancelled');
  const response = await fetch('/auth.wasm', { signal });
  if (!response.ok) throw Error('Authentication module unavailable');
  let e;
  const { instance } = await WebAssembly.instantiate(await response.arrayBuffer(), {
    env: {
      bw_clock_unix_ms: () => Date.now(),
      bw_clock_monotonic_ms: () => performance.now(),
      bw_warden_lua_execute: (pointer, length) => {
        if (
          !e ||
          typeof executeWardenLua !== 'function' ||
          length > 4095 ||
          pointer > e.memory.buffer.byteLength - length
        )
          return 2;
        try {
          const source = new TextDecoder('utf-8', { fatal: true }).decode(
            new Uint8Array(e.memory.buffer, pointer, length),
          );
          return executeWardenLua(source) === true ? 0 : 1;
        } catch {
          return 1;
        }
      },
    },
  });
  e = instance.exports;
  e._initialize?.();
  e.bw_auth_clear();
  const io = () => new Uint8Array(e.memory.buffer, e.bw_auth_io(), 512);
  const name = new TextEncoder().encode(username),
    secret = new TextEncoder().encode(password);
  const random = crypto.getRandomValues(new Uint8Array(32));
  io().set(name);
  io().set(secret, name.length);
  io().set(random, name.length + secret.length);
  const status = e.bw_auth_credentials(name.length, secret.length);
  name.fill(0);
  secret.fill(0);
  random.fill(0);
  password = '';
  username = '';
  if (status) {
    e.bw_auth_clear();
    throw Error('Authentication credentials rejected by WASM');
  }
  if (clientImage) {
    try {
      await loadClientImage(e, clientImage, signal);
    } catch (error) {
      e.bw_auth_clear();
      request.fill(0);
      throw error;
    }
  }
  return new Promise((resolve, reject) => {
    let transport,
      closed = false,
      delivered = false,
      stage = 'challenge',
      pending = new Uint8Array(),
      refresh,
      routes = [],
      routeTime = 0,
      latestRealms = [],
      worldAttempt;
    const reader = createChallengeReader();
    const timer = setTimeout(() => terminate(Error('Authentication response timeout')), 20000);
    const abort = () => terminate(Error('Authentication cancelled'));
    function terminate(error = Error('Authentication connection closed')) {
      if (closed) return;
      closed = true;
      clearTimeout(timer);
      clearTimeout(refresh?.timer);
      signal?.removeEventListener('abort', abort);
      transport?.close();
      if (!worldAttempt) e.bw_auth_clear();
      request.fill(0);
      pending.fill(0);
      if (!delivered) reject(error);
      refresh?.reject(error);
      refresh = null;
    }
    let sessionApi;
    function receiveRealms(realms) {
      latestRealms = realms;
      pending.fill(0);
      pending = new Uint8Array();
      stage = 'idle';
      if (!delivered) {
        delivered = true;
        clearTimeout(timer);
        signal?.removeEventListener('abort', abort);
        sessionApi = {
          authenticated: true,
          server_proof_verified: true,
          realms,
          close() {
            worldAttempt?.controller.abort();
            terminate();
            e.bw_auth_clear();
          },
          async connectRealm(name, { signal, onStatus } = {}) {
            if (closed) throw Error('Authentication connection closed. Please log in again.');
            signal?.throwIfAborted();
            worldAttempt?.controller.abort();
            // Reserve ownership before any await: all attempts share one WASM instance.
            const controller = new AbortController(),
              attempt = { controller };
            worldAttempt = attempt;
            const abort = () => controller.abort();
            signal?.addEventListener('abort', abort, { once: true });
            if (signal?.aborted) abort();
            try {
              let row = latestRealms.find((row) => row.name === name),
                route =
                  row &&
                  routes.find((route) => route.id === row.id && route.address === row.address);
              if (!route || Date.now() - routeTime > 80000) {
                await (refresh?.promise || sessionApi.requestRealms());
                row = latestRealms.find((row) => row.name === name);
                route =
                  row &&
                  routes.find((route) => route.id === row.id && route.address === row.address);
              }
              controller.signal.throwIfAborted();
              if (closed || worldAttempt !== attempt)
                throw Error('World connection attempt is no longer active');
              if (!row || !route)
                throw Error(
                  'The authentication server did not authorize a world route for that realm',
                );
              routes = routes.filter((candidate) => candidate !== route);
              const endpoint = new URL(url || 'ws://127.0.0.1:8788/auth');
              endpoint.pathname = '/world/' + route.ticket;
              endpoint.search = '';
              const session = await connectWorldSession({
                e,
                realm: row,
                url: endpoint.href,
                signal: controller.signal,
                onStatus,
                onClosed() {
                  signal?.removeEventListener('abort', abort);
                  if (worldAttempt === attempt) worldAttempt = null;
                  if (closed) e.bw_auth_clear();
                },
              });
              attempt.session = session;
              return session;
            } catch (error) {
              signal?.removeEventListener('abort', abort);
              if (worldAttempt === attempt) worldAttempt = null;
              if (closed) e.bw_auth_clear();
              throw error;
            }
          },
          requestRealms() {
            if (closed)
              return Promise.reject(
                Error('Authentication connection closed. Please log in again.'),
              );
            if (refresh) return Promise.reject(Error('Realm refresh already pending'));
            const promise = new Promise((resolve, reject) => {
              refresh = {
                resolve,
                reject,
                timer: setTimeout(() => terminate(Error('Realm list response timeout')), 15000),
              };
              stage = 'realms';
              pending = new Uint8Array();
              try {
                transport.send(new Uint8Array([16, 0, 0, 0, 0]));
              } catch (error) {
                terminate(error);
              }
            });
            if (refresh) refresh.promise = promise;
            return promise;
          },
        };
        resolve(sessionApi);
      } else if (refresh) {
        clearTimeout(refresh.timer);
        refresh.resolve(realms);
        refresh = null;
      }
    }
    signal?.addEventListener('abort', abort, { once: true });
    if (signal?.aborted) {
      abort();
      return;
    }
    connectAuthTransport({
      signal,
      url,
      onError: terminate,
      onControl(message) {
        routes = Array.isArray(message.routes) ? message.routes : [];
        routeTime = Date.now();
      },
      onClose: () => terminate(),
      onBytes(chunk) {
        if (closed) return;
        try {
          if (stage === 'challenge') {
            const challenge = reader.push(chunk);
            if (!challenge) return;
            if (!challenge.accepted) throw rejectCode(challenge.result);
            if (challenge.securityFlags)
              throw Error(
                'This account requires a security token, PIN or matrix; that input is not implemented yet',
              );
            io().set(challenge.bytes);
            const result = e.bw_auth_challenge(challenge.bytes.length);
            challenge.bytes.fill(0);
            if (result) throw Error(`Unsupported server SRP challenge (${result})`);
            stage = 'proof';
            transport.send(io().slice(0, 75));
            io().fill(0);
            return;
          }
          if (stage === 'idle') throw Error('Unexpected authentication server data');
          if (pending.length + chunk.length > 65538)
            throw Error('Authentication response exceeds limit');
          const joined = new Uint8Array(pending.length + chunk.length);
          joined.set(pending);
          joined.set(chunk, pending.length);
          pending = joined;
          if (stage === 'proof') {
            if (pending.length < 2) return;
            if (pending[0] !== 1) throw Error('Unexpected authentication proof response');
            if (pending[1]) throw rejectCode(pending[1]);
            if (pending.length < 32) return;
            io().set(pending.subarray(0, 32));
            const result = e.bw_auth_verify(32);
            if (result) throw Error('Server authentication proof did not verify');
            if (diagnosticWardenAudit && e.bw_warden_audit(1))
              throw Error('Cannot enable the local Warden diagnostic mode');
            pending = pending.slice(32);
            stage = 'realms';
            transport.send(new Uint8Array([16, 0, 0, 0, 0]));
          }
          if (stage === 'realms') {
            const realms = readRealms(pending);
            if (realms) receiveRealms(realms);
          }
        } catch (error) {
          terminate(error);
        }
      },
    })
      .then((connection) => {
        transport = connection;
        if (closed) {
          connection.close();
          return;
        }
        connection.send(request);
        request.fill(0);
      })
      .catch(terminate);
  });
}
