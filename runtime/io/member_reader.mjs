// Bounded current-client MPQ member reads. Table lookup and zlib decoding stay
// in the existing C/WASM modules; large members are decoded sector by sector.
export async function openArchive({ read, size, lookupWasm, decoderWasm, onPhase = () => {} }) {
  const limit = 33554432;
  async function range(offset, count) {
    if (
      !Number.isSafeInteger(offset) ||
      !Number.isSafeInteger(count) ||
      offset < 0 ||
      count < 0 ||
      count > limit ||
      offset > size - count
    )
      throw Error('Archive range outside bounds');
    const bytes = new Uint8Array(await read(offset, count));
    if (bytes.length !== count) throw Error('Short archive read');
    return bytes;
  }
  const header = await range(0, 44),
    view = new DataView(header.buffer);
  const version = view.getUint16(12, true),
    headerSize = view.getUint32(4, true);
  const shift = view.getUint16(14, true),
    sectorSize = 512 << shift;
  if (
    view.getUint32(0, true) !== 0x1a51504d ||
    version > 1 ||
    headerSize !== (version ? 44 : 32) ||
    shift > 3
  )
    throw Error('Unsupported MPQ header');
  if (
    version &&
    (view.getBigUint64(32, true) !== 0n || view.getUint16(40, true) || view.getUint16(42, true))
  )
    throw Error('Unsupported MPQ high table offsets');
  const hashCount = view.getUint32(24, true),
    blockCount = view.getUint32(28, true);
  const hashOffset = view.getUint32(16, true),
    blockOffset = view.getUint32(20, true);
  if (
    !hashCount ||
    hashCount & (hashCount - 1) ||
    hashCount > 131072 ||
    !blockCount ||
    blockCount > 131072 ||
    hashOffset < headerSize ||
    blockOffset < headerSize
  )
    throw Error('Invalid MPQ table dimensions');
  const [hashes, blocks] = await Promise.all([
    range(hashOffset, hashCount * 16),
    range(blockOffset, blockCount * 16),
  ]);
  const e = (await WebAssembly.instantiate(lookupWasm, {})).instance.exports;
  new Uint8Array(e.memory.buffer, e.hash_ptr(), hashes.length).set(hashes);
  new Uint8Array(e.memory.buffer, e.block_ptr(), blocks.length).set(blocks);
  if (e.load_tables(hashCount, blockCount) !== 1) throw Error('MPQ table initialization failed');
  const d = (await WebAssembly.instantiate(decoderWasm, {})).instance.exports;
  function lookup(name, locale = 0, platform = 0) {
    if (!/^[\x01-\x7f]{1,255}$/.test(name)) throw Error('Unsupported MPQ member name');
    const bytes = new TextEncoder().encode(name);
    new Uint8Array(e.memory.buffer, e.name_ptr(), bytes.length).set(bytes);
    const status = e.lookup(bytes.length, locale, platform);
    if (!status) return null;
    if (status !== 1) throw Error('Invalid MPQ matching entry');
    const result = new DataView(e.memory.buffer, e.result_ptr(), 32),
      out = {};
    for (const [i, field] of [
      'physical_offset',
      'stored_bytes',
      'logical_bytes',
      'flags',
      'hash_slot',
      'block_index',
      'locale',
      'platform',
    ].entries())
      out[field] = result.getUint32(i * 4, true);
    if (out.physical_offset > size - out.stored_bytes) throw Error('MPQ member outside archive');
    return out;
  }
  function inflate(bytes, expected) {
    if (bytes.length === expected) return bytes;
    if (expected > 4096 || bytes.length < 2 || bytes.length > 8192 || bytes[0] !== 2)
      throw Error('Unsupported MPQ compression or sector size');
    new Uint8Array(d.memory.buffer, d.input_ptr(), bytes.length - 1).set(bytes.subarray(1));
    if (d.decode_zlib(bytes.length - 1, 4096, expected) || d.output_size() !== expected)
      throw Error('MPQ zlib sector rejected');
    return new Uint8Array(d.memory.buffer, d.output_ptr(), expected);
  }
  async function readMember(name) {
    const metadata = lookup(name);
    if (!metadata) return null;
    const { flags, logical_bytes: logical, stored_bytes: stored } = metadata;
    const allowed = 0x80000000 | 0x04000000 | 0x01000000 | 0x00000200;
    if (
      !(flags & 0x80000000) ||
      flags & ~allowed ||
      !logical ||
      logical > limit ||
      !stored ||
      stored > limit
    )
      throw Error('Unsupported MPQ member flags or capacity: ' + name);
    onPhase('read');
    const raw = await range(metadata.physical_offset, stored);
    onPhase('decompress');
    if (!(flags & 0x200)) {
      if (stored !== logical) throw Error('Invalid uncompressed MPQ member size');
      return { name, metadata, bytes: raw };
    }
    if (flags & 0x01000000) return { name, metadata, bytes: inflate(raw, logical).slice() };
    const sectors = Math.ceil(logical / sectorSize),
      offsetsCount = sectors + 1 + (flags & 0x04000000 ? 1 : 0);
    const tableBytes = offsetsCount * 4;
    if (sectors > 8192 || tableBytes > raw.length) throw Error('MPQ sector table capacity');
    const offsets = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
    let previous = offsets.getUint32(0, true);
    if (previous !== tableBytes) throw Error('Invalid MPQ sector table start');
    for (let i = 1; i < offsetsCount; i++) {
      const next = offsets.getUint32(i * 4, true);
      if (next < previous || next > stored) throw Error('Invalid MPQ sector offsets');
      previous = next;
    }
    if (previous !== stored) throw Error('Invalid MPQ member end');
    const bytes = new Uint8Array(logical);
    for (let i = 0; i < sectors; i++) {
      const start = offsets.getUint32(i * 4, true),
        end = offsets.getUint32((i + 1) * 4, true);
      const expected = Math.min(sectorSize, logical - i * sectorSize);
      bytes.set(inflate(raw.subarray(start, end), expected), i * sectorSize);
    }
    return { name, metadata, bytes };
  }
  return { lookup, readMember };
}
