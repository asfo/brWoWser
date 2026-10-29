// Browser boundary for an owner-authorized source and a host-prepared local cache.
const sha = async (bytes) =>
  Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (v) =>
    v.toString(16).padStart(2, '0'),
  ).join('');
export async function verifiedLocalMovie(original, manifest, readCache) {
  if (!original || typeof original.arrayBuffer !== 'function')
    throw Error('Missing original local cinematic');
  if (
    typeof manifest?.source_sha256 !== 'string' ||
    typeof manifest.output_sha256 !== 'string' ||
    manifest.media_type !== 'video/webm; codecs="vp9, opus"' ||
    typeof readCache !== 'function'
  )
    throw Error('Invalid local cinematic manifest');
  if ((await sha(await original.arrayBuffer())) !== manifest.source_sha256)
    throw Error('Selected original cinematic differs from local cache source');
  let blob, pending;
  return async () => {
    if (blob) return { blob, type: manifest.media_type };
    if (!pending)
      pending = (async () => {
        const bytes = await readCache();
        if ((await sha(bytes)) !== manifest.output_sha256)
          throw Error('Local cinematic cache integrity mismatch');
        blob = new Blob([bytes], { type: manifest.media_type });
        return { blob, type: manifest.media_type };
      })().catch((error) => {
        pending = null;
        throw error;
      });
    return pending;
  };
}
