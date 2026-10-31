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

export function movementPacket(guid, position, flags, milliseconds, fallTime = 0, jump) {
  if (
    !Array.isArray(position) ||
    position.length !== 4 ||
    !position.every(Number.isFinite) ||
    !Number.isInteger(flags) ||
    flags < 0 ||
    (flags & ~0x103f) !== 0 ||
    !Number.isInteger(milliseconds) ||
    milliseconds < 0 ||
    !Number.isInteger(fallTime) ||
    fallTime < 0 ||
    fallTime > 0xffffffff
  )
    throw Error('Invalid player movement');
  const source = characterGuid(guid),
    packed = [];
  let mask = 0;
  source.forEach((value, i) => {
    if (value) {
      mask |= 1 << i;
      packed.push(value);
    }
  });
  const falling = !!(flags & 0x1000);
  if (
    falling &&
    (!jump ||
      ![jump.verticalSpeed, jump.sinAngle, jump.cosAngle, jump.horizontalSpeed].every(
        Number.isFinite,
      ) ||
      jump.verticalSpeed < 0 ||
      jump.verticalSpeed > 50 ||
      Math.abs(jump.sinAngle) > 1.001 ||
      Math.abs(jump.cosAngle) > 1.001 ||
      jump.horizontalSpeed < 0 ||
      jump.horizontalSpeed > 50)
  )
    throw Error('Invalid jump trajectory');
  const bytes = new Uint8Array(1 + packed.length + 30 + (falling ? 16 : 0)),
    view = new DataView(bytes.buffer);
  bytes[0] = mask;
  bytes.set(packed, 1);
  const start = 1 + packed.length;
  view.setUint32(start, flags, true);
  view.setUint32(start + 6, milliseconds >>> 0, true);
  position.forEach((v, i) => view.setFloat32(start + 10 + i * 4, v, true));
  view.setUint32(start + 26, fallTime, true);
  if (falling)
    [jump.verticalSpeed, jump.sinAngle, jump.cosAngle, jump.horizontalSpeed].forEach((value, i) =>
      view.setFloat32(start + 30 + i * 4, value, true),
    );
  // flags2 stays zero: transport, swimming and flight are separate profiles.
  return bytes;
}
