import { playMovieMedia, cancelPendingMedia } from '../media/autoplay.mjs';
// Browser codec/output boundary. Playlist, fallback and screen transitions stay in original Lua.
export function createMoviePlayer({ video, resolve, finished, subtitles = () => {} }) {
  if (!video || typeof resolve !== 'function' || typeof finished !== 'function')
    throw Error('movie backend dependencies');
  let active = null,
    epoch = 0,
    disposed = false;
  function check() {
    if (disposed) throw Error('movie player disposed');
  }
  function release() {
    cancelPendingMedia(video);
    const old = active;
    active = null;
    if (old) {
      video.pause();
      video.removeAttribute('src');
      video.load();
      if (old.url) URL.revokeObjectURL(old.url);
    }
  }
  function stop() {
    check();
    epoch++;
    release();
  }
  async function start(request) {
    check();
    if (
      !Number.isSafeInteger(request.token) ||
      request.token < 1 ||
      typeof request.path !== 'string' ||
      !Number.isFinite(request.volume)
    )
      throw Error('invalid movie request');
    const generation = ++epoch;
    release();
    let resource;
    try {
      resource = await resolve(request.path);
    } catch (error) {
      if (disposed || generation !== epoch) return false;
      throw error;
    }
    if (disposed || generation !== epoch) return false;
    if (!(resource?.blob instanceof Blob) || resource.blob.size === 0)
      throw Error('invalid movie resource');
    const type = resource.type || resource.blob.type;
    if (!video.canPlayType(type)) throw Error('unsupported browser movie codec: ' + type);
    // MovieVolumeList numbers are retained for the native contract; native-to-browser gain is unvalidated.
    const url = URL.createObjectURL(resource.blob);
    active = { token: request.token, url, generation };
    video.src = url;
    video.loop = false;
    video.playsInline = true;
    try {
      await playMovieMedia(video);
    } catch (error) {
      if (disposed || generation !== epoch) return false;
      if (active?.generation === generation) release();
      throw error;
    }
    if (generation !== epoch || disposed) return false;
    return true;
  }
  function ended() {
    if (!active || disposed) return;
    const token = active.token;
    epoch++;
    release();
    finished({ token, reason: 'ended' });
  }
  function failed() {
    if (!active || disposed) return;
    const token = active.token;
    epoch++;
    release();
    finished({ token, reason: 'error', error: 'browser movie decode failed' });
  }
  video.addEventListener('ended', ended);
  video.addEventListener('error', failed);
  return {
    start,
    stop,
    enableSubtitles(enabled) {
      check();
      subtitles(Boolean(enabled));
    },
    metrics() {
      return {
        active_token: active?.token ?? null,
        current_time: video.currentTime || 0,
        paused: video.paused,
        ready_state: video.readyState,
      };
    },
    dispose() {
      if (disposed) return;
      epoch++;
      release();
      video.removeEventListener('ended', ended);
      video.removeEventListener('error', failed);
      disposed = true;
    },
  };
}
