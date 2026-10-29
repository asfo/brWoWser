import { randomBytes } from 'node:crypto';
import { readRealms } from './realm_protocol.mjs';

export function isMachineHost(host) {
  const name = String(host || '')
    .replace(/^\[|\]$/g, '')
    .toLowerCase();
  return name === '127.0.0.1' || name === 'localhost' || name === '::1';
}

// The browser never supplies a TCP destination. Tickets come only from records
// received on the configured authentication server's TCP connection.
export function createWorldRouteGate() {
  const tickets = new Map();
  function revoke(owner) {
    for (const [ticket, row] of tickets) if (row.owner === owner) tickets.delete(ticket);
  }
  return {
    observe(owner, publish) {
      let pending = null;
      return {
        client(bytes) {
          if (bytes.length === 5 && bytes[0] === 16) pending = new Uint8Array();
        },
        server(bytes) {
          if (pending === null) return;
          if (pending.length + bytes.length > 65538) {
            pending = null;
            return;
          }
          const joined = new Uint8Array(pending.length + bytes.length);
          joined.set(pending);
          joined.set(bytes, pending.length);
          pending = joined;
          let realms;
          try {
            realms = readRealms(pending);
          } catch {
            pending = null;
            return;
          }
          if (!realms) return;
          pending = null;
          revoke(owner);
          const routes = [];
          for (const realm of realms) {
            const match = /^(?:\[([0-9a-fA-F:]+)\]|([a-zA-Z0-9.-]+)):(\d{1,5})$/.exec(
              realm.address,
            );
            if (!match) continue;
            const host = match[1] || match[2],
              port = Number(match[3]);
            if (port < 1 || port > 65535 || tickets.size >= 4096 || !isMachineHost(host)) continue;
            const ticket = randomBytes(24).toString('hex');
            tickets.set(ticket, { host, port, owner, expires: Date.now() + 90000 });
            routes.push({ id: realm.id, address: realm.address, ticket });
          }
          publish({ type: 'world_routes', routes });
        },
        close() {
          pending = null;
          revoke(owner);
        },
      };
    },
    consume(ticket) {
      const row = tickets.get(ticket);
      tickets.delete(ticket);
      if (!row || row.expires < Date.now() || row.owner.readyState !== 1) return null;
      return { host: row.host, port: row.port };
    },
  };
}
