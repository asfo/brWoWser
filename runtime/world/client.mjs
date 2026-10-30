import { parseDBC } from '../character/catalog.mjs';
import { createCharacterPreview } from '../character/renderer.mjs';
import { createTerrainRenderer } from './terrain_renderer.mjs';
import { createStructureRenderer } from './structure_renderer.mjs';
import { createSkyRenderer } from './sky_renderer.mjs';

// Browser orchestration only. Decoding, spline evaluation, ground sampling,
// movement, player placement and view matrices run in the asset worker's WASM.
export async function createWorldClient({
  gl,
  canvas,
  source,
  catalog,
  modules,
  prepareWasm,
  entry,
  send,
  onIssue = console.warn,
  isCurrent = () => true,
  onProgress = () => {},
}) {
  let disposed = false,
    player,
    skyRenderer,
    cinematic,
    voice,
    voiceURL,
    cinematicMs = 0,
    cinematicBegun = false,
    confirmedSkip = false,
    movementAt = 0,
    lastFlags = 0,
    introductionPending = null,
    introductionId = 0,
    narrationBlocked = false,
    lastCompleteState,
    needsHandoff = false,
    decorRevision = 0,
    loadMilliseconds = 0;
  let modal = false,
    inputX = 0,
    inputY = 0,
    inputZoom = 0;
  const keys = new Set(),
    tiles = new Map(),
    structures = new Map(),
    decoration = new Map(),
    loadQueue = [],
    listeners = [];
  const started = performance.now();
  let timeAnchor = entry.gameTime ?? 720,
    timeReceived = entry.timeReceived ?? performance.timeOrigin + performance.now();
  let activeLoads = 0;
  function runLoads() {
    while (activeLoads < 3 && loadQueue.length) {
      const job = loadQueue.shift();
      activeLoads++;
      Promise.resolve()
        .then(() => (disposed || !isCurrent() ? undefined : job.load()))
        .then(job.resolve, job.reject)
        .finally(() => {
          activeLoads--;
          runLoads();
        });
    }
  }
  function enqueue(load) {
    return new Promise((resolve, reject) => {
      loadQueue.push({ load, resolve, reject });
      runLoads();
    });
  }
  const listen = (target, name, handler, options) => {
    target.addEventListener(name, handler, options);
    listeners.push(() => target.removeEventListener(name, handler, options));
  };
  function dispose() {
    if (disposed) return;
    disposed = true;
    for (const remove of listeners) remove();
    player?.dispose();
    skyRenderer?.dispose();
    for (const tile of tiles.values()) tile.renderer?.dispose();
    for (const structure of structures.values())
      for (const renderer of structure.renderers) renderer.dispose();
    voice?.pause();
    if (voiceURL) URL.revokeObjectURL(voiceURL);
  }
  async function table(name, minFields) {
    return parseDBC(await source.readAsset('DBFilesClient\\' + name + '.dbc'), { name, minFields });
  }
  try {
    await source.world('init', { wasm: modules.world });
    await source.world('spawn', { position: entry.location.position });
    await source.world('environment');
    let cloudTexture;
    try {
      cloudTexture = await source.decodeTexture('Environments\\Stars\\StarsAndClouds.blp');
    } catch (error) {
      onIssue(error.message);
    }
    skyRenderer = createSkyRenderer(gl, cloudTexture);
    const maps = await table('Map', 2),
      row = maps.byId(entry.location.map);
    if (row === undefined) throw Error('The server selected a map absent from the client');
    const name = maps.text(row, 1);
    if (!/^[A-Za-z0-9_]+$/.test(name)) throw Error('Invalid map directory');
    const prefix = 'World\\Maps\\' + name + '\\' + name;
    const wdt = await source.readAsset(prefix + '.wdt');
    const wdtView = new DataView(wdt.buffer, wdt.byteOffset, wdt.byteLength);
    let bigAlpha = false,
      tileTable;
    for (let at = 0; at + 8 <= wdt.length;) {
      const size = wdtView.getUint32(at + 4, true);
      if (size > wdt.length - at - 8) throw Error('Invalid world definition');
      const id = wdtView.getUint32(at, true);
      if (id === 0x4d504844 && size >= 4) bigAlpha = !!(wdtView.getUint32(at + 8, true) & 4);
      if (id === 0x4d41494e && size === 64 * 64 * 8) tileTable = at + 8;
      at += size + 8;
    }
    if (tileTable === undefined) throw Error('This map has no supported outdoor terrain');
    async function loadStructure(path, kind) {
      if (structures.has(path)) return structures.get(path).promise;
      if (structures.size >= 256) return;
      const structure = { renderers: [], doodadRefs: new Set() };
      structures.set(path, structure);
      structure.promise = enqueue(async () => {
        if (kind === 'm2') {
          const mesh = await source.world('static-model', { path: path.replace(/\.mdx$/i, '.m2') });
          if (mesh.effects?.emitters || mesh.effects?.uv) {
            const preview = await createCharacterPreview({
              gl,
              canvas,
              ...source,
              modules,
              prepareWasm,
              worldPropPath: path.replace(/\.mdx$/i, '.m2'),
              isCurrent: () => !disposed && isCurrent(),
              onIssue,
            });
            if (disposed || !isCurrent()) {
              preview.dispose();
              return;
            }
            structure.renderers.push({
              dispose: () => preview.dispose(),
              draw: (view, placement) =>
                preview.draw({
                  elapsed: view.globalMs,
                  viewState: view,
                  placement,
                  clear: false,
                  preserveDepth: true,
                }).triangles,
            });
            return;
          }
          const images = [];
          for (const material of mesh.materials)
            images.push(await source.decodeTexture(material.path));
          if (!disposed && isCurrent())
            structure.renderers.push(createStructureRenderer(gl, mesh, mesh.materials, images));
          return;
        }
        const root = await source.world('wmo-root', { path });
        for (const group of root.groups) {
          if (disposed || !isCurrent()) return;
          const mesh = await source.world('wmo-group', {
            path: path.replace(/\.wmo$/i, '_' + String(group).padStart(3, '0') + '.wmo'),
          });
          for (const index of mesh.doodadRefs || []) structure.doodadRefs.add(index);
          if (!mesh.batches.length) continue;
          const used = new Set();
          for (let i = 2; i < mesh.batches.length; i += 8) used.add(mesh.batches[i]);
          const images = [];
          for (const [id, material] of root.materials.entries())
            images.push(used.has(id) ? await source.decodeTexture(material.path) : null);
          if (disposed || !isCurrent()) return;
          structure.renderers.push(createStructureRenderer(gl, mesh, root.materials, images));
        }
      })
        .catch((error) => {
          structure.error = error;
          onIssue('Structure ' + path + ': ' + error.message);
        })
        .finally(() => {
          structure.ready = true;
        });
      return structure.promise;
    }
    function loadDecoration(object) {
      if (object.kind !== 'wmo') return Promise.resolve();
      const key = object.id + ':' + object.path;
      if (decoration.has(key)) return decoration.get(key).promise;
      const record = { children: [], parentPath: object.path };
      decoration.set(key, record);
      record.promise = enqueue(async () => {
        const result = await source.world('wmo-decor', {
          path: object.path,
          placement: object.placement,
          doodadSet: object.doodadSet,
        });
        if (!disposed && isCurrent()) {
          record.children = result.children;
          decorRevision++;
        }
      })
        .catch((error) => onIssue('Decoration ' + object.path + ': ' + error.message))
        .finally(() => {
          record.ready = true;
        });
      return record.promise;
    }
    async function loadTile(x, y, required = false) {
      if (x < 0 || x >= 64 || y < 0 || y >= 64) return;
      const key = x + ':' + y;
      if (tiles.has(key)) return tiles.get(key).promise;
      if (!(wdtView.getUint32(tileTable + (y * 64 + x) * 8, true) & 1)) {
        if (required) throw Error('The starting terrain tile is absent from the client');
        return;
      }
      const state = {};
      tiles.set(key, state);
      state.promise = (async () => {
        const tile = await source.world('terrain', {
          path: prefix + '_' + x + '_' + y + '.adt',
          bigAlpha,
        });
        const images = [];
        // Avoid a request flood sharing the archive worker with input/camera samples.
        for (const path of tile.textures) images.push(await source.decodeTexture(path));
        if (disposed || !isCurrent() || tiles.get(key) !== state) return;
        state.renderer = createTerrainRenderer(gl, tile, images);
        state.tileX = x;
        state.tileY = y;
        state.objects = tile.objects;
      })().catch((error) => {
        state.error = error;
        if (required) throw error;
        onIssue(error.message);
      });
      return state.promise;
    }
    async function loadIntroduction(id) {
      introductionId = id;
      const [sequences, cameras] = await Promise.all([
        table('CinematicSequences', 10),
        table('CinematicCamera', 7),
      ]);
      const sequence = sequences.byId(id);
      if (sequence === undefined) throw Error('Original introduction sequence is unavailable');
      const cameraId = sequences.u(sequence, 2),
        camera = cameras.byId(cameraId);
      if (camera === undefined) throw Error('Original introduction camera is unavailable');
      const path = cameras.text(camera, 1).replace(/\.mdx$/i, '.m2');
      const result = await source.world('cinematic', {
        path,
        origin: Array.from({ length: 4 }, (_, i) => cameras.f(camera, 3 + i)),
      });
      cinematic = { duration: result.duration, id };
      const sound = cameras.u(camera, 2);
      if (sound) {
        try {
          const sounds = await table('SoundEntries', 24),
            soundRow = sounds.byId(sound);
          if (soundRow === undefined)
            throw Error('Introduction narration is absent from SoundEntries');
          let file = '';
          for (let i = 3; i < 13 && !file; i++) file = sounds.text(soundRow, i);
          if (!file) throw Error('Introduction narration has no source file');
          const bytes = await source.readAsset(
            sounds.text(soundRow, 23).replace(/\\$/, '') + '\\' + file,
          );
          voiceURL = URL.createObjectURL(
            new Blob([bytes], { type: /\.mp3$/i.test(file) ? 'audio/mpeg' : 'audio/wav' }),
          );
          voice = new Audio(voiceURL);
          voice.preload = 'auto';
          voice.volume = 0.8;
          voice.onerror = () => onIssue('Introduction narration could not be decoded');
        } catch (error) {
          onIssue(error.message);
        }
      }
    }
    if (entry.cinematic) await loadIntroduction(entry.cinematic);
    const appearance = { ...entry.character, mode: 'select', worldPreview: true };
    const preview = await createCharacterPreview({
      gl,
      canvas,
      ...source,
      catalog,
      appearance,
      modules,
      prepareWasm,
      isCurrent: () => !disposed && isCurrent(),
      onIssue,
    });
    if (disposed || !isCurrent()) {
      preview.dispose();
      throw Error('World loading cancelled');
    }
    player = preview;
    const controls = () => [
      Number(keys.has('KeyW') || keys.has('ArrowUp')) -
        Number(keys.has('KeyS') || keys.has('ArrowDown')),
      Number(keys.has('KeyQ')) - Number(keys.has('KeyE')),
      Number(keys.has('KeyA') || keys.has('ArrowLeft')) -
        Number(keys.has('KeyD') || keys.has('ArrowRight')),
      0,
      inputX,
      inputY,
      inputZoom,
    ];
    async function sample(elapsed) {
      const data = {
        elapsed,
        controls: controls(),
        aspect: canvas.width / canvas.height,
        map: entry.location.map,
        time:
          (timeAnchor + (performance.timeOrigin + performance.now() - timeReceived) / 60000) * 2,
        globalMs: performance.now() - started,
      };
      if (cinematic) data.cinematicMs = Math.min(cinematic.duration, Math.floor(cinematicMs));
      inputX = inputY = inputZoom = 0;
      return source.world('sample', data);
    }
    function visibleObjects(state, tile) {
      let mask;
      for (let i = 0; i < state.objectVisibility.length; i += 130)
        if (
          state.objectVisibility[i] === tile.tileX &&
          state.objectVisibility[i + 1] === tile.tileY
        ) {
          mask = state.objectVisibility.subarray(i + 2, i + 130);
          break;
        }
      return (tile.objects || []).filter(
        (object, index) => !mask || !!(mask[index >>> 5] & (1 << (index & 31))),
      );
    }
    function visibleDecor(state) {
      const result = [];
      for (const record of decoration.values())
        for (const child of record.children) {
          if (!structures.get(record.parentPath)?.doodadRefs.has(child.sourceIndex)) continue;
          if (state.decorVisibility[child.decorId >>> 5] & (1 << (child.decorId & 31)))
            result.push(child);
        }
      return result;
    }
    function prepareVisible(state) {
      const jobs = [],
        seen = new Set();
      for (const tile of tiles.values())
        for (const object of visibleObjects(state, tile)) {
          jobs.push(loadDecoration(object));
          if (seen.has(object.path)) continue;
          seen.add(object.path);
          jobs.push(loadStructure(object.path, object.kind));
        }
      for (const child of visibleDecor(state))
        if (!seen.has(child.path)) {
          seen.add(child.path);
          jobs.push(loadStructure(child.path, 'm2'));
        }
      return Promise.all(jobs);
    }
    onProgress({ phase: 'terrain' });
    const initial = await sample(0);
    await loadTile(initial.tiles[0], initial.tiles[1], true);
    // Warm the required camera/player tiles first. Neighbors stream during
    // presentation instead of delaying entry behind nine complete texture sets.
    await loadTile(initial.tiles[2], initial.tiles[3], true);
    onProgress({ phase: 'scene' });
    const warmed = await sample(0);
    await prepareVisible(warmed);
    await prepareVisible(await sample(0));
    if (disposed || !isCurrent()) throw Error('World loading cancelled');
    lastCompleteState = warmed;
    await send({ operation: 'ready' });
    loadMilliseconds = Math.round(performance.now() - started);
    let orbiting = false;
    listen(
      canvas,
      'keydown',
      (event) => {
        if (event.code === 'Escape' && cinematic) {
          event.preventDefault();
          event.stopImmediatePropagation();
          keys.clear();
          send({ operation: modal ? 'cinematic-resume' : 'cinematic-prompt' }).catch((error) =>
            onIssue(error.message),
          );
          return;
        }
        if (
          [
            'KeyW',
            'KeyA',
            'KeyS',
            'KeyD',
            'KeyQ',
            'KeyE',
            'ArrowUp',
            'ArrowDown',
            'ArrowLeft',
            'ArrowRight',
          ].includes(event.code)
        ) {
          event.preventDefault();
          event.stopImmediatePropagation();
          if (!modal) keys.add(event.code);
        }
      },
      true,
    );
    listen(
      canvas,
      'keyup',
      (event) => {
        keys.delete(event.code);
      },
      true,
    );
    listen(window, 'blur', () => {
      keys.clear();
      orbiting = false;
    });
    listen(
      canvas,
      'pointerdown',
      (event) => {
        if (event.button === 2 && !cinematic && !modal) {
          orbiting = true;
          canvas.focus();
          event.preventDefault();
          event.stopImmediatePropagation();
        }
        if (voice?.paused && cinematicBegun && !modal) {
          ((narrationBlocked = false), lastCompleteState, (needsHandoff = false));
          voice.play().catch(() => {
            narrationBlocked = true;
          });
        }
      },
      true,
    );
    listen(window, 'pointerup', () => {
      orbiting = false;
    });
    listen(canvas, 'contextmenu', (event) => event.preventDefault());
    listen(
      canvas,
      'pointermove',
      (event) => {
        if (orbiting) {
          inputX += event.movementX;
          inputY += event.movementY;
          event.stopImmediatePropagation();
        }
      },
      true,
    );
    listen(
      canvas,
      'wheel',
      (event) => {
        if (!cinematic && !modal) {
          inputZoom += event.deltaY;
          event.preventDefault();
          event.stopImmediatePropagation();
        }
      },
      { capture: true, passive: false },
    );
    return {
      dispose,
      sync(next) {
        if (next.gameTime !== undefined && next.timeReceived !== timeReceived) {
          timeAnchor = next.gameTime;
          timeReceived = next.timeReceived;
        }
        const wasModal = modal;
        modal = !!next.modal;
        if (next.cinematic && next.cinematic !== introductionId && !introductionPending) {
          introductionPending = loadIntroduction(next.cinematic)
            .catch((error) => {
              onIssue(error.message);
              return send({ operation: 'error', message: error.message });
            })
            .finally(() => {
              introductionPending = null;
            });
        }
        if (next.cinematic === 0 && cinematic) {
          cinematic = null;
          confirmedSkip = wasModal;
          voice?.pause();
          needsHandoff = true;
        }
      },
      async draw(elapsed) {
        if (disposed) return;
        let state = introductionPending
          ? lastCompleteState
          : await sample(modal || cinematic ? 0 : elapsed);
        if (!state) return;
        const x = state.tiles[0],
          y = state.tiles[1],
          current = tiles.get(x + ':' + y);
        if (!current?.renderer) {
          if (current?.error) throw current.error;
          await loadTile(x, y, true);
          state = await sample(0);
        }
        lastCompleteState = state;
        // Keep a bounded 3x3 neighborhood around the current viewpoint. Source
        // terrain never becomes a fabricated flat fallback when a read fails.
        for (let dy = -1; dy <= 1; dy++)
          for (let dx = -1; dx <= 1; dx++)
            loadTile(x + dx, y + dy).catch((error) => onIssue(error.message));
        for (const [key, tile] of tiles) {
          const [tx, ty] = key.split(':').map(Number);
          if (Math.abs(tx - x) > 1 || Math.abs(ty - y) > 1) {
            tile.renderer?.dispose();
            tiles.delete(key);
          }
        }
        const revision = decorRevision;
        // Streaming must not stall presentation or repeatedly pause narration.
        // Only the explicit cinematic-to-player handoff waits for its scenery.
        const visibleReady = prepareVisible(state);
        if (needsHandoff) await visibleReady;
        else visibleReady.catch((error) => onIssue(error.message));
        if (needsHandoff || revision !== decorRevision) {
          state = await sample(0);
          const decorationReady = prepareVisible(state);
          if (needsHandoff) await decorationReady;
          else decorationReady.catch((error) => onIssue(error.message));
          needsHandoff = false;
        }
        if (cinematic && !modal && !introductionPending) {
          if (!cinematicBegun) {
            await send({ operation: 'cinematic-begin' });
            cinematicBegun = true;
          }
          if (voice?.paused && !narrationBlocked)
            voice.play().catch((error) => {
              narrationBlocked = true;
              onIssue(error.message);
            });
          if (voice && !voice.paused && !voice.ended) cinematicMs = voice.currentTime * 1000;
          else cinematicMs += elapsed * 1000;
          if (cinematicMs >= cinematic.duration) {
            await send({ operation: 'cinematic-end' });
            cinematic = null;
            needsHandoff = true;
            voice?.pause();
          }
        } else if (modal) voice?.pause();
        const view = {
          view: state.view.subarray(0, 16),
          projection: state.view.subarray(16, 32),
          transform: state.view.subarray(32, 48),
          relativeTransform: state.view.subarray(67, 83),
          particleView: state.view.subarray(48, 64),
          eye: state.view.subarray(64, 67),
          globalMs: performance.now() - started,
        };
        const now = performance.now(),
          flags = state.player[4];
        if (!cinematic && !modal && (flags !== lastFlags || now - movementAt >= 500)) {
          const opcode =
            flags === lastFlags
              ? 0xee
              : !flags
                ? 0xb7
                : flags & 1
                  ? 0xb5
                  : flags & 2
                    ? 0xb6
                    : flags & 4
                      ? 0xb8
                      : flags & 8
                        ? 0xb9
                        : flags & 16
                          ? 0xbb
                          : 0xbc;
          await send({
            operation: 'movement',
            opcode,
            flags,
            position: Array.from(state.player.subarray(0, 4)),
            time: Math.floor(now),
          });
          lastFlags = flags;
          movementAt = now;
        }
        await new Promise((resolve) => requestAnimationFrame(resolve));
        if (disposed || !isCurrent()) return;
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, canvas.width, canvas.height);
        gl.disable(gl.SCISSOR_TEST);
        gl.depthMask(true);
        gl.clearColor(0.53, 0.67, 0.78, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        skyRenderer.draw(state);
        let triangles = 0;
        for (const tile of tiles.values())
          if (tile.renderer) {
            let mask;
            for (let i = 0; i < state.visibility.length; i += 10)
              if (state.visibility[i] === tile.tileX && state.visibility[i + 1] === tile.tileY) {
                mask = state.visibility.subarray(i + 2, i + 10);
                break;
              }
            triangles += tile.renderer.draw(view, mask);
          }
        const drawnObjects = new Set();
        for (const tile of tiles.values())
          for (const object of visibleObjects(state, tile)) {
            if (drawnObjects.has(object.id)) continue;
            drawnObjects.add(object.id);
            for (const renderer of structures.get(object.path)?.renderers || [])
              triangles += renderer.draw(view, object.placement);
          }
        for (const object of visibleDecor(state))
          for (const renderer of structures.get(object.path)?.renderers || [])
            triangles += renderer.draw(view, object.placement);
        if (!cinematic)
          player.draw({
            elapsed: view.globalMs,
            facing: state.player[3],
            moving: !!(state.player[4] & 15),
            viewState: view,
            placement: state.placement,
            clear: false,
            preserveDepth: true,
          });
        return {
          load_ms: loadMilliseconds,
          network_ms: entry.networkMs,
          queued_assets: loadQueue.length,
          phase: cinematic ? 'cinematic' : 'world',
          triangles,
          tiles: [...tiles.values()].filter((t) => t.renderer).length,
          position: Array.from(state.player.subarray(0, 4)),
          cinematic_ms: Math.floor(cinematicMs),
          game_time_minutes:
            (timeAnchor + (performance.timeOrigin + performance.now() - timeReceived) / 60000) %
            1440,
          skipped: confirmedSkip,
        };
      },
    };
  } catch (error) {
    dispose();
    throw error;
  }
}
