// 3.3.5a logon challenge wire format. No password or SRP proof is sent here.
export function buildLogonChallenge(username, { locale = 'enUS' } = {}) {
  if (typeof username !== 'string' || !/^[A-Za-z0-9_]{1,16}$/.test(username))
    throw Error('Expected an ASCII account name of 1 to 16 characters');
  if (!/^[a-z]{2}[A-Z]{2}$/.test(locale)) throw Error('Invalid client locale');
  const name = new TextEncoder().encode(username.toUpperCase());
  const bytes = new Uint8Array(34 + name.length);
  const view = new DataView(bytes.buffer);
  bytes[1] = 8;
  view.setUint16(2, bytes.length - 4, true);
  // Client identifiers are little-endian four-character integers on the wire.
  bytes.set([0x57, 0x6f, 0x57, 0], 4);
  bytes.set([3, 3, 5], 8);
  view.setUint16(11, 12340, true);
  bytes.set([0x36, 0x38, 0x78, 0], 13);
  bytes.set([0x6e, 0x69, 0x57, 0], 17);
  bytes.set(new TextEncoder().encode([...locale].reverse().join('')), 21);
  bytes[33] = name.length;
  bytes.set(name, 34);
  return bytes;
}

export function createChallengeReader() {
  let pending = new Uint8Array(),
    complete = false;
  return {
    push(chunk) {
      if (complete) throw Error('Challenge already complete');
      if (!(chunk instanceof Uint8Array)) throw TypeError('Expected protocol bytes');
      if (pending.length + chunk.length > 1024) throw Error('Challenge response exceeds limit');
      const joined = new Uint8Array(pending.length + chunk.length);
      joined.set(pending);
      joined.set(chunk, pending.length);
      pending = joined;
      if (pending.length < 3) return null;
      if (pending[0] !== 0 || pending[1] !== 0) throw Error('Unexpected logon challenge header');
      const result = pending[2];
      if (result !== 0) {
        complete = true;
        return { result, accepted: false };
      }
      if (pending.length < 36) return null;
      const generatorLength = pending[35];
      if (generatorLength < 1 || generatorLength > 32) throw Error('Invalid SRP generator length');
      const modulusLengthAt = 36 + generatorLength;
      if (pending.length <= modulusLengthAt) return null;
      const modulusLength = pending[modulusLengthAt];
      if (modulusLength !== 32) throw Error('Unsupported SRP modulus length');
      const flagsAt = modulusLengthAt + 1 + modulusLength + 32 + 16;
      if (pending.length <= flagsAt) return null;
      const securityFlags = pending[flagsAt];
      if (securityFlags & ~7) throw Error('Unsupported challenge security flags');
      const size =
        flagsAt +
        1 +
        (securityFlags & 1 ? 20 : 0) +
        (securityFlags & 2 ? 12 : 0) +
        (securityFlags & 4 ? 1 : 0);
      if (pending.length < size) return null;
      complete = true;
      return {
        result,
        accepted: true,
        securityFlags,
        generatorLength,
        modulusLength,
        bytes: pending.slice(0, size),
      };
    },
  };
}
