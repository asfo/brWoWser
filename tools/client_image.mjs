import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
export async function readCompat() {
  const b = await readFile(new URL('../Wow.exe', import.meta.url));
  const pe = b.readUInt32LE(60),
    optional = pe + 24;
  const base = b.readUInt32LE(optional + 28);
  const sections = optional + b.readUInt16LE(pe + 20);
  const offset = (address) => {
    for (let i = 0; i < b.readUInt16LE(pe + 6); i++) {
      const p = sections + i * 40,
        rva = b.readUInt32LE(p + 12),
        size = b.readUInt32LE(p + 16);
      if (address - base >= rva && address - base < rva + size)
        return b.readUInt32LE(p + 20) + address - base - rva;
    }
    throw Error('Unsupported client compatibility table');
  };
  const at = offset(b.readUInt32LE(offset(0xaf57f0))),
    end = b.indexOf(0, at);
  if (end < at || end - at > 65536) throw Error('Invalid client compatibility source');
  const bytes = b.subarray(at, end);
  if (
    createHash('sha256').update(bytes).digest('hex') !==
    '4556b7e3b8dccc9f7fa5ffe6836236726ca66cfac09b5062396e8e23c0eb3824'
  )
    throw Error('Unsupported WotLK client compatibility source');
  return bytes;
}
