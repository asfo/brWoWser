export async function readRawExtent(file, m, maxBytes = 16 * 1024 * 1024) {
  if (
    m.flags !== 0x80000000 ||
    !Number.isSafeInteger(m.logical_bytes) ||
    m.logical_bytes <= 0 ||
    m.logical_bytes > maxBytes ||
    m.stored_bytes !== m.logical_bytes ||
    !Number.isSafeInteger(m.physical_offset) ||
    m.physical_offset < 0 ||
    m.physical_offset > file.size - m.stored_bytes
  )
    throw Error('unsupported raw audio extent');
  const bytes = new Uint8Array(m.logical_bytes);
  let reads = 0;
  for (let at = 0; at < bytes.length; at += 1048576) {
    const n = Math.min(1048576, bytes.length - at),
      part = new Uint8Array(await file.read(m.physical_offset + at, n));
    if (part.length !== n) throw Error('short raw audio read');
    bytes.set(part, at);
    reads++;
  }
  return { bytes, reads };
}
