import { importRealmList } from '../../runtime/ui/realm_host.mjs';
import {
  importCharacterMetadata,
  importCharacterRoster,
  characterPreview,
  readCharacterCreate,
} from '../../runtime/ui/character_host.mjs';
import { authenticateAccount } from '../../runtime/network/auth_session.mjs';
import { listen, send } from '../../engine/workers/environment.mjs';
import { loadOriginalGlue, openOriginalGlue } from '../../engine/glue/engine.mjs';
import { readDrawList, readRenderPacket } from '../../runtime/ui/draw_bridge.mjs';
import { applyConfigBytes, serializeConfig } from '../../runtime/config/host.mjs';
import { waitForCompletion } from '../../runtime/io/completion_wait.mjs';
let movieReply, movieKey, audioReply, liveSession, inputHandler, closeAuthentication;
listen(async (config) => {
  if (config.kind === 'ui-close') {
    try {
      closeAuthentication?.();
      await liveSession?.close();
      send({ kind: 'ui-closed' });
    } catch (error) {
      send({ kind: 'error', message: error.message });
    }
    return;
  }
  if (config.kind === 'ui-input') {
    let recovery;
    try {
      if (!liveSession || !inputHandler) throw Error('Input session not ready');
      const result = await liveSession.dispatch(
        async (api) => {
          const before = api.lua.run('return AccountLogin:IsShown() and 1 or 0');
          const wasLogin = !before.status && Number(api.lua.result(1)) === 1;
          try {
            return await inputHandler(config.action);
          } catch (error) {
            // An unsupported screen callback can hide the supported login before failing.
            // Restore that screen through original OnShow, preserving input only in Lua.
            const shown = api.lua.run('return AccountLogin:IsShown() and 1 or 0');
            if (
              wasLogin &&
              api.e.glue_screen_state() === 1 &&
              !shown.status &&
              Number(api.lua.result(1)) === 0
            ) {
              const restored = api.lua.run(
                'local account=AccountLoginAccountEdit:GetText();local password=AccountLoginPasswordEdit:GetText();for _,name in pairs(GlueScreenInfo) do local frame=_G[name];if frame and frame~=AccountLogin then pcall(frame.Hide,frame) end end;AccountLogin:Show();AccountLoginAccountEdit:SetText(account);AccountLoginPasswordEdit:SetText(password)',
              );
              if (!restored.status)
                try {
                  recovery = await inputHandler({ kind: 10, elapsed: 0 });
                } catch {}
            }
            throw error;
          }
        },
        { recoverable: true },
      );
      send({ kind: 'ui-result', id: config.id, result });
    } catch (error) {
      send({ kind: 'ui-result', id: config.id, error: error.message, recovery });
    }
    return;
  }
  if (config.kind === 'audio-ack') {
    audioReply?.(config.acknowledgements);
    return;
  }
  if (config.kind === 'movie-key') {
    movieKey?.(config.key);
    return;
  }
  if (config.kind === 'movie-finished') {
    movieReply?.(config.token);
    return;
  }
  if (config.kind !== 'init') return;
  const control = new Int32Array(config.shared);
  let context,
    token = 0,
    lastState = 0;
  try {
    const result = await (config.interactive ? openOriginalGlue : loadOriginalGlue)(
      config.wasm,
      config.files,
      config.fixture,
      {
        beforeLoad: async (api) => {
          const modelKey = (path) => path.replaceAll('/', '\\').toLowerCase();
          const admittedModels = new Set((config.fixture.models || []).map(modelKey));
          for (const path of config.characterMetadata?.models || []) {
            const key = modelKey(path);
            if (admittedModels.has(key)) continue;
            if (admittedModels.size >= 64)
              throw Error('Character model admission capacity exceeded: ' + path);
            api.name(path);
            if (api.e.frame_model_asset()) throw Error('Character model admission failed: ' + path);
            admittedModels.add(key);
          }
          importCharacterMetadata(api, config.characterMetadata);
          if (config.authNetwork && api.e.glue_auth_enable())
            throw Error('Authentication profile failed');
          if (api.e.glue_options_readonly_profile()) throw Error('read-only options profile');
          if (
            config.viewport &&
            api.e.frame_viewport(config.viewport.width, config.viewport.height)
          )
            throw Error('viewport profile');
          for (const category of config.realmCategories || []) {
            api.name(category.label);
            if (api.e.glue_realm_category(category.id, category.flags))
              throw Error('Realm category import failed');
          }
          for (const font of config.fontAssets || []) {
            api.name(font.name);
            api.input(font.bytes);
            if (api.e.frame_font_asset(font.bytes.byteLength))
              throw Error('Edit font admission failed');
          }
          for (const asset of config.htmlAssets || []) {
            api.name(asset.name);
            api.input(asset.bytes);
            if (api.e.frame_html_asset(asset.bytes.byteLength))
              throw Error('Legal document admission failed: ' + asset.name);
          }
          if (config.startup) {
            if (api.e.glue_startup_profile() || api.e.glue_movie_profile())
              throw Error('startup profile');
            if (config.startup.configBytes)
              applyConfigBytes(api.e, new Uint8Array(config.startup.configBytes));
            for (const path of new Set([
              config.startup.path,
              ...(config.startup.movies || []).map((row) => row.logical_path),
            ])) {
              api.name(path);
              if (api.e.frame_movie_register()) throw Error('movie resource admission');
            }
          }
        },
        platform: {
          screen_wait() {
            const { e } = context,
              requests = [],
              text = (pointer) => {
                const bytes = new Uint8Array(e.memory.buffer, pointer),
                  end = bytes.indexOf(0);
                return new TextDecoder().decode(bytes.subarray(0, end));
              };
            while (e.frame_model_next() === 0)
              requests.push({
                owner: e.frame_model_owner(),
                operation: e.frame_model_kind(),
                value: e.frame_model_value(),
                path: text(e.frame_model_path()),
              });
            const id = ++token;
            Atomics.store(control, 0, 0);
            Atomics.store(control, 3, 1);
            send({ kind: 'screen-wait', token: id, requests, items: readDrawList(e) });
            try {
              waitForCompletion(control, id, 60000);
              return 0;
            } catch (error) {
              send({ kind: 'screen-wait-failed', message: error.message });
              return 1;
            } finally {
              Atomics.store(control, 3, 0);
            }
          },
        },
        afterLoad: async (api) => {
          context = api;
          const { e, lua, name, input } = api;
          if (config.viewport && e.frame_viewport(config.viewport.width, config.viewport.height))
            throw Error('Settled viewport profile');
          const drainMovies = () => {
            const text = (p) => {
              const b = new Uint8Array(e.memory.buffer, p);
              return new TextDecoder().decode(b.subarray(0, b.indexOf(0)));
            };
            while (e.frame_movie_next() === 0)
              send({
                kind: 'movie-request',
                request: {
                  cursor_hidden: e.frame_cursor_hidden(),
                  token: e.frame_movie_token(),
                  operation: e.frame_movie_operation(),
                  path: text(e.frame_movie_path()),
                  volume: e.frame_movie_volume(),
                },
              });
          };
          if (e.glue_agreements(config.acceptedMask || 0, 31 & ~(config.acceptedMask || 0)))
            throw Error('agreement profile');
          name('');
          if (e.glue_offline_realm()) throw Error('offline realm profile');
          for (const [channel, id, kit] of config.audioCatalogue
            ? config.audioCatalogue.map((r) => [r.channel, r.id, r.kit])
            : [
                [0, 12765, 'GS_LichKing'],
                [1, 9902, 'GlueScreenIntro'],
                [2, 850, 'igMainMenuOpen'],
                ...(config.interactive ? [[2, 851, 'igMainMenuClose']] : []),
              ]) {
            name(kit);
            if (e.glue_audio_asset(channel, id)) throw Error('audio catalogue');
          }
          if (config.interactive) {
            if (e.glue_warning_profile()) throw Error('Fresh changed-option warning profile');
            if (e.glue_system_profile(4)) throw Error('Portable system capability profile');
            if (e.glue_scan_profile()) throw Error('Scanner browser profile');
            for (const [i, key] of [
              'readTOS',
              'readEULA',
              'readTerminationWithoutNotice',
              'readScanning',
              'readContest',
            ].entries()) {
              name(key);
              input(new TextEncoder().encode((config.acceptedMask || 0) & (1 << i) ? '1' : '0'));
              if (e.glue_cvar(1) || e.glue_agreement_profile(i, 0))
                throw Error('Browser agreement storage profile');
            }
          }
          for (const asset of config.uiAssets) {
            name(asset.name);
            if (e.frame_texture_asset(asset.width, asset.height))
              throw Error('UI texture registration');
          }
          name('FRAMES_LOADED');
          if (e.frame_event(0)) {
            const bytes = new Uint8Array(e.memory.buffer, e.frame_error());
            throw Error(
              'FRAMES_LOADED failed: ' +
                new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0))),
            );
          }
          if (e.glue_neutral_gamma()) throw Error('gamma profile');
          if (e.glue_renderer_profile()) throw Error('renderer capability profile');
          if (e.glue_options_readonly_profile()) throw Error('read-only options profile');
          if (e.glue_screen_backend()) throw Error('screen backend registration');
          let initialScreen = 'login',
            movieCompletions = 0;
          if (config.startup) {
            const decision = e.glue_startup_screen(config.startup.nativeFlag ?? 2);
            if (!decision) throw Error('startup decision');
            initialScreen = decision === 2 ? 'movie' : 'login';
          }
          const argument = lua.run('return ' + JSON.stringify(initialScreen));
          if (argument.status) throw Error(argument.error);
          name('SET_GLUE_SCREEN');
          const status = e.frame_event(1);
          lastState = e.glue_screen_state();
          if (status)
            throw Error(
              new TextDecoder().decode(
                new Uint8Array(e.memory.buffer, e.frame_error()).subarray(
                  0,
                  new Uint8Array(e.memory.buffer, e.frame_error()).indexOf(0),
                ),
              ),
            );
          if (config.startup && e.glue_screen_state() === 2) {
            await new Promise((resolve, reject) => {
              const text = (p) => {
                const b = new Uint8Array(e.memory.buffer, p);
                return new TextDecoder().decode(b.subarray(0, b.indexOf(0)));
              };
              const drain = () => {
                while (e.frame_movie_next() === 0)
                  send({
                    kind: 'movie-request',
                    request: {
                      cursor_hidden: e.frame_cursor_hidden(),
                      token: e.frame_movie_token(),
                      operation: e.frame_movie_operation(),
                      path: text(e.frame_movie_path()),
                      volume: e.frame_movie_volume(),
                    },
                  });
              };
              movieKey = (key) => {
                try {
                  if (!['SPACE', 'ENTER', 'ESCAPE'].includes(key))
                    throw Error('unsupported movie key');
                  const result = lua.run(
                    'MovieFrame_OnKeyUp(MovieFrame,' + JSON.stringify(key) + ')',
                  );
                  if (result.status) throw Error(result.error);
                  drain();
                  if (e.glue_screen_state() === 1) {
                    movieReply = movieKey = null;
                    resolve();
                  }
                } catch (error) {
                  reject(error);
                }
              };
              movieReply = (token) => {
                try {
                  const status = e.frame_movie_complete(token);
                  if (status) throw Error('movie completion rejected ' + status);
                  movieCompletions++;
                  drain();
                  if (e.glue_screen_state() === 1) {
                    movieReply = movieKey = null;
                    resolve();
                  }
                } catch (error) {
                  reject(error);
                }
              };
              drain();
            });
          }
          if (e.glue_screen_state() !== 1) throw Error('screen did not commit');
          if (e.frame_update_scroll()) {
            const b = new Uint8Array(e.memory.buffer, e.frame_error());
            throw Error(
              'C scroll layout update failed: ' +
                new TextDecoder().decode(b.subarray(0, b.indexOf(0))),
            );
          }
          let scannerUnavailableEvents = 0;
          const finishUnavailableScan = () => {
            if (e.glue_scan_pending()) {
              if (e.glue_scan_unavailable()) throw Error('Scanner unavailable transition rejected');
              name('SCANDLL_ERROR');
              if (e.frame_event(0)) {
                const bytes = new Uint8Array(e.memory.buffer, e.frame_error());
                throw Error(
                  'Original scanner error event failed: ' +
                    new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0))),
                );
              }
              scannerUnavailableEvents++;
            }
          };
          finishUnavailableScan();
          const fadeState = () => {
            const state = lua.run('return #FADEFRAMES,AccountLoginUI:GetAlpha()');
            if (state.status) throw Error(state.error);
            return { fade_pending: Number(lua.result(1)), login_alpha: Number(lua.result(2)) };
          };
          const tickFades = (elapsed) => {
            if (!fadeState().fade_pending) return;
            const tick = lua.run(
              'local handler=GlueParent:GetScript("OnUpdate");if handler then handler(GlueParent,' +
                JSON.stringify(elapsed) +
                ') end',
            );
            if (tick.status) throw Error(tick.error);
          };
          tickFades(0);
          let audioCompleted = 0;
          const flushAudio = async (initial = false) => {
            if (config.audio) {
              const requests = [];
              while (e.glue_audio_next() === 0)
                requests.push({
                  token: e.glue_audio_token(),
                  operation: e.glue_audio_operation(),
                  channel: e.glue_audio_channel(),
                  asset: e.glue_audio_id(),
                  fade_seconds: e.glue_audio_fade(),
                });
              if (!requests.length) return;
              await new Promise((resolve, reject) => {
                audioReply = (rows) => {
                  try {
                    if (!Array.isArray(rows) || rows.length !== requests.length)
                      throw Error('Audio completion count');
                    for (let i = 0; i < rows.length; i++) {
                      if (
                        rows[i].token !== requests[i].token ||
                        ![0, 1].includes(rows[i].status) ||
                        e.glue_audio_ack(rows[i].token, rows[i].status)
                      )
                        throw Error('Audio completion rejected');
                      if (rows[i].status) throw Error('Audio provider failed');
                      audioCompleted++;
                    }
                    audioReply = null;
                    resolve();
                  } catch (error) {
                    reject(error);
                  }
                };
                const query = lua.run(
                  'return GetCVar("Sound_MasterVolume"),GetCVar("Sound_MusicVolume"),GetCVar("Sound_AmbienceVolume"),GetCVar("Sound_SFXVolume")',
                );
                if (query.status) throw Error(query.error);
                const volumes = Array.from({ length: 4 }, (_, i) => Number(lua.result(i + 1)));
                if (volumes.some((v) => !Number.isFinite(v) || v < 0 || v > 1))
                  throw Error('Unsupported audio volume profile');
                const flags = lua.run(
                  'return GetCVarBool("Sound_EnableAllSound") and 1 or 0,GetCVarBool("Sound_EnableMusic") and 1 or 0,GetCVarBool("Sound_EnableAmbience") and 1 or 0,GetCVarBool("Sound_EnableSFX") and 1 or 0',
                );
                if (flags.status) throw Error(flags.error);
                for (let i = 0; i < 4; i++) volumes[i] *= Number(lua.result(i + 1));
                send({
                  kind: 'audio-batch',
                  requests,
                  volumes,
                  packet: initial && !config.omitRenderPacket ? readRenderPacket(e) : null,
                });
              });
            }
          };
          await flushAudio(true);

          const characterModels = [{}, {}];
          const drainCharacterModels = () => {
            while (e.frame_model_next() === 0) {
              const owner = e.frame_model_owner() + 1;
              const role = [0, 1].find((index) => e.frame_model_role(index) === owner);
              if (role === undefined) continue;
              const model = characterModels[role];
              const operation = e.frame_model_kind();
              if (operation === 1) {
                const bytes = new Uint8Array(e.memory.buffer, e.frame_model_path());
                model.backgroundPath = new TextDecoder().decode(
                  bytes.subarray(0, bytes.indexOf(0)),
                );
              } else if (operation === 2) model.camera = e.frame_model_value();
              else if (operation === 3) model.sequence = e.frame_model_value();
              else if (operation === 4) {
                model.sequence = e.frame_model_value();
                model.sequenceMs = e.frame_model_time();
              } else if (operation === 5) model.scale = e.frame_model_scalar();
            }
          };
          const snapshot = () => {
            drainCharacterModels();
            const preview = characterPreview(e, worldSession?.characters || []);
            if (preview) Object.assign(preview, characterModels[preview.mode === 'create' ? 1 : 0]);
            const items = readDrawList(e, { visibleOnly: config.compactInput });
            const state = lua.run(
              'return (TOSAccepted() and 1 or 0)+(EULAAccepted() and 2 or 0)+(TerminationWithoutNoticeAccepted() and 4 or 0)+(ScanningAccepted() and 8 or 0)+(ContestAccepted() and 16 or 0),TOSFrame.noticeType,TOSAccept:IsEnabled()',
            );
            if (state.status) throw Error(state.error);
            const accepted = Number(lua.result(1)),
              notice = lua.result(2),
              enabled = Number(lua.result(3));
            return {
              movie_active: e.glue_screen_state() === 2,
              auth_busy: !!e.glue_auth_request() || !!e.glue_world_busy() || !!characterJob,
              character_busy: !!characterJob,
              character_preview: preview,
              screen_state: e.glue_screen_state(),
              cursor_hidden: e.frame_cursor_hidden(),
              ...fadeState(),
              accepted_mask: accepted,
              notice,
              accept_enabled: enabled,
              config: serializeConfig(e),
              scanner: { policy: 'unavailable', error_events: scannerUnavailableEvents },
              edit_focus: e.frame_edit_focus(),
              login_form_shown: items.some((r) => r.name === 'AccountLoginUI' && r.visible),
              items: config.compactInput ? items.filter((r) => r.visible) : items,
              packet: config.omitRenderPacket
                ? null
                : readRenderPacket(e, { items, visibleOnly: config.compactInput }),
            };
          };
          let authJob, realmSession, realmRefresh, worldJob, worldSession, characterJob;
          closeAuthentication = () => {
            e.glue_character_connected(0);
            worldJob?.controller.abort();
            worldSession?.close();
            authJob?.controller.abort();
            realmSession?.close();
          };
          const pumpAuthentication = () => {
            const generation = e.glue_auth_request();
            if (authJob && authJob.generation !== generation) {
              authJob.controller.abort();
              authJob = null;
            }
            if (authJob?.done) {
              const job = authJob;
              authJob = null;
              if (!e.glue_auth_finish(job.generation)) {
                if (job.error) {
                  const reply = lua.run(
                    'GlueDialog:Hide();GlueDialog_Show("OKAY",' +
                      JSON.stringify(job.error.message) +
                      ')',
                  );
                  if (reply.status) throw Error(reply.error);
                } else {
                  realmSession?.close();
                  realmSession = job.result;
                  importRealmList(api, job.result.realms);
                  const reply = lua.run(
                    'GlueDialog:Hide();AccountLoginUI:Hide();RealmList.offset=0;RealmList.selectedName=nil',
                  );
                  if (reply.status) throw Error(reply.error);
                  const preference = lua.run('return GetServerName()');
                  if (preference.status) throw Error(preference.error);
                  const selectedRealm = !!lua.result(1),
                    categories = lua.run('return select("#",GetRealmCategories())');
                  if (categories.status) throw Error(categories.error);
                  const categoryCount = Number(lua.result(1));
                  if (selectedRealm && !config.chooseRealmMode) {
                    startWorldConnection();
                  } else if (!selectedRealm && categoryCount > 1) {
                    const wizard = lua.run(
                      'GlueParent_OnEvent("GET_PREFERRED_REALM_INFO",' + categoryCount + ')',
                    );
                    if (wizard.status) throw Error(wizard.error);
                  } else {
                    name('OPEN_REALM_LIST');
                    if (e.frame_event(0)) throw Error('Original realm list event failed');
                  }
                }
              }
              return;
            }
            if (generation && !authJob) {
              realmSession?.close();
              realmSession = null;

              // DefaultServerLogin captures arguments before original Lua clears the EditBox.
              const length = e.glue_auth_credentials();
              if (length < 2 || length > 192)
                throw Error('Authentication credential handoff failed');
              const bytes = new Uint8Array(e.memory.buffer, e.frame_input_ptr(), length);
              let username, password;
              try {
                username = new TextDecoder().decode(bytes.subarray(2, 2 + bytes[0]));
                password = new TextDecoder().decode(bytes.subarray(2 + bytes[0]));
              } finally {
                bytes.fill(0);
              }
              const query = lua.run('return GetLocale()');
              if (query.status) throw Error(query.error);
              const locale = lua.result(1),
                controller = new AbortController();
              const job = { generation, controller, done: false };
              authJob = job;
              authenticateAccount({
                username,
                password,
                locale,
                signal: controller.signal,
                executeWardenLua: (source) => lua.run(source, 100000).status === 0,
                clientImage: config.clientImage,
              }).then(
                (result) => {
                  job.result = result;
                  job.done = true;
                },
                (error) => {
                  job.error = error;
                  job.done = true;
                },
              );
              username = '';
              password = '';
            }
          };
          const startWorldConnection = () => {
            if (worldJob) return;
            worldSession?.close();
            worldSession = null;
            e.glue_character_connected(0);
            if (!realmSession) {
              const reply = lua.run(
                'RealmList:Show();GlueDialog_Show("OKAY","Please log in again before connecting to a realm.")',
              );
              if (reply.status) throw Error(reply.error);
              return;
            }
            const query = lua.run('return GetServerName()');
            if (query.status) throw Error(query.error);
            const realm = lua.result(1);
            if (!realm || e.glue_world_begin())
              throw Error('World connection is already pending or no realm is selected');
            const controller = new AbortController(),
              job = { controller, done: false, status: null, realm };
            worldJob = job;
            const reply = lua.run(
              'RealmList:Hide();GlueDialog_Show("CANCEL","Connecting to game server...")',
            );
            if (reply.status) throw Error(reply.error);
            realmSession
              .connectRealm(realm, {
                signal: controller.signal,
                onStatus(status) {
                  if (job.status?.phase !== status.phase)
                    console.info(
                      'World connection phase: ' + status.phase + ' (' + status.elapsed_ms + ' ms)',
                    );
                  job.status = status;
                  job.statusChanged = true;
                },
              })
              .then(
                (result) => {
                  job.result = result;
                  job.done = true;
                },
                (error) => {
                  job.error = error;
                  job.done = true;
                },
              );
          };
          const worldErrorMessage = (error) => {
            const detail = error?.message || 'World connection closed';
            console.warn('Connection detail: ' + detail);
            if (
              /Warden|world|transport|bridge|connection|timeout|UPSTREAM|WEBSOCKET/i.test(detail)
            ) {
              const original = lua.run('return DISCONNECTED');
              if (!original.status) {
                const text = lua.result(1);
                if (typeof text === 'string' && text) return text;
              }
              return 'You have been disconnected from the server.';
            }
            return detail;
          };
          const pumpWorld = () => {
            if (worldSession?.closed) {
              const error = worldSession.error;
              worldSession = null;
              characterJob = null;
              e.glue_character_connected(0);
              const reply = lua.run(
                'GlueDialog:Hide();SetGlueScreen("login");RealmList:Show();GlueDialog_Show("OKAY",' +
                  JSON.stringify(worldErrorMessage(error)) +
                  ')',
              );
              if (reply.status) throw Error(reply.error);
            }
            if (!worldJob) return;
            if (!e.glue_world_busy()) {
              worldJob.controller.abort();
              worldJob = null;
              const reply = lua.run('GlueDialog:Hide();RealmList:Show()');
              if (reply.status) throw Error(reply.error);
              return;
            }
            if (worldJob.done) {
              const job = worldJob;
              worldJob = null;
              if (!job.error && e.glue_characters_received(job.result.characters.length))
                throw Error('Character count import failed');
              e.glue_world_finish();
              let message;
              if (job.error) message = worldErrorMessage(job.error);
              else {
                worldSession = job.result;
                importCharacterRoster(api, worldSession.characters, config.characterMetadata);
                const reply = lua.run(
                  'GlueDialog:Hide();RealmList:Hide();SetGlueScreen("charselect");UpdateCharacterList()',
                );
                if (reply.status) throw Error(reply.error);
                return;
              }
              const reply = lua.run(
                'GlueDialog:Hide();RealmList:Show();GlueDialog_Show("OKAY",' +
                  JSON.stringify(message) +
                  ')',
              );
              if (reply.status) throw Error(reply.error);
            } else if (worldJob.statusChanged) {
              worldJob.statusChanged = false;
              const status = worldJob.status;
              if (status.phase.startsWith('warden-')) return;
              const message =
                status.phase === 'queue'
                  ? 'Waiting in realm queue. Position: ' + status.position
                  : status.phase === 'characters'
                    ? 'Retrieving character list...'
                    : 'Connecting to game server...';
              if (status.phase !== 'queue') {
                const token =
                  status.phase === 'characters' ? 'CHARACTER_LIST_RETRIEVING' : 'GAME_SERVER_LOGIN';
                const original = lua.run('return ' + token);
                if (!original.status) {
                  const text = lua.result(1);
                  if (typeof text === 'string' && text) {
                    const reply = lua.run('GlueDialog_Show("CANCEL",' + JSON.stringify(text) + ')');
                    if (reply.status) throw Error(reply.error);
                    return;
                  }
                }
              }
              const reply = lua.run('GlueDialog_Show("CANCEL",' + JSON.stringify(message) + ')');
              if (reply.status) throw Error(reply.error);
            }
          };
          const beginCharacterJob = (kind, promise, context = {}) => {
            const job = { ...context, kind, done: false };
            characterJob = job;
            Promise.resolve(promise).then(
              (result) => {
                job.result = result;
                job.done = true;
              },
              (error) => {
                job.error = error;
                job.done = true;
              },
            );
          };
          const pumpCharacters = () => {
            if (characterJob?.done) {
              const job = characterJob;
              characterJob = null;
              if (
                job.kind === 'created-roster' ||
                (job.kind === 'create' && (job.error || !job.result.success))
              )
                e.glue_character_finish();
              if (!worldSession || worldSession.closed) return;
              if (job.error) {
                const reply = lua.run(
                  'GlueDialog:Hide();GlueDialog_Show("OKAY",' +
                    JSON.stringify(worldErrorMessage(job.error)) +
                    ')',
                );
                if (reply.status) throw Error(reply.error);
              } else if (job.kind === 'create') {
                if (job.result.success) {
                  beginCharacterJob('created-roster', worldSession.refreshCharacters(), {
                    previousGuids: job.previousGuids,
                    requestedName: job.requestedName,
                  });
                } else {
                  const reply = lua.run(
                    'GlueDialog:Hide();GlueDialog_Show("OKAY",_G[' +
                      JSON.stringify(job.result.token) +
                      '] or CHAR_CREATE_ERROR)',
                  );
                  if (reply.status) throw Error(reply.error);
                }
              } else {
                importCharacterRoster(api, job.result, config.characterMetadata);
                let createdIndex = 0;
                if (job.kind === 'created-roster') {
                  const added = job.result
                    .map((row, index) => ({ row, index }))
                    .filter(({ row }) => !job.previousGuids.has(row.guid.join(':')));
                  const created =
                    added.length === 1
                      ? added[0]
                      : added.find(
                          ({ row }) => row.name.toLowerCase() === job.requestedName.toLowerCase(),
                        );
                  if (created) createdIndex = created.index + 1;
                }
                const reply = lua.run(
                  job.kind === 'created-roster'
                    ? 'GlueDialog:Hide();SetGlueScreen("charselect");CharacterSelect.selectLast=0;' +
                        (createdIndex
                          ? 'CharacterSelect.selectedIndex=' + createdIndex + ';'
                          : '') +
                        'UpdateCharacterList()'
                    : 'UpdateCharacterList()',
                );
                if (reply.status) throw Error(reply.error);
              }
            }
            const actions = e.glue_character_actions();
            if (actions & 4) {
              worldSession?.close();
              worldSession = null;
              characterJob = null;
              e.glue_character_connected(0);
              if (e.glue_screen_state() !== 1) {
                const reply = lua.run('GlueDialog:Hide();SetGlueScreen("login")');
                if (reply.status) throw Error(reply.error);
              }
              return;
            }
            if (!worldSession || worldSession.closed) return;
            if (actions & 1) {
              const index = e.glue_character_metric(2);
              if (index) worldSession.selectCharacter(worldSession.characters[index - 1].guid);
              const reply = lua.run(
                'CharacterSelect_OnEvent(CharacterSelect,"UPDATE_SELECTED_CHARACTER",' +
                  index +
                  ')',
              );
              if (reply.status) throw Error(reply.error);
            }
            if (!characterJob && actions & 16) {
              const character = readCharacterCreate(api);
              const reply = lua.run('GlueDialog_Show("CANCEL",CHAR_CREATE_IN_PROGRESS)');
              if (reply.status) throw Error(reply.error);
              beginCharacterJob('create', worldSession.createCharacter(character), {
                requestedName: character.name,
                previousGuids: new Set(worldSession.characters.map((row) => row.guid.join(':'))),
              });
            } else if (!characterJob && actions & 2) {
              beginCharacterJob('roster', worldSession.refreshCharacters());
            }
          };
          const pumpRealmActions = () => {
            if (realmRefresh?.done) {
              const update = realmRefresh;
              realmRefresh = null;
              if (update.error) {
                const reply = lua.run(
                  'GlueDialog_Show("OKAY",' + JSON.stringify(update.error.message) + ')',
                );
                if (reply.status) throw Error(reply.error);
              } else {
                importRealmList(api, update.realms);
                const reply = lua.run('RealmListUpdate()');
                if (reply.status) throw Error(reply.error);
              }
            }
            const action = e.glue_realm_action();
            if (action === 1) {
              startWorldConnection();
            } else if (action === 4) {
              startWorldConnection();
            } else if (action === 5) {
              const reply = lua.run('RealmList:Show()');
              if (reply.status) throw Error(reply.error);
            } else if (action === 2) {
              realmSession?.close();
              realmSession = null;
              const reply = lua.run('AccountLoginUI:Show()');
              if (reply.status) throw Error(reply.error);
            } else if (action === 3 && realmSession && !realmRefresh) {
              const update = { done: false };
              realmRefresh = update;
              realmSession.requestRealms().then(
                (realms) => {
                  update.realms = realms;
                  update.done = true;
                },
                (error) => {
                  update.error = error;
                  update.done = true;
                },
              );
            }
          };
          inputHandler = async (action) => {
            if (!action || ![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13].includes(action.kind))
              throw Error('Invalid UI action');
            pumpAuthentication();
            pumpRealmActions();
            pumpWorld();
            pumpCharacters();
            let status;
            if (action.kind === 11) {
              if (!['SPACE', 'ENTER', 'ESCAPE'].includes(action.key))
                throw Error('Invalid movie key');
              const reply = lua.run(
                'MovieFrame_OnKeyUp(MovieFrame,' + JSON.stringify(action.key) + ')',
              );
              if (reply.status) throw Error(reply.error);
              status = 0;
            } else if (action.kind === 13) {
              if (!['ESCAPE', 'ENTER'].includes(action.key)) throw Error('Invalid modal key');
              const reply = lua.run(
                'local key=' +
                  JSON.stringify(action.key) +
                  ';for _,name in ipairs({"GlueDialog","RealmList","VideoOptionsFrame","AudioOptionsFrame","OptionsSelectFrame","CinematicsFrame","CreditsFrame","CharacterCreate","CharacterSelect","RealmWizard"}) do local frame=_G[name];if (key=="ESCAPE" or name=="GlueDialog" or name=="RealmList" or name=="CharacterCreate" or name=="CharacterSelect" or name=="RealmWizard") and frame and frame:IsVisible() then local handler=frame:GetScript("OnKeyDown");if handler then handler(frame,key) end;break end end',
              );
              if (reply.status) throw Error(reply.error);
              status = 0;
            } else if (action.kind === 12) {
              if (!Number.isSafeInteger(action.token) || action.token < 1)
                throw Error('Invalid movie token');
              status = e.frame_movie_complete(action.token);
              if (status) throw Error('Movie completion rejected ' + status);
            } else if (action.kind === 9) {
              if (
                !Number.isFinite(action.width) ||
                !Number.isFinite(action.height) ||
                action.width < 256 ||
                action.width > 8192 ||
                action.height < 256 ||
                action.height > 8192
              )
                throw Error('Invalid UI viewport');
              status = e.frame_viewport(action.width, action.height);
            } else if (action.kind === 10) {
              if (!Number.isFinite(action.elapsed) || action.elapsed < 0 || action.elapsed > 0.25)
                throw Error('Invalid UI elapsed');
              let fast = false;
              if (action.compact) {
                const q = lua.run(
                  'return #FADEFRAMES==1 and FADEFRAMES[1]==AccountLoginUI and AccountLoginUI.mode=="IN" and not AccountLoginUI.finishedFunction',
                );
                if (q.status) throw Error(q.error);
                fast = !!lua.result(1);
              }
              tickFades(action.elapsed);
              const credits = lua.run(
                'if RealmList and RealmList:IsVisible() then local handler=RealmList:GetScript("OnUpdate");if handler then handler(RealmList,' +
                  JSON.stringify(action.elapsed) +
                  ') end end;if CreditsFrame and CreditsFrame:IsVisible() then local update=CreditsFrame:GetScript("OnUpdate");if update then update(CreditsFrame,' +
                  JSON.stringify(action.elapsed) +
                  ') end end',
              );
              if (credits.status) throw Error(credits.error);
              if (e.glue_screen_state() === 4 || e.glue_screen_state() === 5) {
                const characters = lua.run(
                  'for _,name in ipairs({"CharacterSelect","CharacterSelectUI","CharacterSelectRotateLeft","CharacterSelectRotateRight","CharSelectEnterWorldButton","CharacterCreateFrame","CharacterCreateRotateLeft","CharacterCreateRotateRight","CharCreateOkayButton","CharCreateBackButton"}) do local frame=_G[name];if frame and frame:IsVisible() then local handler=frame:GetScript("OnUpdate");if handler then handler(frame,' +
                    JSON.stringify(action.elapsed) +
                    ') end end end',
                );
                if (characters.status) throw Error(characters.error);
              }
              if (fast) return { ...fadeState(), fade_only: true, status: 0 };
              status = 0;
            } else if (action.kind <= 2 || action.kind >= 5) {
              if (![action.x, action.y, action.delta].every(Number.isFinite))
                throw Error('Invalid pointer action');
              status = e.frame_input_action(action.kind, action.x, action.y, action.delta);
            } else if (action.kind === 3) {
              if (typeof action.text !== 'string' || action.text.length > 4096)
                throw Error('Invalid text input');
              const bytes = new TextEncoder().encode(action.text);
              input(bytes);
              status = e.frame_edit_action(1, bytes.length);
            } else {
              if (
                !Number.isInteger(action.operation) ||
                ![2, 3, 4, 5, 6, 7, 8, 9, 10, 11].includes(action.operation)
              )
                throw Error('Invalid key operation');
              status = e.frame_edit_action(action.operation, 0);
            }
            if (status === 1) {
              const bytes = new Uint8Array(e.memory.buffer, e.frame_error());
              throw Error(
                'C UI input rejected: ' +
                  new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0))),
              );
            }
            if (e.frame_edit_pump()) throw Error('C edit update rejected');
            tickFades(0);
            finishUnavailableScan();
            if (e.frame_update_scroll()) {
              const bytes = new Uint8Array(e.memory.buffer, e.frame_error());
              throw Error(
                'C scroll update rejected: ' +
                  new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0))),
              );
            }
            drainMovies();
            pumpAuthentication();
            pumpRealmActions();
            pumpWorld();
            pumpCharacters();
            await flushAudio();
            return {
              ...snapshot(),
              status,
              target: e.frame_input_target(),
              audio_completed: audioCompleted,
            };
          };

          return {
            ...fadeState(),
            audio_completed: audioCompleted,
            startup: config.startup
              ? {
                  initial_screen: initialScreen,
                  movie_completions: movieCompletions,
                  config: serializeConfig(e),
                  cursor_hidden: e.frame_cursor_hidden(),
                }
              : undefined,
            screen: 'login',
            screen_state: e.glue_screen_state(),
            items: readDrawList(e),
            packet: config.omitRenderPacket ? null : readRenderPacket(e),
            pending_audio: e.glue_audio_pending(),
            scope:
              'Original lifecycle events through C/Lua, synchronous imported resource/GPU barrier before selected initial login commit. Fresh EULA unaccepted; input/bootstrap/agreements remain separate.',
          };
        },
      },
    );
    if (config.interactive) {
      liveSession = result;
      send({ kind: 'done', result: result.snapshot, retained: true });
    } else send({ kind: 'done', result });
  } catch (error) {
    send({ kind: 'error', message: error.message, stack: error.stack, screen_state: lastState });
  }
});
