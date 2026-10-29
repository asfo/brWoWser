import { luaHost } from '../../runtime/lua/host.mjs';
const sha = async (b) =>
  Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', b)), (v) =>
    v.toString(16).padStart(2, '0'),
  ).join('');
const expect = (v, m) => {
  if (!v) throw Error(m);
};
export async function loadOriginalGlue(
  wasm,
  files,
  fixture,
  { afterLoad, beforeLoad, platform = {} } = {},
) {
  expect(files.length === fixture.file_hashes.length, 'file count');
  const bytes = await Promise.all(Array.from(files, (f) => f.arrayBuffer()));
  for (let i = 0; i < bytes.length; i++)
    expect((await sha(bytes[i])) === fixture.file_hashes[i], 'source identity ' + i);
  let e;
  const view = () => new DataView(e.memory.buffer),
    wasi = {
      environ_sizes_get: (c, s) => {
        view().setUint32(c, 0, true);
        view().setUint32(s, 0, true);
        return 0;
      },
      environ_get: () => 0,
      fd_close: () => 8,
      fd_fdstat_get: () => 8,
      fd_fdstat_set_flags: () => 8,
      fd_prestat_get: () => 8,
      fd_prestat_dir_name: () => 8,
      fd_read: () => 8,
      fd_seek: () => 8,
      path_open: () => 76,
      fd_write: () => 8,
      proc_exit: (c) => {
        throw Error('WASI exit ' + c);
      },
    };
  const { instance } = await WebAssembly.instantiate(wasm, {
    wasi_snapshot_preview1: wasi,
    platform,
  });
  e = instance.exports;
  e._initialize();
  const lua = luaHost(instance),
    encoder = new TextEncoder(),
    decoder = new TextDecoder();
  lua.init();
  const text = (p) => {
    const b = new Uint8Array(e.memory.buffer, p);
    return decoder.decode(b.subarray(0, b.indexOf(0)));
  };
  const name = (v) => {
    const b = encoder.encode(v);
    expect(b.length < 512, 'name capacity');
    const out = new Uint8Array(e.memory.buffer, e.loader_name_ptr(), 512);
    out.fill(0);
    out.set(b);
  };
  const input = (b) => {
    expect(b.byteLength < 1048576, 'input capacity');
    new Uint8Array(e.memory.buffer, e.frame_input_ptr(), b.byteLength).set(new Uint8Array(b));
  };
  const run = (v) => {
    const r = lua.run(v);
    expect(r.status === 0, r.error);
  };
  const loads = [];
  let trapped = false;
  let phase = 'runtime initialization';
  try {
    expect(
      e.frame_init() === 0 && e.loader_init() === 0 && e.frame_viewport(1024, 768) === 0,
      'runtime init',
    );
    for (const event of fixture.events) {
      name(event);
      expect(e.frame_define_event() === 0, 'event registration');
    }
    run(new Uint8Array(bytes[fixture.compat]));
    name(fixture.locale || 'esES');
    expect(e.glue_init() === 0 && e.glue_fresh_profile() === 0, 'profile init');
    expect(
      e.glue_expansion(Number(fixture.profile.metadata.GetClientExpansionLevel.split(':')[1])) ===
        0,
      'expansion',
    );
    for (const r of fixture.profile.defaults) {
      if (!r.available || r.name === 'showToolsUI') continue;
      name(r.name);
      const b = encoder.encode(r.value);
      input(b);
      expect(e.glue_cvar(b.length) === 0, 'CVar ' + r.name);
    }
    expect(
      e.glue_video(1024, 768, 0, 32, 24, 1) === 0 &&
        e.glue_video_caps(0, 1, 0) === 0 &&
        e.glue_processors(1) === 0,
      'controlled display fixture',
    );
    const output = encoder.encode('System Default');
    input(output);
    expect(e.glue_audio_output(output.length) === 0, 'default audio profile');
    name('Fonts\\FRIZQT__.TTF');
    input(bytes[fixture.font]);
    expect(e.frame_font_asset(bytes[fixture.font].byteLength) === 0, 'font registration');
    for (const model of fixture.models) {
      name(model);
      expect(e.frame_model_asset() === 0, 'model identity registration');
    }
    if (beforeLoad) await beforeLoad({ e, lua, name, input, run });
    for (const r of fixture.resources) {
      expect((await sha(bytes[r.index])) === r.sha256, 'resource identity');
      name(r.name);
      input(bytes[r.index]);
      expect(e.loader_add(bytes[r.index].byteLength) === 0, text(e.loader_error()));
    }
    expect(e.loader_seal_verified() === 0, 'verified resource seal');
    for (const path of fixture.order) {
      phase = path;
      name(path);
      const status = e.loader_load();
      loads.push({ name: path, status, objects: e.frame_count() });
      expect(status === 0, text(e.loader_error()));
    }
    expect(
      e.frame_protected_failures() === 0,
      'swallowed callback failure: ' + text(e.frame_protected_error()),
    );
    phase = 'original Glue lifecycle';
    const host_result = afterLoad ? await afterLoad({ e, lua, name, input, run }) : undefined;
    expect(
      e.frame_protected_failures() === 0,
      'swallowed host lifecycle failure: ' + text(e.frame_protected_error()),
    );
    return {
      host_result,
      completed: loads.length,
      resources: fixture.resources.length,
      objects: e.frame_count(),
      protected_failures: e.frame_protected_failures(),
      loads,
      wasm_sha256: await sha(wasm),
      profile_scope:
        'Closed verified original-source set; single logical canvas/worker/default audio output, minimal advertised graphics features. No full taint or detected hardware profile.',
      scope:
        'Actual C parser/composition, Lua and frame initialization on original local files. No FRAMES_LOADED, settled visibility/layout, renderer, intro, authentication or complete login.',
    };
  } catch (error) {
    trapped = error instanceof WebAssembly.RuntimeError;
    if (trapped) error.message = `WASM trap during ${phase}: ${error.message}`;
    throw error;
  } finally {
    // A trap can leave the C stack unusable. Reentering it for cleanup can
    // hide the original error; discard the failed worker/instance instead.
    if (!trapped) {
      e.frame_close();
      lua.close();
      expect(lua.heapBytes() === 0, 'VM heap cleanup');
    }
  }
}

// Retain the same initialized C/Lua VM until the owner explicitly closes it.
// Operations are serialized. Default callers fail closed; interactive browser
// input opts into reporting an action error while keeping later input usable.
export async function openOriginalGlue(wasm, files, fixture, options = {}) {
  let release,
    readyResolve,
    readyReject,
    api,
    closed = false,
    failure = null,
    queue = Promise.resolve();
  const held = new Promise((resolve) => {
    release = resolve;
  });
  const ready = new Promise((resolve, reject) => {
    readyResolve = resolve;
    readyReject = reject;
  });
  const completion = loadOriginalGlue(wasm, files, fixture, {
    ...options,
    afterLoad: async (context) => {
      api = context;
      const initial = options.afterLoad ? await options.afterLoad(context) : undefined;
      readyResolve(initial);
      await held;
      return initial;
    },
  });
  // Attach immediately: initialization can reject before the readiness consumer.
  completion.catch((error) => {
    failure = error;
    readyReject(error);
  });
  const initial = await ready;
  const dispatch = (operation, { recoverable = false } = {}) => {
    if (closed) return Promise.reject(Error('frame session closed'));
    const result = queue.then(async () => {
      if (failure) throw failure;
      const failuresBefore = api.e.frame_protected_failures();
      try {
        const value = await operation(api);
        if (api.e.frame_protected_failures() > failuresBefore) {
          const bytes = new Uint8Array(api.e.memory.buffer, api.e.frame_protected_error()),
            detail = new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0)));
          throw Error('protected callback failed during frame session: ' + detail);
        }
        return value;
      } catch (error) {
        if (!recoverable) failure = error;
        throw error;
      }
    });
    queue = result.catch(() => {});
    return result;
  };
  return {
    initial,
    snapshot: {
      host_result: initial,
      completed: fixture.order.length,
      resources: fixture.resources.length,
      objects: api.e.frame_count(),
      protected_failures: api.e.frame_protected_failures(),
      wasm_sha256: await sha(wasm),
    },
    dispatch,
    async close() {
      if (!closed) {
        closed = true;
        await queue;
        release();
      }
      return completion;
    },
  };
}
