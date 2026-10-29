// Owner-authorized local File, explicit localhost mount, or Node test file.
export async function readLocalSource(row, offset, count) {
  const size = row.size ?? row.file?.size;
  if (
    !Number.isSafeInteger(offset) ||
    !Number.isSafeInteger(count) ||
    offset < 0 ||
    count < 0 ||
    offset > size ||
    count > size - offset
  )
    throw Error('Local source range rejected');
  if (!count) return new ArrayBuffer(0);
  if (row.file) return row.file.slice(offset, offset + count).arrayBuffer();
  if (row.url) {
    const response = await fetch(row.url, {
      headers: { Range: `bytes=${offset}-${offset + count - 1}` },
    });
    if (
      response.status !== 206 ||
      response.headers.get('Content-Range') !== `bytes ${offset}-${offset + count - 1}/${size}`
    )
      throw Error('Local range response rejected');
    const bytes = await response.arrayBuffer();
    if (bytes.byteLength !== count) throw Error('Local range length mismatch');
    return bytes;
  }
  const file = await (await import('node:fs/promises')).open(row.diskPath, 'r');
  try {
    const bytes = new Uint8Array(count),
      result = await file.read(bytes, 0, count, offset);
    if (result.bytesRead !== count) throw Error('Local file short read');
    return bytes;
  } finally {
    await file.close();
  }
}
