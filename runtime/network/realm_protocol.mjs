// 3.3.5 realm response framing, shared by the endpoint gate and client adapter.
export function readRealms(bytes) {
  if (bytes.length < 3) return null;
  if (bytes[0] !== 16) throw Error('Unexpected realm response');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const size = view.getUint16(1, true) + 3;
  if (size < 11 || size > 65538) throw Error('Invalid realm response size');
  if (bytes.length < size) return null;
  const count = view.getUint16(7, true),
    realms = [];
  if (count > 256) throw Error('Realm count exceeds limit');
  let offset = 9;
  function need(n) {
    if (offset + n > size - 2) throw Error('Truncated realm response');
  }
  function text() {
    const begin = offset;
    while (offset < size - 2 && bytes[offset]) offset++;
    if (offset >= size - 2 || offset - begin > 255) throw Error('Invalid realm string');
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes.subarray(begin, offset++));
  }
  for (let i = 0; i < count; i++) {
    need(3);
    const type = bytes[offset++],
      locked = bytes[offset++],
      flags = bytes[offset++];
    const name = text(),
      address = text();
    need(7);
    const population = view.getFloat32(offset, true);
    offset += 4;
    const characters = bytes[offset++],
      timezone = bytes[offset++],
      id = bytes[offset++];
    let major, minor, revision, build;
    if (flags & 4) {
      need(5);
      major = bytes[offset++];
      minor = bytes[offset++];
      revision = bytes[offset++];
      build = view.getUint16(offset, true);
      offset += 2;
    }
    if (!Number.isFinite(population)) throw Error('Invalid realm population');
    realms.push({
      name,
      address,
      type,
      locked: !!locked,
      flags,
      population,
      characters,
      timezone,
      id,
      major,
      minor,
      revision,
      build,
    });
  }
  if (offset !== size - 2) throw Error('Unexpected realm response tail');
  return realms;
}
