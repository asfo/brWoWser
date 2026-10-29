const MAX_OUTPUT = 16 * 1024 * 1024,
  MAX_RANGE = 1024 * 1024;
const integer = (n) => Number.isSafeInteger(n) && n >= 0;
export async function readLargeSectorMember(file, m, decoderWasm) {
  if (
    m.flags !== 0x84000200 ||
    !integer(m.logical_bytes) ||
    !m.logical_bytes ||
    m.logical_bytes > MAX_OUTPUT ||
    !integer(m.stored_bytes) ||
    m.stored_bytes < 12 ||
    !integer(m.physical_offset) ||
    m.physical_offset > file.size - m.stored_bytes
  )
    throw Error('unsupported large sector member');
  let reads = 0;
  const range = async (offset, count) => {
    if (!integer(offset) || !integer(count) || count > MAX_RANGE || offset > file.size - count)
      throw Error('large member range outside bounds');
    const bytes = new Uint8Array(await file.read(offset, count));
    reads++;
    if (bytes.length !== count) throw Error('short large member read');
    return bytes;
  };
  const header = await range(0, 44),
    h = new DataView(header.buffer, header.byteOffset, header.byteLength),
    version = h.getUint16(12, true),
    shift = h.getUint16(14, true);
  if (
    h.getUint32(0, true) !== 0x1a51504d ||
    version > 1 ||
    h.getUint32(4, true) !== (version ? 44 : 32) ||
    shift > 3 ||
    m.physical_offset < (version ? 44 : 32)
  )
    throw Error('unsupported large member MPQ header');
  if (
    version &&
    (h.getBigUint64(32, true) !== 0n || h.getUint16(40, true) || h.getUint16(42, true))
  )
    throw Error('unsupported large member high offsets');
  const sectorSize = 512 << shift,
    count = Math.ceil(m.logical_bytes / sectorSize),
    tableBytes = (count + 2) * 4;
  if (count > 4096 || tableBytes > m.stored_bytes)
    throw Error('unsupported large sector dimensions');
  const table = await range(m.physical_offset, tableBytes),
    view = new DataView(table.buffer, table.byteOffset, table.byteLength),
    offsets = Array.from({ length: count + 2 }, (_, i) => view.getUint32(i * 4, true));
  if (
    offsets[0] !== tableBytes ||
    offsets.at(-1) !== m.stored_bytes ||
    offsets.some((n, i) => n > m.stored_bytes || (i && n < offsets[i - 1]))
  )
    throw Error('invalid large sector table');
  for (let i = 0; i < count; i++) {
    const expected = Math.min(sectorSize, m.logical_bytes - i * sectorSize),
      stored = offsets[i + 1] - offsets[i];
    if (stored <= 0 || stored > expected) throw Error('invalid large sector length');
  }
  const { instance } = await WebAssembly.instantiate(decoderWasm, {}),
    d = instance.exports,
    output = new Uint8Array(m.logical_bytes);
  let cache = new Uint8Array(),
    cacheAt = -1,
    zlibSectors = 0,
    storedSectors = 0;
  for (let i = 0; i < count; i++) {
    const begin = offsets[i],
      end = offsets[i + 1],
      expected = Math.min(sectorSize, m.logical_bytes - i * sectorSize);
    if (begin < cacheAt || end > cacheAt + cache.length) {
      cacheAt = begin;
      cache = await range(m.physical_offset + begin, Math.min(MAX_RANGE, offsets[count] - begin));
    }
    const sector = cache.subarray(begin - cacheAt, end - cacheAt);
    if (sector.length !== end - begin) throw Error('truncated grouped sector read');
    if (sector.length === expected) {
      output.set(sector, i * sectorSize);
      storedSectors++;
    } else {
      if (sector.length < 2 || sector[0] !== 2) throw Error('unsupported large sector compression');
      new Uint8Array(d.memory.buffer, d.input_ptr(), sector.length - 1).set(sector.subarray(1));
      if (d.decode_zlib(sector.length - 1, 4096, expected) !== 0 || d.output_size() !== expected)
        throw Error('large sector zlib rejected');
      output.set(new Uint8Array(d.memory.buffer, d.output_ptr(), expected), i * sectorSize);
      zlibSectors++;
    }
  }
  return {
    bytes: output,
    reads,
    sector_count: count,
    sector_size: sectorSize,
    stored_sectors: storedSectors,
    zlib_sectors: zlibSectors,
    crc_payload_bytes: offsets.at(-1) - offsets[count],
    crc_status: 'PRESENT_NOT_VERIFIED',
  };
}
