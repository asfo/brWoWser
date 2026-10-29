// Compare the C-owned draw packet without converting glyph coverage to JSON.
// Used only to reuse an unchanged GPU surface; it never changes layout/state.
export function sameUIPacket(a, b) {
  if (a === b) return true;
  if (a === null || b === null || typeof a !== 'object' || typeof b !== 'object') return false;
  if (ArrayBuffer.isView(a) || ArrayBuffer.isView(b)) {
    if (
      !ArrayBuffer.isView(a) ||
      !ArrayBuffer.isView(b) ||
      a.constructor !== b.constructor ||
      a.byteLength !== b.byteLength
    )
      return false;
    const x = new Uint8Array(a.buffer, a.byteOffset, a.byteLength),
      y = new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
    for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return false;
    return true;
  }
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  const keys = Object.keys(a);
  if (keys.length !== Object.keys(b).length) return false;
  for (const key of keys) if (!Object.hasOwn(b, key) || !sameUIPacket(a[key], b[key])) return false;
  return true;
}
