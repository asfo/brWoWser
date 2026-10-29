import { verifiedLocalMovie } from './local_asset.mjs';
const normalize = (path) => path.toLowerCase().replaceAll('\\', '/');
// Original Lua chooses the playlist. This provider only resolves its admitted
// owner-local filenames to privately prepared codec-compatible bytes.
export function createCinematicResolver(selected, movies, { initial, prepare, readCache }) {
  if (
    !(selected instanceof Map) ||
    !Array.isArray(movies) ||
    typeof prepare !== 'function' ||
    typeof readCache !== 'function'
  )
    throw Error('Invalid cinematic catalogue dependencies');
  const catalogue = new Map(),
    resolved = new Map();
  for (const movie of movies) {
    const key = normalize(movie.logical_path);
    if (catalogue.has(key)) throw Error('Ambiguous cinematic catalogue');
    catalogue.set(key, movie);
  }
  return async (path) => {
    const key = normalize(path),
      movie = catalogue.get(key);
    if (!movie) throw Error('Cinematic not admitted: ' + path);
    if (!resolved.has(key)) {
      const pending = (async () => {
        const source = selected.get(movie.source_path.toLowerCase());
        if (!source) throw Error('Missing original local cinematic: ' + movie.source_path);
        const manifest =
          initial && normalize(initial.logical_path) === key ? initial : await prepare(movie);
        if (
          normalize(manifest.logical_path) !== key ||
          manifest.source_path !== movie.source_path ||
          manifest.source_sha256 !== movie.source_sha256
        )
          throw Error('Cinematic cache source identity mismatch');
        return verifiedLocalMovie(source, manifest, () => readCache(manifest, movie));
      })().catch((error) => {
        resolved.delete(key);
        throw error;
      });
      resolved.set(key, pending);
    }
    return (await resolved.get(key))();
  };
}
