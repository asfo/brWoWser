// Build 12340 world-entry services. No credentials or object-update payloads
// are exposed to the rendering thread.
export const WORLD = Object.freeze({
  login: 0x3d,
  loginFailed: 0x41,
  gameTime: 0x42,
  verify: 0x236,
  cinematic: 0xfa,
  cinematicBegin: 0xfb,
  cinematicEnd: 0xfc,
  activeMover: 0x26a,
  timeSync: 0x390,
  timeSyncResponse: 0x391,
  update: 0xa9,
  compressedUpdate: 0x1f6,
});

export function characterGuid(guid) {
  if (
    !Array.isArray(guid) ||
    guid.length !== 2 ||
    !guid.every((n) => Number.isInteger(n) && n >= 0 && n <= 0xffffffff)
  )
    throw Error('Invalid player identifier');
  const bytes = new Uint8Array(8),
    view = new DataView(bytes.buffer);
  guid.forEach((n, i) => view.setUint32(i * 4, n, true));
  return bytes;
}

export function worldLocation(bytes) {
  if (bytes.length !== 20) throw Error('Invalid world entry location');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const position = Array.from({ length: 4 }, (_, i) => view.getFloat32(4 + i * 4, true));
  if (
    !position.every(Number.isFinite) ||
    position.slice(0, 2).some((v) => Math.abs(v) > 18000) ||
    Math.abs(position[2]) > 100000
  )
    throw Error('World entry position exceeds supported bounds');
  return { map: view.getUint32(0, true), position };
}

export function movementPacket(guid, position, flags, milliseconds) {
  if (
    !Array.isArray(position) ||
    position.length !== 4 ||
    !position.every(Number.isFinite) ||
    !Number.isInteger(flags) ||
    flags < 0 ||
    flags > 0x3f
  )
    throw Error('Invalid ground movement');
  const source = characterGuid(guid),
    packed = [];
  let mask = 0;
  source.forEach((value, i) => {
    if (value) {
      mask |= 1 << i;
      packed.push(value);
    }
  });
  const bytes = new Uint8Array(1 + packed.length + 30),
    view = new DataView(bytes.buffer);
  bytes[0] = mask;
  bytes.set(packed, 1);
  const start = 1 + packed.length;
  view.setUint32(start, flags, true);
  view.setUint32(start + 6, milliseconds >>> 0, true);
  position.forEach((v, i) => view.setFloat32(start + 10 + i * 4, v, true));
  // flags2 and fallTime remain zero: swimming, jumping and transport movement
  // require their additional protocol fields before they can be admitted.
  return bytes;
}
