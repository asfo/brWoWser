import { createGlyphAtlas } from '../../runtime/ui/glyph_atlas.mjs';
import { createSoftwareCursor } from '../../runtime/ui/software_cursor.mjs';
import { createCinematicResolver } from '../../runtime/movie/catalogue.mjs';
import { gameViewport, applyGameViewport } from '../../runtime/graphics/viewport.mjs';
import { createUITextureCache } from '../../runtime/ui/texture_cache.mjs';
import { createMovieSurface } from '../../runtime/movie/canvas_surface.mjs';
import { inventoryFromMount } from '../../runtime/config/local_mount.mjs';
import { chooseStartupConfig } from '../../runtime/config/session_profile.mjs';
import { sceneClocks } from '../../runtime/scene/playback_clock.mjs';
import { loadLocalEditFont } from './font_assets.mjs';
import { connectCanvasKeyboard } from '../../runtime/ui/canvas_keyboard.mjs';
import { sameUIPacket } from '../../runtime/ui/packet_equal.mjs';
import { connectCanvasInput } from '../../runtime/ui/canvas_input.mjs';
import {
  agreementIdentity,
  agreementStore,
  validateAgreementRecord,
} from '../../runtime/ui/agreement_store.mjs';
import { readLegalFiles } from '../../runtime/ui/legal_files.mjs';
import { clientFiles } from '../../runtime/config/client_files.mjs';
import { createLocalAudio } from './audio_assets.mjs';
import { createMoviePlayer } from '../../runtime/movie/browser_player.mjs';
import { configStore } from '../../runtime/config/host.mjs';
import { loadUIImages } from './ui_assets.mjs';
import { createUIRenderer } from '../../runtime/ui/webgl_renderer.mjs';
import { runFrameSession } from './frame_session.mjs';
import { createResourceSession } from './resource_session.mjs';
import { createSceneGlow } from '../../runtime/scene/glow_renderer.mjs';
import { createSceneRenderer } from '../../runtime/scene/renderer.mjs';
import { createCharacterAssetSource } from '../../runtime/character/assets.mjs';
import { createCharacterCatalog } from '../../runtime/character/catalog.mjs';
import { createCharacterPreview } from '../../runtime/character/renderer.mjs';
import { createWorldClient } from '../../runtime/world/client.mjs';
import { uploadWebGLResources } from '../../runtime/graphics/webgl_resources.mjs';
const get = async (path) => {
    const r = await fetch(path);
    if (!r.ok) throw Error('HTTP ' + r.status + ' ' + path);
    return r.arrayBuffer();
  },
  json = async (path) => JSON.parse(new TextDecoder().decode(await get(path)));
const freshMode = new URLSearchParams(location.search).has('fresh');
let resetProfile = new URLSearchParams(location.search).has('reset-profile');
const chooseRealmMode = new URLSearchParams(location.search).has('choose-realm');
const startupMode = true,
  liveMode = true,
  inputMode = true;
if (liveMode) document.documentElement.classList.add('live');
let viewport = liveMode
  ? gameViewport(innerWidth, innerHeight, devicePixelRatio)
  : { logicalWidth: 1024, logicalHeight: 768, width: 1024, height: 768 };
window.addEventListener('resize', () => {
  if (!liveMode || window.sceneSession) return;
  viewport = gameViewport(innerWidth, innerHeight, devicePixelRatio);
  const canvas = document.querySelector('canvas');
  if (canvas) applyGameViewport(canvas, viewport);
});
window.ready = true;
const launch = (event) => {
  window.startupFailure = null;
  window.experiment = (async () => {
    if (resetProfile) {
      await configStore('clear');
      await agreementStore('clear');
      resetProfile = false;
      const url = new URL(location.href);
      url.searchParams.delete('reset-profile');
      history.replaceState(null, '', url);
    }
    const config = await json('/config.json'),
      modules = Object.fromEntries(
        await Promise.all(
          Object.entries(config.moduleURLs).map(async ([k, p]) => [k, await get(p)]),
        ),
      ),
      manifest = await json('/manifest.json'),
      inventory = event.inventory || clientFiles(event.target.files),
      selected = new Map([...inventory.data].map(([path, row]) => [path, row.file || row])),
      files = [...new Set(manifest.jobs.filter((r) => r.path).map((r) => r.path))].map((path) => ({
        ...inventory.data.get(path.toLowerCase()),
        path,
      }));
    if (files.some((r) => !r.file && !r.url)) throw Error('Missing selected local archive');
    const originalFiles = await Promise.all(
        config.fileURLs.map(async (path, i) => new File([await get(path)], 'local-' + i)),
      ),
      frameArgs = {
        wasm: await get('/frames.wasm'),
        files: originalFiles,
        fixture: config.fixture,
        uiAssets: config.uiAssets,
        chooseRealmMode,
        htmlAssets: await readLegalFiles(inventory, config.locale, [
          'eula.html',
          'tos.html',
          ...['Credits.html', 'Credits_BC.html', 'Credits_LK.html'].filter((name) =>
            inventory.data.has((config.locale + '/' + name).toLowerCase()),
          ),
          ...(inventory.data.has((config.locale + '/termination.html').toLowerCase())
            ? ['termination.html']
            : []),
        ]),
      },
      resourceArgs = {
        files,
        models: manifest.jobs,
        modules,
        indexHash: config.indexHash,
        wasm: await get('/resources.wasm'),
        lookupWasm: await get('/lookup.wasm'),
        decoderWasm: await get('/decoder.wasm'),
        blpWasm: await get('/blp.wasm'),
        prepareWasm: await get('/prepare.wasm'),
        meshWasm: modules.mesh,
      };
    if (liveMode)
      frameArgs.viewport = { width: viewport.logicalWidth, height: viewport.logicalHeight };
    frameArgs.fontAssets = [
      await loadLocalEditFont(
        Worker,
        inventory.archives,
        {
          locale: config.locale,
          startup: await get('/startup.wasm'),
          orderWasm: await get('/order.wasm'),
          lookupWasm: resourceArgs.lookupWasm,
          decoderWasm: resourceArgs.decoderWasm,
        },
        config.editFont,
      ),
    ];
    let moviePlayer,
      movieSurface,
      moviePresentation,
      moviePlaying = false,
      startupEvents = [];
    const consentIdentity = await agreementIdentity(frameArgs.htmlAssets);
    if (inputMode) {
      frameArgs.interactive = true;
      frameArgs.authNetwork = liveMode;
      frameArgs.clientImage = config.clientImage;
      frameArgs.realmCategories = config.realmCategories;
      frameArgs.characterMetadata = config.characterMetadata;
      frameArgs.compactInput = liveMode;
      frameArgs.acceptedMask = freshMode
        ? 0
        : validateAgreementRecord(await agreementStore('read'), consentIdentity);
    }
    if (startupMode) {
      const cfg = inventory.config,
        startupConfig = chooseStartupConfig({
          fresh: freshMode,
          saved: await configStore('read'),
          imported: cfg ? await cfg.text() : undefined,
        }),
        saved = chooseRealmMode
          ? startupConfig.replace(/^SET[ \t]+realmName[ \t]+"[^"\r\n]*"[ \t]*(?:\r?\n|$)/gim, '')
          : startupConfig;
      const resumeConfig =
        !freshMode &&
        !chooseRealmMode &&
        config.preferredRealm &&
        !/^SET\s+realmName\s+"[^"\r\n]+"/m.test(saved || '')
          ? (saved || '') + '\nSET realmName ' + JSON.stringify(config.preferredRealm) + '\n'
          : saved;
      frameArgs.startup = {
        path: config.movie.logical_path,
        movies: config.movies,
        nativeFlag: 2,
        configBytes: new TextEncoder().encode(resumeConfig ?? 'SET gxResolution "1024x768"\n'),
      };
      const video = document.createElement('video');
      window.movieDecoder = video;
      const resolve = createCinematicResolver(selected, config.movies, {
        initial: config.movie,
        prepare: (movie) => json('/movie-cache/' + movie.name + '.json'),
        readCache: (_, movie) =>
          movie.logical_path === config.movie.logical_path
            ? get('/private-movie.webm')
            : get('/movie-cache/' + movie.name + '.webm'),
      });
      moviePlayer = createMoviePlayer({
        video,
        resolve,
        finished: (message) => window.movieFinish?.(message.token),
      });
      video.hidden = true;
      document.querySelector('canvas').hidden = false;
    }

    const canvas = document.querySelector('canvas');
    applyGameViewport(canvas, viewport);
    const rawGL = canvas.getContext('webgl2', { antialias: false, alpha: false });
    if (!rawGL) throw Error('WebGL2 unavailable');
    window.gpuDiagnostics = { calls: [] };
    const gl = rawGL;
    let session,
      pool,
      renderer,
      uiRenderer,
      audio,
      uiAssets,
      playbackStarted = 0,
      first,
      firstDrawFinished = false,
      frameSession,
      inputConnection,
      keyboardConnection;
    let characterSourcePromise, characterCatalogPromise, characterPreview, characterBackground;
    let worldClient,
      worldKey = '',
      worldGeneration = 0;
    const ensureCharacterSource = () => {
      characterSourcePromise ??= (async () =>
        createCharacterAssetSource(inventory.archives, {
          locale: config.locale,
          startup: await get('/startup.wasm'),
          orderWasm: await get('/order.wasm'),
          lookupWasm: resourceArgs.lookupWasm,
          decoderWasm: resourceArgs.decoderWasm,
          blpWasm: await get('/ui-blp.wasm'),
        }))();
      characterCatalogPromise ??= characterSourcePromise.then((source) =>
        createCharacterCatalog(source.readAsset),
      );
    };
    const updateWorld = (state) => {
      const entry = state.world_entry;
      if (!entry) {
        if (worldKey) {
          worldGeneration++;
          worldKey = '';
          worldClient?.dispose();
          worldClient = null;
        }
        return;
      }
      if (entry.phase === 'load-error') {
        if (worldKey || worldClient) {
          worldGeneration++;
          worldKey = '';
          worldClient?.dispose();
          worldClient = null;
        }
        window.worldStatus = { phase: 'error', message: entry.error };
        return;
      }
      worldClient?.sync(entry);
      if (!entry.location) return;
      const key = JSON.stringify([
        entry.character.guid,
        entry.location.map,
        entry.resourceRevision || 0,
      ]);
      if (key === worldKey) return;
      worldKey = key;
      const generation = ++worldGeneration;
      ensureCharacterSource();
      disposeCharacter();
      disposeBackground();
      window.worldStatus = { phase: 'loading' };
      (async () => {
        const [source, catalog] = await Promise.all([
          characterSourcePromise,
          characterCatalogPromise,
        ]);
        if (generation !== worldGeneration) return;
        const client = await createWorldClient({
          gl,
          canvas,
          source,
          catalog,
          modules,
          prepareWasm: resourceArgs.prepareWasm,
          entry,
          isCurrent: () => generation === worldGeneration,
          send: (action) => window.uiInput({ kind: 14, ...action }),
          onIssue: (message) => console.warn('World: ' + message),
        });
        if (generation !== worldGeneration) {
          client.dispose();
          return;
        }
        worldClient = client;
        client.sync(window.uiInputState?.world_entry || entry);
        window.worldStatus = { phase: 'ready' };
      })().catch((error) => {
        if (generation !== worldGeneration) return;
        window.worldStatus = { phase: 'error', message: error.message };
        console.warn('World: ' + error.message);
        window
          .uiInput({ kind: 14, operation: 'error', message: error.message })
          .catch((error) => console.warn(error.message));
      });
    };
    let characterGlow;
    let characterPointerDown = false;
    let backgroundKey = '',
      backgroundGeneration = 0,
      backgroundStarted = 0;
    let characterKey = '',
      characterGeneration = 0,
      characterStarted = 0;
    const isCharacterScreen = (state) => state?.screen_state === 4 || state?.screen_state === 5;
    const disposeCharacter = () => {
      characterGeneration++;
      characterKey = '';
      characterPreview?.dispose();
      characterPreview = null;
    };
    const disposeBackground = () => {
      backgroundGeneration++;
      backgroundKey = '';
      backgroundStarted = 0;
      characterBackground?.dispose();
      characterBackground = null;
      window.characterBackgroundStatus = { state: 'idle' };
    };
    const updateCharacterPreview = (state) => {
      updateWorld(state);
      if (state.world_entry) return;
      const appearance = state.character_preview;
      if (!isCharacterScreen(state) || !appearance) {
        if (characterKey || characterPreview) disposeCharacter();
        if (backgroundKey || characterBackground) disposeBackground();
        return;
      }
      const key = JSON.stringify([
        appearance.mode,
        appearance.race,
        appearance.gender,
        appearance.class,
        appearance.skin,
        appearance.face,
        appearance.hairStyle,
        appearance.hairColor,
        appearance.facialHair,
        appearance.equipment,
      ]);
      ensureCharacterSource();
      const backgroundPath = appearance.backgroundPath;
      if (backgroundPath && backgroundPath !== backgroundKey) {
        disposeBackground();
        backgroundKey = backgroundPath;
        const backgroundRevision = backgroundGeneration;
        window.characterBackgroundStatus = { state: 'loading', model: backgroundPath };
        (async () => {
          const source = await characterSourcePromise;
          const preview = await createCharacterPreview({
            gl,
            canvas,
            ...source,
            backgroundPath,
            isCurrent: () => backgroundRevision === backgroundGeneration,
            modules,
            prepareWasm: resourceArgs.prepareWasm,
            onIssue: (message) => console.warn(message),
          });
          if (backgroundRevision !== backgroundGeneration) {
            preview.dispose();
            return;
          }
          characterBackground = preview;
          backgroundStarted = performance.now();
        })().catch((error) => {
          if (backgroundRevision === backgroundGeneration) {
            window.characterBackgroundStatus = {
              state: 'error',
              model: backgroundPath,
              error: error.message,
            };
            console.warn('Character background: ' + error.message);
          }
        });
      }
      if (appearance.visible === false) {
        if (characterKey || characterPreview) disposeCharacter();
        return;
      }
      if (key === characterKey) return;
      // Keep the last complete GPU model until its replacement is ready.
      // Customization may await archive reads; clearing here caused a flash.
      characterGeneration++;
      characterKey = key;
      const generation = characterGeneration;
      window.characterPreviewStatus = { state: 'loading' };
      (async () => {
        const [source, catalog] = await Promise.all([
          characterSourcePromise,
          characterCatalogPromise,
        ]);
        if (generation !== characterGeneration) return;
        const preview = await createCharacterPreview({
          gl,
          canvas,
          ...source,
          catalog,
          appearance,
          isCurrent: () => generation === characterGeneration,
          modules,
          prepareWasm: resourceArgs.prepareWasm,
          onIssue: (message) => console.warn(message),
        });
        if (generation !== characterGeneration) {
          preview.dispose();
          return;
        }
        const previous = characterPreview;
        characterPreview = preview;
        if (!previous) characterStarted = performance.now();
        previous?.dispose();
        window.characterPreviewStatus = {
          state: 'ready',
          model: preview.modelPath,
          animated: preview.animated,
        };
      })().catch((error) => {
        if (generation !== characterGeneration) return;
        window.characterPreviewStatus = { state: 'error', message: error.message };
        console.warn('Character preview: ' + error.message);
      });
    };
    const clearScene = (target = null) => {
      gl.bindFramebuffer(gl.FRAMEBUFFER, target);
      gl.viewport(0, 0, canvas.width, canvas.height);
      gl.disable(gl.SCISSOR_TEST);
      gl.depthMask(true);
      gl.clearColor(0, 0, 0, 1);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    };
    const drawCharacter = (state) => {
      // Input processing can await a worker after the rAF timestamp was taken.
      // Sample the clock at drawing time so particle time cannot move backward.
      const now = performance.now();
      const amount = state.character_preview?.backgroundLighting?.glow || 0;
      const glow = (characterGlow ??= createSceneGlow(gl));
      clearScene(glow.begin(canvas.width, canvas.height, amount));
      try {
        const backgroundDraw = characterBackground?.draw({
          elapsed: Math.max(0, now - backgroundStarted),
          clear: true,
          lighting: state.character_preview?.backgroundLighting,
          diagonalFraming: state.screen_state === 4,
        });
        if (backgroundDraw)
          window.characterBackgroundStatus = {
            state: 'drawn',
            model: characterBackground.modelPath,
            ...backgroundDraw,
          };
        if (!characterPreview || !characterBackground) return;
        const characterDraw = characterPreview.draw({
          elapsed: Math.max(0, now - characterStarted),
          facing: ((state.character_preview?.facing || 0) * Math.PI) / 180,
          lighting: state.character_preview?.lighting,
          camera: characterBackground.cameraValues,
          diagonalFraming: state.screen_state === 4,
          placement: characterBackground.attachment(0, Math.max(0, now - backgroundStarted)),
          pointLights: characterBackground.lights(),
          clear: false,
        });
        window.characterPreviewStatus = {
          state: 'drawn',
          model: characterPreview.modelPath,
          ...characterDraw,
        };
        gl.viewport(0, 0, canvas.width, canvas.height);
      } finally {
        glow.end(amount);
      }
    };
    window.addEventListener(
      'pagehide',
      () => {
        disposeCharacter();
        worldGeneration++;
        worldClient?.dispose();
        disposeBackground();
        characterGlow?.dispose();
        characterSourcePromise?.then((source) => source.dispose()).catch(() => {});
      },
      { once: true },
    );
    if (startupMode) {
      movieSurface = createMovieSurface(gl, canvas, window.movieDecoder);
    }
    const uiTextureCache = createUITextureCache(gl);
    const glyphAtlas = createGlyphAtlas(gl);
    window.addEventListener('pagehide', () => glyphAtlas.dispose(), { once: true });
    window.addEventListener('pagehide', () => uiTextureCache.dispose(), { once: true });
    let softwareCursor;
    let uiConfig, previousUIPacket;
    const prepareUI = async (packet, replace = false) => {
      if (
        uiRenderer &&
        (!replace ||
          sameUIPacket(
            { ...previousUIPacket, surfaceAlpha: undefined },
            { ...packet, surfaceAlpha: undefined },
          ))
      ) {
        uiRenderer.setOpacity(packet.surfaceAlpha ?? 1);
        previousUIPacket = packet;
        return uiAssets;
      }
      if (!uiConfig) {
        const [startup, orderWasm, blpWasm] = await Promise.all([
          get('/startup.wasm'),
          get('/order.wasm'),
          get('/ui-blp.wasm'),
        ]);
        uiConfig = {
          locale: config.locale,
          startup,
          orderWasm,
          lookupWasm: resourceArgs.lookupWasm,
          decoderWasm: resourceArgs.decoderWasm,
          blpWasm,
        };
      }
      const allFiles = inventory.archives;
      const nextAssets = await loadUIImages(allFiles, uiConfig, packet, uiAssets?.images),
        nextRenderer = await createUIRenderer(gl, packet, nextAssets.images, {
          textureCache: uiTextureCache,
          glyphAtlas,
          reuse: uiRenderer,
          logicalWidth: viewport.logicalWidth,
          logicalHeight: viewport.logicalHeight,
        });
      uiRenderer?.dispose();
      uiAssets = nextAssets;
      uiRenderer = nextRenderer;
      if (window.sceneSession) window.sceneSession.uiRenderer = uiRenderer;
      window.uiSurfaceBuilds = (window.uiSurfaceBuilds || 0) + 1;
      previousUIPacket = packet;
      return uiAssets;
    };
    try {
      const cursorConfig = {
        locale: config.locale,
        startup: await get('/startup.wasm'),
        orderWasm: await get('/order.wasm'),
        lookupWasm: resourceArgs.lookupWasm,
        decoderWasm: resourceArgs.decoderWasm,
        blpWasm: await get('/ui-blp.wasm'),
      };
      const cursorAssets = await loadUIImages(inventory.archives, cursorConfig, {
        commands: [],
        preloadAssets: ['Interface\\Cursor\\Point.blp'],
      });
      const cursorImage = cursorAssets.images.get('interface/cursor/point.blp');
      if (!cursorImage)
        throw Error('Original cursor unavailable: ' + JSON.stringify(cursorAssets.issues));
      softwareCursor = createSoftwareCursor(gl, canvas, cursorImage, {
        hidden: () => moviePlaying || !!window.uiInputState?.cursor_hidden,
        redraw: () => {
          if (
            !renderer ||
            !uiRenderer ||
            moviePlaying ||
            (liveMode && window.animationStatus?.state === 'running')
          )
            return;
          renderer.draw({
            batch: -1,
            fileCamera: true,
            materialMode: 'scene',
            frontFace: 'ccw',
            passPolicy: 'baseline',
            measurePixels: false,
          });
          uiRenderer.draw();
          softwareCursor.draw();
        },
      });
      window.addEventListener('pagehide', () => softwareCursor.dispose(), { once: true });
      if (startupMode) {
        frameArgs.audio = true;
        frameArgs.audioCatalogue = config.audio.assets;
        audio = await createLocalAudio(
          inventory.archives,
          {
            locale: config.locale,
            startup: await get('/startup.wasm'),
            orderWasm: await get('/order.wasm'),
            lookupWasm: resourceArgs.lookupWasm,
            decoderWasm: resourceArgs.decoderWasm,
            blpWasm: await get('/ui-blp.wasm'),
          },
          config.audio,
        );
      }
      const transition = await runFrameSession(
        Worker,
        frameArgs,
        async (request) => {
          moviePlaying = false;
          movieSurface?.stop();
          moviePlayer?.stop();
          canvas.style.cursor = 'none';
          if (session && renderer && pool) return { ready: true };
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.disable(gl.SCISSOR_TEST);
          gl.clearColor(0, 0, 0, 1);
          gl.clear(gl.COLOR_BUFFER_BIT);
          if (
            !request.requests.some(
              (r) => r.operation === 1 && r.path.includes('UI_MainMenu_Northrend'),
            )
          )
            throw Error('Missing original model request');
          let phase = 'resource-workers';
          try {
            session = await createResourceSession(Worker, resourceArgs);
            phase = 'gpu-upload';
            pool = await uploadWebGLResources(gl, session.payload);
            phase = 'scene-create';
            renderer = createSceneRenderer(
              gl,
              canvas,
              session.scene.payload,
              session.scene.cameraValues,
              pool,
            );
            phase = 'first-draw';
            first = renderer.draw();
            if (first.drawn_triangles <= 0) throw Error('Empty scene draw');
            firstDrawFinished = true;
            return { ready: true };
          } catch (error) {
            throw Error('[' + phase + '] ' + error.message);
          }
        },
        startupMode
          ? async (request, finish, key) => {
              canvas.style.cursor = 'none';
              startupEvents.push({ operation: request.operation, path: request.path });
              window.movieFinish = finish;
              window.movieKey = key;
              if (request.operation === 1) {
                moviePlaying = true;
                audio?.pauseScene();
                try {
                  const playing = await moviePlayer.start(request);
                  if (playing) movieSurface.start();
                } catch (error) {
                  if (!frameSession) throw Error('Cinematic playback: ' + error.message);
                  moviePlaying = false;
                  window.dispatchEvent(
                    new CustomEvent('brwowser-render-error', {
                      detail: 'Cinematic playback: ' + error.message,
                    }),
                  );
                  finish(request.token);
                }
              } else if (request.operation === 2) {
                moviePlaying = false;
                movieSurface.stop();
                moviePlayer.stop();
              } else if (request.operation === 3) moviePlayer.enableSubtitles(request.volume !== 0);
            }
          : undefined,
        startupMode
          ? async (requests, volumes, packet) => {
              if (packet) {
                await prepareUI(packet);
                uiRenderer.draw();
                if (!playbackStarted) playbackStarted = performance.now();
              }
              return audio.drain(requests, volumes);
            }
          : undefined,
        async (state) => {
          if (state.error) {
            window.dispatchEvent(new CustomEvent('brwowser-render-error', { detail: state.error }));
            return;
          }
          moviePlaying = !!state.movie_active;
          canvas.style.cursor = 'none';
          if (!moviePlaying) {
            playbackStarted = performance.now();
            await audio?.startScene();
            window.movieKey = null;
            await prepareUI(state.packet, true);
          }
          window.uiInputState = state;
          window.uiInputRevision = (window.uiInputRevision || 0) + 1;
        },
      );
      if (transition.kind !== 'done') {
        const backend = transition.waits.find((row) => row.error);
        throw Error(transition.message + (backend ? ' | backend: ' + backend.error : ''));
      }
      frameSession = transition.session;
      if (!firstDrawFinished || transition.result.host_result.screen_state !== 1)
        throw Error('Transition committed without first draw');
      if (startupMode) {
        moviePresentation = {
          surface: 'game-canvas',
          decoder: 'browser',
          ...movieSurface.metrics(),
        };
        movieSurface.stop();
        moviePlayer.stop();
        moviePlaying = false;
        window.addEventListener(
          'pagehide',
          () => {
            movieSurface.dispose();
            moviePlayer.dispose();
          },
          { once: true },
        );
        document.querySelector('canvas').style.cursor = 'none';
        window.movieDecoder.hidden = true;
        document.querySelector('canvas').hidden = false;
        window.movieKey = null;
        if (!freshMode) await configStore('write', transition.result.host_result.startup.config);
      }
      await audio?.startScene();
      playbackStarted = performance.now();
      const initialClock = liveMode
        ? sceneClocks(
            Math.max(0, performance.now() - (playbackStarted || performance.now())),
            session.scene.metadata.sequenceDurationMs,
          )
        : { sequenceMs: 15000, globalMs: 15000 };
      const sample = await session.sample(initialClock.sequenceMs, initialClock.globalMs);
      renderer.update(sample);
      const initialSceneDraw = renderer.draw(),
        packet = transition.result.host_result.packet;
      await prepareUI(packet, true);
      const uiDraw = uiRenderer.draw();
      if (frameSession) {
        let acceptedMask = frameArgs.acceptedMask;
        let persistedConfig = transition.result.host_result.startup?.config;
        const performInput = async (action) => {
          if (action.kind === 5) characterPointerDown = true;
          if (action.kind === 7) characterPointerDown = false;
          const inputStarted = performance.now();
          const previousScreen = window.uiInputState?.screen_state ?? 1;
          const state = await frameSession.input(action);
          updateCharacterPreview(state);
          if (action.kind !== 10) window.uiAnimationPaused = false;
          if (state.screen_state === 3 || isCharacterScreen(state)) audio?.pauseScene();
          else if ([3, 4, 5].includes(previousScreen) && state.screen_state === 1) {
            playbackStarted = performance.now();
            await audio?.startScene();
          }
          if (state.fade_only) {
            uiRenderer.setOpacity(state.login_alpha);
            window.uiInputState = {
              ...(window.uiInputState || transition.result.host_result),
              ...state,
            };
            window.uiInputRevision = (window.uiInputRevision || 0) + 1;
            return window.uiInputState;
          }
          if (!state.movie_active) await prepareUI(state.packet, true);
          if (state.world_entry) {
            // World drawing belongs to the animation loop; input acknowledgements
            // must never recursively await another world frame.
          } else if (isCharacterScreen(state)) {
            // The animation loop draws timer updates once, after input settles.
            if (action.kind !== 10) drawCharacter(state);
          } else if (!state.movie_active && action.kind !== 10)
            renderer.draw({
              batch: -1,
              fileCamera: true,
              materialMode: 'scene',
              frontFace: 'ccw',
              passPolicy: 'baseline',
              measurePixels: false,
            });
          state.ui_draw = state.movie_active
            ? null
            : action.kind === 10
              ? { drawn_quads: uiRenderer.commandCount, issues: uiRenderer.issues }
              : uiRenderer.draw();
          state.ui_asset_issues = uiAssets.issues;
          if (state.accepted_mask !== acceptedMask) {
            if ((state.accepted_mask | acceptedMask) !== state.accepted_mask)
              throw Error('Agreement state regressed');
            if (!freshMode) {
              await agreementStore('write', {
                version: 1,
                identity: consentIdentity,
                accepted_mask: state.accepted_mask,
              });
              await configStore('write', state.config);
            }
            acceptedMask = state.accepted_mask;
          }
          if (!freshMode && state.config !== undefined && state.config !== persistedConfig) {
            await configStore('write', state.config);
            persistedConfig = state.config;
          }
          state.input_latency_ms = performance.now() - inputStarted;
          window.uiInputState = state;
          window.uiInputRevision = (window.uiInputRevision || 0) + 1;
          return state;
        };
        let inputQueue = Promise.resolve();
        const applyInput = (action) => {
          const task = inputQueue.then(() => performInput(action));
          inputQueue = task.catch(() => {});
          return task;
        };
        window.uiInput = applyInput;
        if (liveMode) {
          let resizing = false;
          const resize = () => {
            if (resizing) return;
            resizing = true;
            requestAnimationFrame(() => {
              resizing = false;
              viewport = gameViewport(innerWidth, innerHeight, devicePixelRatio);
              applyGameViewport(canvas, viewport);
              applyInput({
                kind: 9,
                width: viewport.logicalWidth,
                height: viewport.logicalHeight,
              }).catch((error) =>
                window.dispatchEvent(
                  new CustomEvent('brwowser-render-error', { detail: error.message }),
                ),
              );
            });
          };
          addEventListener('resize', resize);
          window.addEventListener('pagehide', () => removeEventListener('resize', resize), {
            once: true,
          });
          resize();
        }

        inputConnection = connectCanvasInput(
          canvas,
          applyInput,
          () => {},
          (error) => {
            document.querySelector('pre').hidden = false;
            document.querySelector('pre').textContent = 'UI: ' + error.message;
          },
          () => window.uiInputState?.items || transition.result.host_result.items,
        );
        keyboardConnection = connectCanvasKeyboard(canvas, applyInput, (error) => {
          document.querySelector('pre').hidden = false;
          document.querySelector('pre').textContent = 'UI: ' + error.message;
        });
        window.addEventListener(
          'pagehide',
          () => {
            keyboardConnection.dispose();
            inputConnection.dispose();
            frameSession.close();
          },
          { once: true },
        );
      }
      window.sceneSession = { session, pool, renderer, uiRenderer, audio };
      window.drawSceneAt = async (elapsed, { sound = false } = {}) => {
        const clocks = sceneClocks(elapsed, session.scene.metadata.sequenceDurationMs);
        renderer.update(await session.sample(clocks.sequenceMs, clocks.globalMs));
        const draw = renderer.draw({
          batch: -1,
          fileCamera: true,
          materialMode: 'scene',
          frontFace: 'ccw',
          passPolicy: 'baseline',
          measurePixels: false,
        });
        uiRenderer.draw();
        softwareCursor?.draw();
        if (sound) audio?.frame(clocks.sequenceMs, clocks.globalMs);
        return {
          ...clocks,
          drawn_triangles: draw.drawn_triangles,
          particles: draw.particles,
          particle_batches: draw.particle_batches,
        };
      };
      if (liveMode) {
        let stopped = false,
          lastFrame = 0,
          lastWorldFrame = performance.now(),
          lastUITick = performance.now();
        window.sceneSession.stop = () => {
          stopped = true;
        };
        const animate = async (now) => {
          if (stopped) return;
          if (moviePlaying) {
            requestAnimationFrame(animate);
            return;
          }
          if (now - lastFrame < 1000 / 30) {
            requestAnimationFrame(animate);
            return;
          }
          lastFrame = now;
          try {
            if (
              !window.uiAnimationPaused &&
              (window.uiInputState?.auth_busy ||
                window.uiInputState?.character_busy ||
                isCharacterScreen(window.uiInputState) ||
                (window.uiInputState?.fade_pending ?? transition.result.host_result.fade_pending) ||
                window.uiInputState?.items?.some(
                  (r) => ['CreditsFrame', 'RealmList'].includes(r.name) && r.visible,
                )) &&
              now - lastUITick >=
                ((window.uiInputState?.auth_busy ||
                  (isCharacterScreen(window.uiInputState) && !characterPointerDown) ||
                  window.uiInputState?.items?.some((r) => r.name === 'RealmList' && r.visible)) &&
                !window.uiInputState?.fade_pending
                  ? 250
                  : 1000 / 30)
            ) {
              const elapsed = Math.min(0.25, (now - lastUITick) / 1000);
              lastUITick = now;
              try {
                await window.uiInput({
                  kind: 10,
                  elapsed,
                  compact: previousUIPacket?.surfaceAlpha !== undefined,
                });
              } catch (error) {
                window.uiAnimationPaused = true;
                window.dispatchEvent(
                  new CustomEvent('brwowser-render-error', { detail: error.message }),
                );
              }
            } else if (!(
              window.uiInputState?.fade_pending ?? transition.result.host_result.fade_pending
            ))
              lastUITick = now;
            const creditsScreen = window.uiInputState?.screen_state === 3;
            const clocks = sceneClocks(
              performance.now() - playbackStarted,
              session.scene.metadata.sequenceDurationMs,
            );
            if (window.uiInputState?.world_entry) {
              if (worldClient) {
                try {
                  const worldDraw = await worldClient.draw(
                    Math.min(0.1, (now - lastWorldFrame) / 1000),
                  );
                  if (worldDraw) window.worldStatus = worldDraw;
                } catch (error) {
                  console.warn('World: ' + error.message);
                  worldClient.dispose();
                  worldClient = null;
                  await window.uiInput({ kind: 14, operation: 'error', message: error.message });
                }
              } else clearScene();
              lastWorldFrame = now;
            } else if (isCharacterScreen(window.uiInputState)) {
              drawCharacter(window.uiInputState);
            } else if (creditsScreen) {
              clearScene();
            } else {
              renderer.update(await session.sample(clocks.sequenceMs, clocks.globalMs));
              renderer.draw({
                batch: -1,
                fileCamera: true,
                materialMode: 'scene',
                frontFace: 'ccw',
                passPolicy: 'baseline',
                measurePixels: false,
              });
            }
            uiRenderer.draw();
            softwareCursor?.draw();
            if (!creditsScreen && !window.uiInputState?.world_entry)
              audio?.frame(clocks.sequenceMs, clocks.globalMs);
            window.animationStatus = {
              state: 'running',
              ...clocks,
              frames: (window.animationStatus?.frames || 0) + 1,
            };
            requestAnimationFrame(animate);
          } catch (error) {
            window.animationStatus = { state: 'error', message: error.message };
            document.querySelector('pre').textContent = 'Animation: ' + error.message;
            document.querySelector('pre').hidden = false;
          }
        };
        requestAnimationFrame(animate);
      }
      return {
        movie_presentation: moviePresentation,
        audio_completed: transition.result.host_result.audio_completed,
        audio: audio?.metrics(),
        startup: transition.result.host_result.startup
          ? {
              initial_screen: transition.result.host_result.startup.initial_screen,
              movie_completions: transition.result.host_result.startup.movie_completions,
              cursor_hidden: transition.result.host_result.startup.cursor_hidden,
              config_saved: !freshMode,
              cinematic_provider: 'automatic-local-ffmpeg-cache',
            }
          : undefined,
        startup_events: startupEvents,
        screen_state: 1,
        completed: session.batch.completed,
        gpu_textures: pool.textureCount,
        gpu_buffers: pool.bufferCount,
        buffer_roles: session.payload.buffers.map((row) => ({ name: row.name, role: row.role })),
        first,
        initial_scene_draw: initialSceneDraw,
        ui_snapshot_objects: transition.result.host_result.items.length,
        ui_draw: uiDraw,
        ui_asset_issues: uiAssets.issues,
        protected_failures: transition.result.protected_failures,
        heartbeat_during_pending: transition.heartbeat_during_pending,
        cross_origin_isolated: crossOriginIsolated,
      };
    } catch (error) {
      disposeCharacter();
      disposeBackground();
      characterSourcePromise?.then((source) => source.dispose()).catch(() => {});
      keyboardConnection?.dispose();
      inputConnection?.dispose();
      await frameSession?.close();
      await audio?.dispose();
      uiRenderer?.dispose();
      uiTextureCache.dispose();
      renderer?.dispose();
      pool?.dispose();
      await session?.close();
      throw error;
    }
  })();
  window.experiment.then(
    (result) => {
      const out = document.querySelector('pre');
      out.textContent = JSON.stringify(result, null, 2);
      out.hidden = liveMode;
    },
    (error) => {
      window.startupFailure = error.message;
      const out = document.querySelector('pre');
      out.hidden = false;
      out.textContent = error.message;
    },
  );
};

const directory = document.querySelector('input'),
  startButton = document.querySelector('#start-startup');
if (startupMode) {
  directory.onchange = () => {};
  startButton.onclick = async () => {
    startButton.disabled = true;
    if (liveMode) {
      startButton.hidden = true;
      directory.hidden = true;
      document.querySelector('pre').hidden = true;
    }
    try {
      const cfg = await json('/config.json');
      if (cfg.localData) {
        launch({ inventory: inventoryFromMount(await json('/local-inventory.json')) });
      } else launch({ target: directory });
    } catch (error) {
      window.startupFailure = error.message;
      document.querySelector('pre').hidden = false;
      document.querySelector('pre').textContent = error.message;
      startButton.disabled = false;
    }
  };
} else directory.onchange = launch;
if (startupMode) {
  json('/config.json').then((cfg) => {
    if (cfg.localData) {
      directory.hidden = true;
      if (liveMode) startButton.click();
      else {
        startButton.textContent = freshMode ? 'Start fresh comparison' : 'Start';
        document.querySelector('pre').textContent = 'Local Data mounted. Click Start.';
      }
    }
  });
}
window.addEventListener(
  'keydown',
  (event) => {
    if (event.altKey && event.key === 'Enter') return;
    const key = { Escape: 'ESCAPE', Enter: 'ENTER', ' ': 'SPACE' }[event.key];
    if (
      (key === 'ESCAPE' || key === 'ENTER') &&
      window.uiInput &&
      window.uiInputState?.items?.some(
        (r) =>
          r.visible &&
          (key === 'ESCAPE' ||
            [
              'GlueDialog',
              'RealmList',
              'CharacterSelect',
              'CharacterCreate',
              'RealmWizard',
            ].includes(r.name)) &&
          [
            'GlueDialog',
            'RealmList',
            'VideoOptionsFrame',
            'AudioOptionsFrame',
            'OptionsSelectFrame',
            'CinematicsFrame',
            'CreditsFrame',
            'CharacterSelect',
            'CharacterCreate',
            'RealmWizard',
          ].includes(r.name),
      )
    ) {
      event.preventDefault();
      event.stopImmediatePropagation();
      window
        .uiInput({ kind: 13, key })
        .catch((error) =>
          window.dispatchEvent(new CustomEvent('brwowser-render-error', { detail: error.message })),
        );
      return;
    }
    if (key && window.movieKey) {
      event.preventDefault();
      event.stopImmediatePropagation();
      const send = window.movieKey;
      send(key);
    }
  },
  { capture: true },
);
window.addEventListener('brwowser-render-error', (event) => {
  const out = document.querySelector('pre');
  out.hidden = false;
  out.textContent = event.detail;
});

window.addEventListener('keydown', (event) => {
  if (event.altKey && event.key === 'Enter') {
    event.preventDefault();
    const target = document.querySelector('canvas');
    const action = document.fullscreenElement
      ? document.exitFullscreen()
      : target.requestFullscreen?.();
    action?.catch((error) =>
      window.dispatchEvent(new CustomEvent('brwowser-render-error', { detail: error.message })),
    );
  }
});
