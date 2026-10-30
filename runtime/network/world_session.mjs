import { connectAuthTransport } from './transport.mjs';
import { WORLD, characterGuid, worldLocation, movementPacket } from './world_entry.mjs';
import {
  encodeCharacterCreate,
  characterCreateResult,
  characterDeleteResult,
  createCharacterRoster,
} from './character_protocol.mjs';

const OPCODE = {
  challenge: 0x1ec,
  authSession: 0x1ed,
  authResponse: 0x1ee,
  charEnumRequest: 0x37,
  charEnum: 0x3b,
  charCreateRequest: 0x36,
  charCreate: 0x3a,
  charDeleteRequest: 0x38,
  charDelete: 0x3c,
  ping: 0x1dc,
  warden: 0x2e6,
  wardenResponse: 0x2e7,
};
const MAX_WORLD_PACKET = 1024 * 1024;
const MAX_WORLD_BUFFER = MAX_WORLD_PACKET + 65536;
// World transport and packet assembly only. C/WASM signs authentication,
// transforms header streams and decodes the character table.
export function connectWorldSession({
  e,
  realm,
  url,
  signal,
  onStatus = () => {},
  onClosed = () => {},
}) {
  return new Promise((resolve, reject) => {
    let transport,
      closed = false,
      ready = false,
      encrypted = false,
      authed = false,
      wardenActive = false,
      wardenInitialized = false,
      wardenModuleReady = false,
      deferredCharacters = null,
      closeError = null,
      earlyBytes = [],
      earlySize = 0,
      pending = new Uint8Array(),
      header = null,
      heartbeat,
      wardenTicker,
      wardenWorkStarted = 0,
      wardenLastTick = 0,
      sequence = 0;
    const startedAt = performance.now();
    const realmHost = /^(?:\[([^\]]+)\]|([^:]+)):\d{1,5}$/.exec(realm?.address || '');
    const realmMachine = (realmHost ? realmHost[1] || realmHost[2] : '').toLowerCase();
    e.bw_warden_local(
      realmMachine === '127.0.0.1' || realmMachine === 'localhost' || realmMachine === '::1'
        ? 1
        : 0,
    );
    let phase = 'transport',
      packetCount = 0,
      lastOpcode = null,
      lastReceivedAt = startedAt;
    function status(update) {
      phase = update.phase;
      onStatus({ ...update, elapsed_ms: Math.round(performance.now() - startedAt) });
    }
    let roster,
      rosterRequest = null,
      creationRequest = null,
      deletionRequest = null,
      deletionUnconfirmed = false,
      entryRequest = null,
      worldEntry = null;
    let timer = setTimeout(() => close(Error('World authentication timeout')), 20000);
    const io = () => new Uint8Array(e.memory.buffer, e.bw_auth_io(), 65536);
    function close(error = Error('World connection closed')) {
      if (closed) return;
      closed = true;
      error.message += ` (phase ${phase}, elapsed ${Math.round(performance.now() - startedAt)}ms, packets ${packetCount}, last opcode ${lastOpcode === null ? 'none' : '0x' + lastOpcode.toString(16)})`;
      if (error.transport)
        error.message += ` (sent ${error.transport.sentBytes}, received ${error.transport.receivedBytes} bytes, receive idle ${Math.round(performance.now() - lastReceivedAt)}ms)`;
      closeError = error;
      clearTimeout(timer);
      clearInterval(heartbeat);
      clearInterval(wardenTicker);
      for (const job of [rosterRequest, creationRequest, deletionRequest, entryRequest]) {
        if (!job) continue;
        clearTimeout(job.timer);
        job.reject(error);
      }
      rosterRequest = creationRequest = deletionRequest = null;
      entryRequest = null;
      worldEntry = null;
      signal?.removeEventListener('abort', abort);
      transport?.close();
      e.bw_world_clear();
      pending.fill(0);
      pending = new Uint8Array();
      header?.fill(0);
      header = null;
      deferredCharacters = null;
      for (const bytes of earlyBytes) bytes.fill(0);
      earlyBytes = [];
      earlySize = 0;
      if (!ready) reject(error);
      onClosed(error);
    }
    const abort = () => close(Error('World connection cancelled'));
    function send(opcode, body = new Uint8Array(), plain = false) {
      const packet = new Uint8Array(6 + body.length),
        view = new DataView(packet.buffer);
      if (body.length > 10235) throw Error('World client packet exceeds limit');
      view.setUint16(0, body.length + 4);
      view.setUint32(2, opcode, true);
      packet.set(body, 6);
      if (!plain) {
        io().set(packet.subarray(0, 6));
        if (e.bw_world_encrypt(6)) throw Error('World header encryption failed');
        packet.set(io().subarray(0, 6));
        io().fill(0, 0, 6);
      }
      transport.send(packet);
    }
    function characters() {
      const rows = [];
      const decoder = new TextDecoder('utf-8', { fatal: true });
      for (let i = 0; i < e.bw_world_character_count(); i++) {
        const bytes = new Uint8Array(e.memory.buffer, e.bw_world_character_name(i));
        const end = bytes.indexOf(0);
        if (end < 0 || end > 48) throw Error('Invalid character name');
        const n = (field) => e.bw_world_character_number(i, field);
        rows.push({
          guid: [n(0), n(1)],
          name: decoder.decode(bytes.subarray(0, end)),
          race: n(2),
          class: n(3),
          gender: n(4),
          skin: n(5),
          face: n(6),
          hairStyle: n(7),
          hairColor: n(8),
          facialHair: n(9),
          level: n(10),
          zone: n(11),
          map: n(12),
          position: [n(13), n(14), n(15)],
          guild: n(16),
          flags: n(17),
          customization: n(18),
          firstLogin: !!n(19),
          pet: { display: n(20), level: n(21), family: n(22) },
          equipment: Array.from({ length: 23 }, (_, slot) => ({
            display: n(23 + slot * 3),
            inventoryType: n(24 + slot * 3),
            enchant: n(25 + slot * 3),
          })),
        });
      }
      return rows;
    }
    function finishCharacters(list) {
      clearTimeout(timer);
      if (ready) {
        roster.replace(list);
        if (rosterRequest) {
          const job = rosterRequest;
          rosterRequest = null;
          clearTimeout(job.timer);
          job.resolve(roster.rows);
        }
        return;
      }
      roster = createCharacterRoster(list);
      ready = true;
      status({ phase: 'ready', realm: realm.name });
      resolve({
        authenticated: true,
        realm: realm.name,
        get characters() {
          return roster.rows;
        },
        get selectedCharacter() {
          return roster.selected;
        },
        selectCharacter(guid) {
          if (closed) throw closeError;
          if (worldEntry) throw Error('A character is already entering the world');
          return roster.select(guid);
        },
        get worldEntry() {
          return worldEntry;
        },
        enterWorld() {
          if (closed) return Promise.reject(closeError);
          if (deletionUnconfirmed)
            return Promise.reject(
              Error('A character deletion is unconfirmed. Please reconnect before continuing.'),
            );
          if (entryRequest) return entryRequest.promise;
          if (worldEntry || creationRequest || deletionRequest || rosterRequest || !roster.selected)
            return Promise.reject(Error('Character is not ready to enter the world'));
          const job = {};
          job.promise = new Promise((resolve, reject) => Object.assign(job, { resolve, reject }));
          entryRequest = job;
          worldEntry = {
            phase: 'entering',
            character: roster.selected,
            cinematic: 0,
            updates: 0,
            entryStarted: performance.now(),
          };
          job.timer = setTimeout(() => close(Error('World entry response timeout')), 30000);
          try {
            send(WORLD.login, characterGuid(roster.selected.guid));
          } catch (error) {
            close(error);
          }
          return job.promise;
        },
        worldAction(action) {
          if (closed) throw closeError;
          if (!worldEntry?.location) throw Error('World entry is not ready');
          if (action.operation === 'resource-error') {
            worldEntry = {
              ...worldEntry,
              phase: 'load-error',
              error: String(action.message || 'World resources could not be loaded').slice(0, 512),
            };
          } else if (action.operation === 'retry') {
            if (worldEntry.phase !== 'load-error') return;
            worldEntry = {
              ...worldEntry,
              phase: 'loading',
              error: null,
              resourceRevision: (worldEntry.resourceRevision || 0) + 1,
            };
          } else if (action.operation === 'ready') {
            if (worldEntry.phase !== 'loading') return;
            send(WORLD.activeMover, characterGuid(worldEntry.character.guid));
            worldEntry = { ...worldEntry, phase: 'world' };
          } else if (action.operation === 'cinematic-begin') {
            if (!worldEntry.cinematic || worldEntry.cinematicStarted) return;
            send(WORLD.cinematicBegin);
            worldEntry = { ...worldEntry, cinematicStarted: true };
          } else if (action.operation === 'cinematic-end') {
            if (!worldEntry.cinematic) return;
            send(WORLD.cinematicEnd);
            worldEntry = { ...worldEntry, cinematic: 0, cinematicStarted: false };
          } else if (action.operation === 'movement') {
            if (worldEntry.phase !== 'world' || worldEntry.cinematic) return;
            const allowed = [0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbe, 0xee];
            if (!allowed.includes(action.opcode)) throw Error('Unsupported movement operation');
            send(
              action.opcode,
              movementPacket(worldEntry.character.guid, action.position, action.flags, action.time),
            );
          } else throw Error('Unsupported world action');
        },
        refreshCharacters() {
          if (closed) return Promise.reject(closeError);
          if (creationRequest || deletionRequest || worldEntry)
            return Promise.reject(Error('Character creation is already pending'));
          if (rosterRequest) return rosterRequest.promise;
          const job = {};
          job.promise = new Promise((resolve, reject) => Object.assign(job, { resolve, reject }));
          rosterRequest = job;
          job.timer = setTimeout(() => close(Error('Character list response timeout')), 20000);
          try {
            send(OPCODE.charEnumRequest);
          } catch (error) {
            close(error);
          }
          return job.promise;
        },
        createCharacter(character) {
          if (closed) return Promise.reject(closeError);
          if (deletionUnconfirmed)
            return Promise.reject(
              Error('A character deletion is unconfirmed. Please reconnect before continuing.'),
            );
          if (creationRequest || deletionRequest || rosterRequest || worldEntry)
            return Promise.reject(Error('A character operation is already pending'));
          let bytes;
          try {
            bytes = encodeCharacterCreate(character);
          } catch (error) {
            return Promise.reject(error);
          }
          const job = {};
          job.promise = new Promise((resolve, reject) => Object.assign(job, { resolve, reject }));
          creationRequest = job;
          // A timeout leaves the server outcome unknown. Close instead of retrying a mutation.
          job.timer = setTimeout(() => close(Error('Character creation response timeout')), 20000);
          try {
            send(OPCODE.charCreateRequest, bytes);
          } catch (error) {
            close(error);
          } finally {
            bytes.fill(0);
          }
          return job.promise;
        },
        deleteCharacter(guid) {
          if (closed) return Promise.reject(closeError);
          if (deletionUnconfirmed)
            return Promise.reject(
              Error('A character deletion is unconfirmed. Please reconnect before continuing.'),
            );
          if (worldEntry || rosterRequest || creationRequest || deletionRequest)
            return Promise.reject(Error('A character operation is already pending'));
          if (!roster.rows.some((row) => row.guid[0] === guid?.[0] && row.guid[1] === guid?.[1]))
            return Promise.reject(Error('Character is not in this roster'));
          const job = {};
          job.promise = new Promise((resolve, reject) => Object.assign(job, { resolve, reject }));
          deletionRequest = job;
          // An uncertain mutation is never retried automatically.
          job.timer = setTimeout(() => {
            if (deletionRequest !== job) return;
            deletionRequest = null;
            deletionUnconfirmed = true;
            // Preserve the connection for read-only roster reconciliation, but
            // require a fresh session before another mutation or world entry.
            job.resolve({ success: false, uncertain: true });
          }, 20000);
          try {
            send(OPCODE.charDeleteRequest, characterGuid(guid));
          } catch (error) {
            close(error);
          }
          return job.promise;
        },
        get closed() {
          return closed;
        },
        get error() {
          return closeError;
        },
        close: () => close(),
      });
    }
    function stopWardenWork() {
      clearInterval(wardenTicker);
      wardenTicker = null;
      wardenWorkStarted = wardenLastTick = 0;
    }
    function handleWardenExecution(result) {
      if (result === 7) stopWardenWork();
      if (result === 7) {
        const length = e.bw_warden_reply_length();
        if (!Number.isInteger(length) || length < 1 || length > 4096)
          throw Error('Invalid signed Warden module response length');
        const response = io().slice(0, length);
        io().fill(0);
        send(OPCODE.wardenResponse, response);
        response.fill(0);
        return true;
      }

      if (result === 8 || result === 121) {
        if (!wardenTicker) {
          wardenWorkStarted = wardenLastTick = performance.now();
          wardenTicker = setInterval(() => {
            if (closed) return;
            try {
              if (performance.now() - wardenWorkStarted > 20000)
                throw Error('Signed Warden request execution timeout');
              const now = performance.now();
              const elapsed = Math.max(0, Math.min(60000, Math.round(now - wardenLastTick)));
              wardenLastTick = now;
              const status = e.bw_warden_tick(elapsed);
              if (!status) stopWardenWork();
              else if (!handleWardenExecution(status))
                throw Error('Unexpected signed Warden update result');
            } catch (error) {
              close(error);
            }
          }, 50);
        }
        return true;
      }
      if (result === 120)
        throw Error(
          `Warden diagnostic completed signed request execution (${e.bw_warden_progress(30)} instructions, ${e.bw_warden_progress(33)} response bytes, ${e.bw_warden_progress(42)} page queries, ${e.bw_warden_progress(43)} process reads); no check response was transmitted.`,
        );
      if (result === 119) {
        const labelLength = e.bw_warden_dependency();
        const dependencyLabel =
          labelLength > 0 && labelLength < 128
            ? new TextDecoder().decode(io().slice(0, labelLength))
            : '';
        io().fill(0);
        const value = (field) => e.bw_warden_progress(28 + field) >>> 0;
        const reasons = {
          2: 'unavailable Windows import',
          3: 'unavailable client callback',
          4: 'unmapped guest memory or protection violation',
          5: 'instruction budget exceeded',
          6: 'unsupported x86 instruction or interrupt',
        };
        const memoryReason = e.bw_warden_progress(39);
        const memoryDetail = memoryReason
          ? ` Guest memory fault ${memoryReason} at 0x${e.bw_warden_progress(36).toString(16)}, length ${e.bw_warden_progress(37)}, access ${e.bw_warden_progress(38)}.`
          : '';
        throw Error(
          'Signed Warden module execution failed: ' +
            (reasons[value(0)] || 'invalid native request or response') +
            ` (execution ${value(0)}, dependency ${dependencyLabel || value(1)}, instructions ${value(2)}, opcode 0x${value(3).toString(16)}, response bytes ${value(5)}, request bytes ${value(6)}, pc 0x${e.bw_warden_progress(46).toString(16)}, saved pc 0x${e.bw_warden_progress(47).toString(16)}, page queries ${e.bw_warden_progress(42)}, process reads ${e.bw_warden_progress(43)}).${memoryDetail} No check result was sent.`,
        );
      }

      return false;
    }
    function packet(opcode, body) {
      if (opcode === WORLD.gameTime) {
        if (!worldEntry || body.length !== 12) throw Error('Invalid world game-time packet');
        const view = new DataView(body.buffer, body.byteOffset, body.length),
          packed = view.getUint32(0, true);
        worldEntry = {
          ...worldEntry,
          gameTime: ((packed >>> 6) & 31) * 60 + (packed & 63),
          timeReceived: performance.timeOrigin + performance.now(),
        };
        return;
      }
      if (opcode === WORLD.timeSync) {
        if (!authed || body.length !== 4) throw Error('Invalid world time synchronization');
        const reply = new Uint8Array(8);
        reply.set(body);
        new DataView(reply.buffer).setUint32(4, Math.floor(performance.now()) >>> 0, true);
        send(WORLD.timeSyncResponse, reply);
        return;
      }
      if (opcode === WORLD.verify) {
        if (!entryRequest || !worldEntry) throw Error('Unexpected world entry location');
        const location = worldLocation(body);
        worldEntry = {
          ...worldEntry,
          phase: 'loading',
          location,
          networkMs: Math.round(performance.now() - worldEntry.entryStarted),
        };
        const job = entryRequest;
        entryRequest = null;
        clearTimeout(job.timer);
        status({ phase: 'loading-world', realm: realm.name });
        job.resolve(location);
        return;
      }
      if (opcode === WORLD.loginFailed) {
        if (!entryRequest || body.length !== 1) throw Error('Unexpected player login failure');
        const job = entryRequest;
        entryRequest = null;
        worldEntry = null;
        clearTimeout(job.timer);
        job.reject(Error(`Player login rejected (${body[0]})`));
        return;
      }
      if (opcode === WORLD.cinematic) {
        if (!worldEntry || body.length !== 4) throw Error('Invalid world cinematic trigger');
        worldEntry = {
          ...worldEntry,
          cinematic: new DataView(body.buffer, body.byteOffset, 4).getUint32(0, true),
        };
        return;
      }
      if (opcode === WORLD.update || opcode === WORLD.compressedUpdate) {
        if (worldEntry) worldEntry = { ...worldEntry, updates: worldEntry.updates + 1 };
        return;
      }
      if (opcode === OPCODE.challenge) {
        if (encrypted) throw Error('Repeated world challenge');
        if (body.length !== 4 && body.length < 8) throw Error('Truncated world challenge');
        io().set(body.subarray(body.length === 4 ? 0 : 4, body.length === 4 ? 4 : 8));
        io().set(crypto.getRandomValues(new Uint8Array(4)), 4);
        const length = e.bw_world_auth(realm.id);
        if (!length) throw Error('World session key unavailable');
        const request = io().slice(0, length);
        io().fill(0);
        send(OPCODE.authSession, request, true);
        request.fill(0);
        encrypted = true;
        status({ phase: 'authenticating', realm: realm.name });
        return;
      }
      if (opcode === OPCODE.authResponse) {
        if (!encrypted || !body.length) throw Error('Invalid world authentication response');
        if (body[0] === 27) {
          if (!wardenActive) clearTimeout(timer);
          const at = body.length >= 16 ? 11 : 1;
          const position =
            body.length >= at + 4
              ? new DataView(body.buffer, body.byteOffset, body.byteLength).getUint32(at, true)
              : 0;
          status({ phase: 'queue', position, realm: realm.name });
          return;
        }
        if (body[0] !== 12) throw Error(`World authentication rejected (${body[0]})`);
        if (authed) return;
        authed = true;
        if (!wardenActive) {
          clearTimeout(timer);
          timer = setTimeout(() => close(Error('Character list response timeout')), 20000);
        }
        status({ phase: 'characters', realm: realm.name });
        send(0x4ff);
        send(OPCODE.charEnumRequest);
        // AzerothCore treats intervals below 27 seconds as over-speed pings.
        heartbeat = setInterval(() => {
          try {
            const ping = new Uint8Array(8);
            new DataView(ping.buffer).setUint32(0, ++sequence, true);
            send(OPCODE.ping, ping);
          } catch (error) {
            close(error);
          }
        }, 30000);
        return;
      }
      if (opcode === OPCODE.charEnum) {
        if (!authed) throw Error('Character list arrived before world authentication');
        if (body.length > 65536) throw Error('Character list exceeds limit');
        io().set(body);
        if (e.bw_world_characters(body.length)) throw Error('Unsupported character list layout');
        const list = characters();
        if (wardenActive) deferredCharacters = list;
        else finishCharacters(list);
        return;
      }
      if (opcode === OPCODE.charCreate) {
        if (!authed || !ready || !creationRequest)
          throw Error('Unexpected character creation response');
        const result = characterCreateResult(body);
        const job = creationRequest;
        creationRequest = null;
        clearTimeout(job.timer);
        job.resolve(result);
        return;
      }
      if (opcode === OPCODE.charDelete) {
        if (authed && ready && deletionUnconfirmed && !deletionRequest) {
          characterDeleteResult(body); // Validate a late reply; never apply it to a new request.
          return;
        }
        if (!authed || !ready || !deletionRequest)
          throw Error('Unexpected character deletion response');
        const result = characterDeleteResult(body),
          job = deletionRequest;
        deletionRequest = null;
        clearTimeout(job.timer);
        job.resolve(result);
        return;
      }
      if (opcode === OPCODE.warden) {
        // Periodic requests do not restart the module-initialization barrier.
        wardenActive = !wardenInitialized;
        if (body.length > 65536) throw Error('Warden packet exceeds limit');
        io().set(body);
        const result = e.bw_warden_receive(body.length);
        if (handleWardenExecution(result)) return;
        if (result === 1) {
          const response = io().slice(0, 1);
          io().fill(0);
          send(OPCODE.wardenResponse, response);
          response.fill(0);
          clearTimeout(timer);
          timer = setTimeout(() => close(Error('Warden module transfer timeout')), 30000);
        }
        if (result === 1 || result === 2) {
          status({
            phase: 'warden-transfer',
            received: e.bw_warden_progress(0),
            total: e.bw_warden_progress(1),
            realm: realm.name,
          });
          return;
        }
        if (result === 4 || result === 5) {
          const length = result === 4 ? 1 : 21;
          const response = io().slice(0, length);
          io().fill(0);
          send(OPCODE.wardenResponse, response);
          response.fill(0);
          wardenModuleReady = true;
          clearTimeout(timer);
          timer = setTimeout(
            () =>
              close(
                Error(
                  result === 4
                    ? 'Warden server challenge timeout'
                    : 'Warden initialization timeout',
                ),
              ),
            20000,
          );
          status({
            phase: result === 4 ? 'warden-module-ready' : 'warden-hash-answered',
            realm: realm.name,
          });
          return;
        }
        if (result === 110 || result === 117) {
          const mask = e.bw_warden_progress(35);
          const names = [
            'timing',
            'native process memory',
            'memory pages',
            'archive file',
            'Lua',
            'drivers',
            'loaded modules',
            'native procedure',
            `unknown type ${e.bw_warden_progress(13)} (request bytes ${e.bw_warden_progress(24)}, strings ${e.bw_warden_progress(25)}, check ${e.bw_warden_progress(26)}, offset ${e.bw_warden_progress(27)})`,
            'direct memory',
          ];
          const requested = names.filter((_, index) => index > 0 && mask & (1 << index));
          const memoryReads = e.bw_warden_progress(41);
          const readDetail = memoryReads ? ` Guest memory preflight reads: ${memoryReads}.` : '';
          const memoryReasons = [
            'none',
            'invalid range',
            'address overflow',
            'unmapped',
            'protected',
          ];
          const memoryReason = e.bw_warden_progress(39);
          const memoryDetail = memoryReason
            ? ` Memory provider: ${memoryReasons[memoryReason] || 'unavailable'} address 0x${e.bw_warden_progress(36).toString(16)}, length ${e.bw_warden_progress(37)}, module index ${e.bw_warden_progress(40)}.`
            : '';
          throw Error(
            'Warden requests unsupported runtime checks: ' +
              (requested.join(', ') || 'unrecognized request') +
              '.' +
              memoryDetail +
              readDetail +
              ' No check result was sent.',
          );
        }
        if (result === 6) {
          if (e.bw_warden_progress(11) === 7) {
            wardenActive = false;
            wardenInitialized = true;
            clearTimeout(timer);
            if (!ready && !deferredCharacters)
              timer = setTimeout(() => close(Error('Character list response timeout')), 20000);
            status({ phase: 'warden-initialized', realm: realm.name });
            if (deferredCharacters) {
              finishCharacters(deferredCharacters);
              deferredCharacters = null;
            }
          }
          return;
        }
        if (result === 3) {
          const length = e.bw_warden_dependency();
          const dependency = new TextDecoder().decode(io().subarray(0, length));
          io().fill(0);
          const reasons = {
            1: e.bw_warden_progress(9)
              ? 'Module initialization and key generation completed; packet integration is incomplete'
              : 'Module entry returned; client callback integration is incomplete',
            2: 'Required Windows API is not implemented: ' + dependency,
            3: 'Required client service is not implemented: ' + dependency,
            4: 'Guest memory or IO access is outside supported regions',
            5: 'Guest execution exceeded its instruction or memory-access budget',
            6:
              'Unsupported guest instruction/exception (opcode 0x' +
              e.bw_warden_progress(14).toString(16) +
              ', vector ' +
              (e.bw_warden_progress(15) >>> 0) +
              ')',
          };
          throw Error(
            `Warden module received (${e.bw_warden_progress(1)} bytes); transfer checksum verified. ` +
              `Container unpacked (${e.bw_warden_progress(3)} bytes). ` +
              'Original-key RSA signature verified. ' +
              `Image prepared (${e.bw_warden_progress(4)} bytes, ${e.bw_warden_progress(5)} relocations, ${e.bw_warden_progress(6)} imports). ` +
              `x86 execution stopped after ${e.bw_warden_progress(8)} instructions. ` +
              (reasons[e.bw_warden_progress(7)] || 'Unsupported x86 execution result') +
              (wardenModuleReady
                ? '. No check result was sent.'
                : '. No module-success or check result was sent.'),
          );
        }
        if (result === 115) {
          const detail = (index) => e.bw_warden_progress(16 + index) >>> 0;
          const reason = detail(6) === 3 ? 'empty callback address' : 'unrecognized record layout';
          throw Error(
            'Unsupported Warden initialization callback descriptor: record ' +
              detail(0) +
              ', size ' +
              detail(1) +
              ', fields ' +
              [detail(2), detail(3), detail(4), detail(5)].join('/') +
              ', reason: ' +
              reason +
              '.',
          );
        }
        if (result === 116) {
          const reasons = [
            'unknown parser failure',
            'request length outside supported bounds',
            'string table length exceeds request bounds',
            'missing string table terminator',
            'check count exceeds supported bounds',
            'check parameters exceed request bounds',
            'string reference outside string table',
            'invalid memory check parameters',
            'empty or incomplete check list',
            'invalid page-check parameters',
          ];
          const value = (field) => e.bw_warden_progress(field) >>> 0;
          throw Error(
            'Warden request parsing failed: ' +
              (reasons[value(26)] || 'unknown parser failure') +
              ` (request bytes ${value(24)}, strings ${value(25)}, offset ${value(27)}). No check result was sent.`,
          );
        }
        const errors = {
          100: 'Invalid Warden packet',
          101: 'Warden module size exceeds the supported limit',
          102: 'Warden module transfer checksum mismatch',
          103: 'Invalid Warden module fragment',
          105: 'Unsupported Warden module container',
          106: 'Invalid or oversized Warden compressed image',
          107: 'Warden module signature does not match the original client signing key',
          108: 'Unsupported or invalid Warden native image layout',
          109: 'Warden module proof completed; initialization callbacks are not implemented yet',
          110: 'Warden module proof completed; runtime client checks are not implemented yet',
          111: 'Unexpected command after the Warden module proof',
          112: 'Native Warden module could not produce its hash response',
          113: 'Native Warden module cipher handoff is unsupported or invalid',
          114: 'Invalid Warden initialization checksum or record length',
          115: 'Unsupported Warden initialization callback descriptor',
          118: 'Warden monotonic timing source is unavailable',
          104: 'Unexpected Warden command or transfer state',
        };
        throw Error(errors[result] || 'Unsupported Warden transfer result');
      }
      // Known-world services may send addon/account/cache metadata before enumeration.
      // Their payloads are not logged and do not imply implemented game behavior.
    }
    function decrypt(bytes) {
      if (!encrypted) return bytes;
      io().set(bytes);
      if (e.bw_world_decrypt(bytes.length)) throw Error('World header decryption failed');
      return io().slice(0, bytes.length);
    }
    function consume(chunk) {
      if (closed) return;
      lastReceivedAt = performance.now();
      if (pending.length + chunk.length > MAX_WORLD_BUFFER)
        throw Error('World receive buffer exceeds limit');
      const bytes = new Uint8Array(pending.length + chunk.length);
      bytes.set(pending);
      bytes.set(chunk, pending.length);
      pending = bytes;
      while (!closed) {
        if (!header) {
          if (!pending.length) return;
          header = decrypt(pending.slice(0, 1));
          pending = pending.slice(1);
        }
        if (header.length === 1) {
          const remaining = header[0] & 128 ? 4 : 3;
          if (pending.length < remaining) return;
          const complete = new Uint8Array(remaining + 1);
          complete.set(header);
          complete.set(decrypt(pending.slice(0, remaining)), 1);
          header = complete;
          pending = pending.slice(remaining);
        }
        const large = !!(header[0] & 128),
          size = large
            ? ((header[0] & 127) << 16) | (header[1] << 8) | header[2]
            : (header[0] << 8) | header[1];
        const opcode = large ? header[3] | (header[4] << 8) : header[2] | (header[3] << 8);
        if (size < 2)
          throw Error(
            `Invalid world packet size ${size} (opcode 0x${opcode.toString(16)}, header ${header.length})`,
          );
        if (size > MAX_WORLD_PACKET)
          throw Error(
            `World packet exceeds supported size ${size}/${MAX_WORLD_PACKET} (opcode 0x${opcode.toString(16)}, header ${header.length})`,
          );
        if (!opcode || opcode >= 0x521)
          throw Error(
            `World header out of sync (opcode 0x${opcode.toString(16)}, size ${size}, header ${header.length})`,
          );
        if (pending.length < size - 2) return;
        const body = pending.slice(0, size - 2);
        pending = pending.slice(size - 2);
        header = null;
        packetCount++;
        lastOpcode = opcode;
        try {
          packet(opcode, body);
        } finally {
          body.fill(0);
        }
      }
    }
    signal?.addEventListener('abort', abort, { once: true });
    if (signal?.aborted) {
      abort();
      return;
    }
    connectAuthTransport({
      url,
      signal,
      onError: close,
      onClose: () => close(),
      onBytes(bytes) {
        try {
          if (!transport) {
            if (closed) return;
            if (earlySize + bytes.length > 262144)
              throw Error('World receive buffer exceeds limit');
            earlyBytes.push(bytes.slice());
            earlySize += bytes.length;
          } else consume(bytes);
        } catch (error) {
          close(error);
        }
      },
    })
      .then((connection) => {
        transport = connection;
        if (closed) connection.close();
        else {
          status({ phase: 'transport-connected', realm: realm.name });
          try {
            for (const bytes of earlyBytes) {
              if (!closed) consume(bytes);
              bytes.fill(0);
            }
            earlyBytes = [];
            earlySize = 0;
          } catch (error) {
            close(error);
          }
        }
      })
      .catch(close);
  });
}
