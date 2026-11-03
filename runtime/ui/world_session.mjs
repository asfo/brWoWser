import { worldLayout } from './world_layout.mjs';
import { frameXMLHash } from './framexml_sources.mjs';
import { luaHost } from '../lua/host.mjs';
import { readDrawList, readRenderPacket } from './draw_bridge.mjs';

// Original FrameXML runs in its own C/Lua VM. Unsupported component lifecycles
// stay deferred explicitly; a failed world UI never destroys the world session.
export async function openWorldUI({
  wasm,
  sources,
  fonts,
  textures,
  compat,
  player,
  labels,
  classes = [],
  settings = [],
  locale,
  width,
  height,
}) {
  let e,
    lua,
    disposed = false;
  const applyUnit = (unit) => {
    if (!unit || unit.values?.length !== 160 || unit.known?.length !== 160) return;
    input(new Uint8Array(new Uint32Array([...unit.values, ...unit.known]).buffer));
    check(e.world_ui_state(1280), 'World UI unit state');
    run(`local h,m=UnitHealth("player"),UnitHealthMax("player");
      if h and m and m>0 then PlayerFrameHealthBar:SetMinMaxValues(0,m);PlayerFrameHealthBar:SetValue(h);PlayerFrameHealthBar:SetStatusBarColor(0,1,0);PlayerFrameHealthBarText:SetText(h.." / "..m) end
      local p,max=UnitPower("player"),UnitPowerMax("player");local kind=UnitPowerType("player");
      if p and max and max>0 then PlayerFrameManaBar:SetMinMaxValues(0,max);PlayerFrameManaBar:SetValue(p);
        local colors={[0]={0,0,1},[1]={1,0,0},[2]={1,.5,.25},[3]={1,1,0},[4]={0,1,1},[5]={.5,.5,.5},[6]={0,.82,1}};local c=colors[kind] or colors[0];
        PlayerFrameManaBar:SetStatusBarColor(c[1],c[2],c[3]);PlayerFrameManaBarText:SetText(p.." / "..max) end
      PlayerLevelText:SetText(UnitLevel("player"));WorldUIRefreshActions();`);
  };
  const encoder = new TextEncoder(),
    decoder = new TextDecoder();
  const text = (pointer) => {
    const b = new Uint8Array(e.memory.buffer, pointer);
    return decoder.decode(b.subarray(0, b.indexOf(0)));
  };
  const wasi = {
    environ_sizes_get(c, s) {
      const v = new DataView(e.memory.buffer);
      v.setUint32(c, 0, true);
      v.setUint32(s, 0, true);
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
    proc_exit(code) {
      throw Error('World UI WASI exit ' + code);
    },
  };
  const instance = (
    await WebAssembly.instantiate(wasm, {
      wasi_snapshot_preview1: wasi,
      platform: {
        screen_wait() {
          throw Error('Glue screen transitions are unavailable in the world UI VM');
        },
      },
    })
  ).instance;
  e = instance.exports;
  e._initialize();
  lua = luaHost(instance);
  const name = (path) => {
    const b = encoder.encode(path);
    if (b.length >= 512) throw Error('World UI name capacity');
    const out = new Uint8Array(e.memory.buffer, e.loader_name_ptr(), 512);
    out.fill(0);
    out.set(b);
  };
  const input = (bytes) => {
    if (bytes.length > 1048576) throw Error('World UI input capacity');
    new Uint8Array(e.memory.buffer, e.frame_input_ptr(), bytes.length).set(bytes);
  };
  const run = (source) => {
    const result = lua.run(source);
    if (result.status) throw Error(result.error);
  };
  const check = (status, message) => {
    if (status) throw Error(message + ': ' + text(e.loader_error()) + ' ' + text(e.frame_error()));
  };
  function dispose() {
    if (disposed) return;
    disposed = true;
    e.frame_close();
    lua.close();
  }
  try {
    lua.init();
    check(e.frame_init(), 'World UI initialization');
    check(e.loader_init(), 'World UI source loader');
    e.frame_defer_lifecycle();
    check(e.frame_viewport(width, height), 'World UI viewport');
    name(locale);
    check(e.glue_init(), 'World UI local settings');
    check(e.world_ui_init(), 'World UI player APIs');
    run(compat);
    for (const row of settings) {
      if (!row.available) continue;
      name(row.name);
      const bytes = encoder.encode(row.value);
      input(bytes);
      check(e.glue_cvar(bytes.length), 'World UI CVar');
    }
    for (const row of classes) {
      for (const [kind, value] of [row.name, row.femaleName || row.name, row.token].entries()) {
        const bytes = encoder.encode(value);
        input(bytes);
        check(e.world_ui_text(16 + row.id * 3 + kind, bytes.length), 'World UI class label');
      }
    }
    check(
      e.world_ui_player(...player.guid, player.race, player.class, player.gender, player.level),
      'World UI player identity',
    );
    for (const [field, value] of [player.name, ...labels, locale].entries()) {
      const bytes = encoder.encode(value);
      input(bytes);
      check(e.world_ui_text(field, bytes.length), 'World UI player text');
    }
    for (const event of sources.events) {
      name(event);
      check(e.frame_define_event(), 'World UI event');
    }
    for (const font of fonts) {
      name(font.path);
      input(font.bytes);
      check(e.frame_font_asset(font.bytes.length), 'World UI font');
    }
    for (const texture of textures) {
      name(texture.path);
      check(e.frame_texture_asset(texture.width, texture.height), 'World UI texture');
    }
    for (const resource of sources.resources) {
      if ((await frameXMLHash(resource.bytes)) !== resource.sha256)
        throw Error('World UI source identity mismatch: ' + resource.path);
      name(resource.path);
      input(resource.bytes);
      check(e.loader_add(resource.bytes.length), 'World UI source');
    }
    check(e.loader_seal_verified(), 'World UI immutable sources');
    for (const entry of sources.entries) {
      e.frame_template_mode(entry.templates ? 1 : 0);
      name(entry.path);
      check(e.loader_load(), entry.path);
    }
    e.frame_template_mode(0);
    run(worldLayout);
    check(e.frame_update_scroll(), 'World UI scroll layout');
    for (const event of ['PLAYER_LOGIN', 'PLAYER_ENTERING_WORLD']) {
      name(event);
      check(e.frame_event(0), 'World UI entry event');
    }
    if (e.frame_protected_failures())
      throw Error('World UI callback failed: ' + text(e.frame_protected_error()));
    return {
      dispose,
      updateUnit: applyUnit,
      updateHUD(hud, records = []) {
        for (const record of records) {
          const path = encoder.encode(record.path + '\0'),
            label = encoder.encode(record.name + '\0'),
            bytes = new Uint8Array(16 + path.length + label.length);
          new Uint32Array(bytes.buffer, 0, 4).set([
            record.id,
            record.kind,
            record.order,
            record.form,
          ]);
          bytes.set(path, 16);
          bytes.set(label, 16 + path.length);
          input(bytes);
          check(e.world_ui_record(bytes.length), 'World HUD metadata');
        }
        const data = new Uint32Array(4251);
        data[0] = hud.revision;
        data[1] = hud.ready;
        data.set(hud.actions, 2);
        data[146] = hud.spells.length;
        data.set(hud.spells, 147);
        data.set(hud.tutorials, 4243);
        input(new Uint8Array(data.buffer));
        check(e.world_ui_hud(data.byteLength), 'World HUD state');
        run('WorldUIRefreshHUD()');
        check(e.frame_update_scroll(), 'World HUD scroll');
      },
      addTextures(textures) {
        for (const texture of textures) {
          name(texture.path);
          check(e.frame_texture_asset(texture.width, texture.height), 'World HUD dynamic texture');
        }
      },
      environment(zone, minutes) {
        e.world_ui_time(minutes);
        run('WorldUIEnvironment(' + JSON.stringify(zone) + ')');
      },
      pointer(action) {
        const status = e.frame_input_action(action.kind, action.x, action.y, action.delta || 0);
        if (status !== 2) check(status, 'World UI input');
        check(e.frame_update_scroll(), 'World UI scroll');
      },
      key(key) {
        if (key === 'Escape') run('TutorialFrame:Hide()');
      },
      snapshot() {
        if (disposed) throw Error('World UI is closed');
        const items = readDrawList(e, { visibleOnly: true });
        return {
          items: items.filter((row) => row.type < 11 || row.type === 14 || row.type === 15),
          packet: readRenderPacket(e, { items, visibleOnly: true }),
          objects: e.frame_count(),
          resources: sources.resources.length,
          tocEntries: sources.toc.length,
          lifecycle: 'selected-hud-active',
          protectedFailures: e.frame_protected_failures(),
          zoom: e.world_ui_zoom(),
          seen: Array.from({ length: 8 }, (_, i) => ({
            word: i,
            value: e.world_ui_seen(i),
          })).flatMap(({ word, value }) =>
            Array.from({ length: 32 }, (_, bit) =>
              value & (1 << bit) ? word * 32 + bit : -1,
            ).filter((i) => i >= 0),
          ),
          rects: Object.fromEntries(
            [
              'PlayerFrame',
              'PlayerPortrait',
              'MicroButtonPortrait',
              'Minimap',
              'MinimapCluster',
              'MainMenuBar',
              'ShapeshiftBarFrame',
              'TutorialFrame',
              'ChatFrame1',
              'ChatFrame1ButtonFrame',
              'ChatFrame1Tab',
              'ChatFrame2Tab',
            ].map((name) => [name, items.find((row) => row.name === name)?.rect]),
          ),
          deferred: ['UIParent', 'PlayerFrame', 'MainMenuBar', 'Minimap'],
          scope:
            'Original selected FrameXML and Lua with C/WASM layout, player resources, action pages and introductory tutorials. General gameplay/window lifecycles and native visual equivalence remain pending.',
        };
      },
      resize(w, h) {
        check(e.frame_viewport(w, h), 'World UI viewport');
      },
    };
  } catch (error) {
    if (!(error instanceof WebAssembly.RuntimeError)) dispose();
    throw error;
  }
}
