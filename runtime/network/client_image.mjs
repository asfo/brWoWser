// Load the owner's executable into a separate, live guest address space.
// This does not execute its entry point or claim a complete Windows process.
export async function loadClientImage(e, descriptor, signal) {
  if (
    descriptor.url !== '/private-client-image.bin' ||
    !Number.isInteger(descriptor.bytes) ||
    descriptor.bytes < 64 ||
    descriptor.bytes > 16 * 1024 * 1024 ||
    !/^[a-f0-9]{64}$/.test(descriptor.sha256)
  )
    throw Error('Invalid owner client image descriptor');
  const response = await fetch(descriptor.url, { signal, cache: 'no-store' });
  if (!response.ok) throw Error('Original local client image unavailable');
  const bytes = new Uint8Array(await response.arrayBuffer());
  try {
    if (bytes.length !== descriptor.bytes) throw Error('Original client image size changed');
    const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (x) =>
      x.toString(16).padStart(2, '0'),
    ).join('');
    if (digest !== descriptor.sha256) throw Error('Original client image identity changed');
    if (e.bw_warden_client_begin(bytes.length)) throw Error('Client guest-image allocation failed');
    for (let offset = 0; offset < bytes.length; offset += 65536) {
      signal?.throwIfAborted();
      const part = bytes.subarray(offset, offset + 65536);
      new Uint8Array(e.memory.buffer, e.bw_auth_io(), part.length).set(part);
      if (e.bw_warden_client_chunk(part.length)) throw Error('Client guest-image transfer failed');
    }
    const status = e.bw_warden_client_finish();
    if (status) throw Error('Original client PE mapping rejected (' + status + ')');
  } finally {
    bytes.fill(0);
  }
}
