import { collectFrameXML } from './framexml_sources.mjs';
import { createUIRenderer } from './webgl_renderer.mjs';

// Asset transport, worker messages and WebGL submission only. Original layout,
// script execution, text rasterization and identity APIs remain in C/WASM.
export async function createWorldHUD({
  gl,
  source,
  wasm,
  compat,
  metadata,
  settings,
  player,
  locale,
  width,
  height,
  textureCache,
  glyphAtlas,
  isCurrent = () => true,
  onStatus = () => {},
}) {
  let worker,
    renderer,
    disposed = false,
    serial = 0;
  const pending = new Map();
  const checkCurrent = () => {
    if (disposed || !isCurrent()) throw Error('World UI loading cancelled');
  };
  function dispose() {
    if (disposed) return;
    disposed = true;
    renderer?.dispose();
    worker?.terminate();
    for (const job of pending.values()) {
      clearTimeout(job.timer);
      job.reject(Error('World UI closed'));
    }
    pending.clear();
  }
  function call(message) {
    checkCurrent();
    return new Promise((resolve, reject) => {
      const id = ++serial;
      const timer = setTimeout(() => {
        pending.delete(id);
        reject(Error('World UI source deadline'));
      }, 60000);
      pending.set(id, { resolve, reject, timer });
      worker.postMessage({ id, ...message });
    });
  }
  try {
    onStatus({ phase: 'reading-framexml' });
    const sources = await collectFrameXML(source.readAsset);
    checkCurrent();
    const fonts = [],
      textures = [],
      images = new Map();
    for (const path of sources.fonts) {
      checkCurrent();
      fonts.push({ path, bytes: await source.readAsset(path) });
    }
    for (const path of sources.textures) {
      checkCurrent();
      const image = await source.decodeTexture(path);
      textures.push({ path, width: image.width, height: image.height });
      images.set(path.toLowerCase().replaceAll('\\', '/'), image);
    }
    const race = metadata.races.find((row) => row.id === player.race),
      cls = metadata.classes.find((row) => row.id === player.class);
    if (!race || !cls) throw Error('World UI player labels unavailable');
    checkCurrent();
    worker = new Worker(new URL('../../app/client/world_ui_worker.mjs', import.meta.url), {
      type: 'module',
    });
    worker.onmessage = ({ data }) => {
      const job = pending.get(data.id);
      if (!job) return;
      pending.delete(data.id);
      clearTimeout(job.timer);
      data.error ? job.reject(Error(data.error)) : job.resolve(data.result);
    };
    worker.onerror = (event) => {
      for (const job of pending.values()) {
        clearTimeout(job.timer);
        job.reject(Error(event.message || 'World UI worker failed'));
      }
      pending.clear();
    };
    onStatus({ phase: 'loading-framexml', resources: sources.resources.length });
    const snapshot = await call({
      kind: 'open',
      args: {
        wasm,
        compat,
        sources,
        fonts,
        textures,
        player,
        labels: [race.name, race.token, cls.name, cls.token, race.faction],
        classes: metadata.classes,
        settings,
        locale,
        width,
        height,
      },
    });
    checkCurrent();
    renderer = await createUIRenderer(gl, snapshot.packet, images, {
      logicalWidth: width,
      logicalHeight: height,
      textureCache,
      glyphAtlas,
    });
    checkCurrent();
    onStatus({ phase: 'ready', ...snapshot, packet: undefined, issues: renderer.issues });
    return {
      dispose,
      draw() {
        if (!disposed) renderer.draw();
      },
      async resize(w, h) {
        const next = await call({ kind: 'resize', width: w, height: h });
        checkCurrent();
        const replacement = await createUIRenderer(gl, next.packet, images, {
          logicalWidth: w,
          logicalHeight: h,
          textureCache,
          glyphAtlas,
        });
        if (disposed || !isCurrent()) {
          replacement.dispose();
          return;
        }
        renderer.dispose();
        renderer = replacement;
      },
    };
  } catch (error) {
    dispose();
    throw error;
  }
}
