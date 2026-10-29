// Immutable public source dependency, cached outside the source tree inventory.
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
export async function x86Sources() {
  const manifest = JSON.parse(
    await readFile(new URL('../runtime/network/x86-toolchain.json', import.meta.url)),
  );
  const files = {};
  for (const [name, expected] of Object.entries(manifest.files)) {
    const cached = new URL('../runtime/network/build/x86-toolchain/' + name, import.meta.url);
    let bytes;
    try {
      bytes = await readFile(cached);
    } catch {
      /* A missing dependency is fetched below. */
    }
    const checksum = (value) => createHash('sha256').update(value).digest('hex');
    if (!bytes || checksum(bytes) !== expected) {
      const response = await fetch(
        `https://raw.githubusercontent.com/wfeldt/libx86emu/${manifest.revision}/${name}`,
        { signal: AbortSignal.timeout(30000) },
      );
      if (!response.ok) throw Error('Cannot fetch pinned x86 source: ' + name);
      bytes = Buffer.from(await response.arrayBuffer());
      if (checksum(bytes) !== expected) throw Error('Pinned x86 source checksum mismatch: ' + name);
      await mkdir(new URL('.', cached), { recursive: true });
      await writeFile(cached, bytes);
    }
    files['x86/' + name] = bytes;
  }
  return files;
}
