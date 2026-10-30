// Bounded owner-file read cache. Archive lookup/decompression remain in WASM;
// adjacent sector reads share one browser File/HTTP range operation.
export function createRangeCache(
  read,
  size,
  { blockSize = 65536, budget = 32 * 1024 * 1024 } = {},
) {
  const blocks = new Map();
  let resident = 0;
  function block(start) {
    if (blocks.has(start)) {
      const row = blocks.get(start);
      blocks.delete(start);
      blocks.set(start, row);
      return row.promise;
    }
    const length = Math.min(blockSize, size - start),
      row = { length };
    row.promise = Promise.resolve(read(start, length))
      .then((value) => {
        const bytes = value instanceof Uint8Array ? value : new Uint8Array(value);
        if (bytes.byteLength !== length) throw Error('Cached archive range length mismatch');
        return bytes;
      })
      .catch((error) => {
        if (blocks.get(start) === row) {
          blocks.delete(start);
          resident -= length;
        }
        throw error;
      });
    blocks.set(start, row);
    resident += length;
    while (resident > budget && blocks.size > 1) {
      const oldest = blocks.keys().next().value;
      resident -= blocks.get(oldest).length;
      blocks.delete(oldest);
    }
    return row.promise;
  }
  return async (offset, count) => {
    if (
      !Number.isSafeInteger(offset) ||
      !Number.isSafeInteger(count) ||
      offset < 0 ||
      count < 0 ||
      offset > size ||
      count > size - offset
    )
      throw Error('Cached archive range rejected');
    if (!count) return new Uint8Array();
    if (count > budget / 2) return read(offset, count);
    const out = new Uint8Array(count);
    for (let position = offset; position < offset + count;) {
      const start = Math.floor(position / blockSize) * blockSize,
        bytes = await block(start),
        local = position - start,
        length = Math.min(bytes.length - local, offset + count - position);
      out.set(bytes.subarray(local, local + length), position - offset);
      position += length;
    }
    return out;
  };
}
