import { createSceneEffects } from '../../runtime/audio/scene_effects.mjs';
import { createMediaPlayer } from '../../runtime/audio/media_player.mjs';
import { createGlueAudioBridge } from '../../runtime/audio/glue_bridge.mjs';
export async function createLocalAudio(files, config, manifest) {
  const assets = [
      ...manifest.assets,
      ...(manifest.effects || []).map((row) => ({ ...row, channel: 2, effect: true })),
    ],
    needsArchiveReader = assets.some((asset) => !asset.source_url),
    worker = needsArchiveReader
      ? new Worker(new URL('./audio-worker.mjs', import.meta.url), { type: 'module' })
      : null,
    pending = new Map(),
    resources = [];
  let next = 0,
    effects;
  const clips = [];
  const call = (data) =>
    new Promise((resolve, reject) => {
      const id = ++next,
        timer = setTimeout(() => {
          pending.delete(id);
          const label = data.kind === 'bytes' ? data.path : data.kind;
          reject(Error('Local audio read deadline: ' + label));
        }, 30000);
      pending.set(id, { resolve, reject, timer });
      worker.postMessage({ id, ...data });
    });
  if (worker) {
    worker.onmessage = ({ data }) => {
      const row = pending.get(data.id);
      if (!row) return;
      pending.delete(data.id);
      clearTimeout(row.timer);
      data.error ? row.reject(Error(data.error)) : row.resolve(data.result);
    };
    worker.onerror = (event) => {
      for (const row of pending.values()) {
        clearTimeout(row.timer);
        row.reject(Error(event.message));
      }
      pending.clear();
    };
  }
  const dispose = async () => {
    await effects?.dispose();
    for (const row of resources) {
      await row.player.dispose();
      URL.revokeObjectURL(row.url);
    }
  };
  try {
    if (needsArchiveReader) await call({ kind: 'init', files, locale: 'esES', ...config });
    for (const asset of assets) {
      let bytes;
      if (asset.source_url) {
        if (!/^\/local-audio\/[a-f\d]{64}$/i.test(asset.source_url))
          throw Error('Invalid local audio URL: ' + asset.name);
        let response;
        try {
          response = await fetch(asset.source_url, { signal: AbortSignal.timeout(30000) });
        } catch {
          throw Error('Local audio fetch failed: ' + asset.name);
        }
        if (!response.ok) throw Error('Local audio asset unavailable: ' + asset.name);
        bytes = new Uint8Array(await response.arrayBuffer());
      } else {
        ({ bytes } = await call({ kind: 'bytes', path: asset.name, archive: asset.archive }));
      }
      const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (v) =>
        v.toString(16).padStart(2, '0'),
      ).join('');
      if (hash !== asset.sha256) throw Error('Audio asset identity mismatch');
      if (asset.effect) {
        clips.push({ metadata: asset, bytes });
        continue;
      }
      const element = new Audio(),
        url = URL.createObjectURL(
          new Blob([bytes], {
            type: asset.media_type || (asset.channel === 0 ? 'audio/mpeg' : 'audio/wav'),
          }),
        ),
        row = { ...asset, element, url, player: createMediaPlayer(element) };
      resources.push(row);
      // Glue music stays active until the original UI stops or replaces it.
      // Interface sounds and scene effects remain one-shot playback.
      element.loop = asset.channel === 0;
      // These are verified local bytes. Let the browser decode them when the
      // original UI requests playback; metadata readiness is not needed to
      // enqueue a sound and must not delay the UI session.
      element.preload = 'metadata';
      element.src = url;
      element.load();
    }

    if (clips.length) effects = await createSceneEffects(clips);
  } catch (error) {
    await dispose();
    throw error;
  } finally {
    worker?.terminate();
    for (const row of pending.values()) {
      clearTimeout(row.timer);
      row.reject(Error('Audio provider closed'));
    }
    pending.clear();
  }
  let requestRows = [],
    acknowledgements = [],
    at = 0,
    current,
    draining = false;
  const exports = {
    glue_audio_next() {
      current = requestRows[at++];
      return current ? 0 : 1;
    },
    glue_audio_token: () => current.token,
    glue_audio_operation: () => current.operation,
    glue_audio_channel: () => current.channel,
    glue_audio_id: () => current.asset,
    glue_audio_fade: () => current.fade_seconds,
    glue_audio_ack(token, status) {
      acknowledgements.push({ token, status });
      return 0;
    },
  };
  const bridge = createGlueAudioBridge(exports, resources);
  return {
    pauseScene() {
      effects?.stop();
    },
    async startScene(sequenceMs = 0, globalMs = sequenceMs) {
      if (effects) {
        effects.sync(sequenceMs, globalMs);
        await effects.play();
      }
    },
    frame(sequenceMs, globalMs) {
      effects?.frame(sequenceMs, globalMs);
    },
    effectMetrics: () => effects?.metrics(),
    async drain(requests, volumes) {
      if (
        !Array.isArray(volumes) ||
        volumes.length !== 4 ||
        volumes.some((v) => !Number.isFinite(v) || v < 0 || v > 1)
      )
        throw Error('Audio gain profile');
      effects?.setVolume(volumes[0] * volumes[3]);
      for (const row of resources) row.player.setVolume(volumes[0] * volumes[row.channel + 1]);
      if (draining) throw Error('Audio drain already pending');
      draining = true;
      requestRows = requests;
      acknowledgements = [];
      at = 0;
      try {
        await bridge.drain();
        return acknowledgements;
      } finally {
        draining = false;
        requestRows = [];
      }
    },
    metrics: () => resources.map((row) => ({ id: row.id, ...row.player.metrics() })),
    async dispose() {
      await bridge?.close();
      await dispose();
    },
  };
}
