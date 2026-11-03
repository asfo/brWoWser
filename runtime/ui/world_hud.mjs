import { collectFrameXML } from './framexml_sources.mjs';
import { sameUIPacket } from './packet_equal.mjs';
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
  send = async () => {},
  onZoom = () => {},
}) {
  let worker,
    renderer,
    disposed = false,
    serial = 0,
    unitRevision = -1,
    hudRevision = -1,
    environmentKey,
    queue = Promise.resolve(),
    currentPacket,
    currentItems = [],
    currentRects;
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
      let image;
      try {
        image = await source.decodeTexture(path);
      } catch (error) {
        if (sources.optionalTextures.includes(path)) continue;
        throw error;
      }
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
    const apply = async (next) => {
      checkCurrent();
      currentRects = next.rects;
      currentItems = next.items;
      onZoom(next.zoom);
      for (const index of next.seen) await send({ operation: 'tutorial-seen', index });
      if (!sameUIPacket(currentPacket, next.packet)) {
        const replacement = await createUIRenderer(gl, next.packet, images, {
          logicalWidth: width,
          logicalHeight: height,
          textureCache,
          glyphAtlas,
        });
        if (disposed || !isCurrent()) {
          replacement.dispose();
          return;
        }
        renderer?.dispose();
        renderer = replacement;
        currentPacket = next.packet;
      }
      onStatus({
        phase: 'ready',
        ...next,
        packet: undefined,
        items: undefined,
        issues: renderer.issues,
      });
    };
    const schedule = (operation) => {
      const job = queue.then(operation);
      queue = job.catch(() => {});
      return job;
    };
    await apply(snapshot);
    return {
      dispose,
      get rects() {
        return currentRects;
      },
      get items() {
        return currentItems;
      },
      captures(event, canvas) {
        const box = canvas.getBoundingClientRect(),
          x = ((event.clientX - box.left) * width) / box.width,
          y = ((box.bottom - event.clientY) * height) / box.height;
        return [
          'MainMenuBar',
          'ShapeshiftBarFrame',
          'MinimapCluster',
          'PlayerFrame',
          'TutorialFrame',
          'ChatFrame1',
          'ChatFrame1ButtonFrame',
          'ChatFrame1Tab',
          'ChatFrame2Tab',
        ].some((name) => {
          const r = currentRects?.[name];
          return r && x >= r[0] && y >= r[1] && x < r[0] + r[2] && y < r[1] + r[3];
        });
      },
      updateUnit(unit) {
        if (!unit || unit.revision === unitRevision) return Promise.resolve();
        unitRevision = unit.revision;
        return schedule(async () => apply(await call({ kind: 'unit', unit })));
      },
      updateHUD(hud) {
        if (!hud || hud.revision === hudRevision) return Promise.resolve();
        hudRevision = hud.revision;
        return schedule(async () => {
          const records = await source.hudCatalog(hud),
            textures = [];
          for (const record of records) {
            const key = record.path.toLowerCase().replaceAll('\\', '/');
            if (!images.has(key)) {
              const image = await source.decodeTexture(record.path);
              images.set(key, image);
              textures.push({ path: record.path, width: image.width, height: image.height });
            }
          }
          await apply(await call({ kind: 'hud', hud, records, textures }));
        });
      },
      environment(zone, minutes) {
        minutes = Math.floor(minutes);
        const key = zone + ':' + minutes;
        if (key === environmentKey || !Number.isFinite(minutes)) return Promise.resolve();
        environmentKey = key;
        return schedule(async () => apply(await call({ kind: 'environment', zone, minutes })));
      },
      pointer(action) {
        return schedule(async () => apply(await call({ kind: 'pointer', action })));
      },
      key(key) {
        return schedule(async () => apply(await call({ kind: 'key', key })));
      },
      draw() {
        if (!disposed) renderer?.draw();
      },
      resize(w, h) {
        return schedule(async () => {
          const next = await call({ kind: 'resize', width: w, height: h });
          width = w;
          height = h;
          currentPacket = null;
          await apply(next);
        });
      },
    };
  } catch (error) {
    dispose();
    throw error;
  }
}
