import { normalizeLoginFade } from './fade_surface.mjs';
const textPacketCaches = new WeakMap();
// Read C-owned scene descriptors. No XML parsing, visibility decisions or HTML
// layout takes place here; unresolved geometry is returned explicitly.
export function readDrawList(e, { visibleOnly = false } = {}) {
  const decoder = new TextDecoder(),
    text = (pointer) => {
      if (!pointer) return '';
      const bytes = new Uint8Array(e.memory.buffer, pointer),
        end = bytes.indexOf(0);
      if (end < 0) throw Error('unterminated draw descriptor');
      return decoder.decode(bytes.subarray(0, end));
    },
    items = [];
  for (let index = 0; index < e.frame_count(); index++) {
    if (e.frame_draw(index))
      throw Error('C draw snapshot failed at object ' + index + ': ' + text(e.frame_error()));
    const n = (field) => e.frame_draw_number(field),
      s = (field) => text(e.frame_draw_string(field));
    // Preserve Lua's hidden credits prefetch textures without laying them out.
    if (visibleOnly && !n(3) && !s(0).startsWith('CreditsArtCache')) continue;
    if (visibleOnly && n(2) >= 11 && n(28)) {
      const x = n(9),
        y = n(10),
        w = n(11),
        h = n(12),
        cx = n(56),
        cy = n(57),
        cw = n(58),
        ch = n(59);
      if (x + w <= cx || y + h <= cy || x >= cx + cw || y >= cy + ch) continue;
    }
    items.push({
      object: n(0),
      parent: n(1),
      type: n(2),
      visible: !!n(3),
      geometryStatus: n(4),
      geometryError: s(9),
      layer: n(5),
      enabled: !!n(6),
      additive: n(60) === 1,
      pressed: n(61) === 1,
      hovered: n(62) === 1,
      mouseEnabled: n(64) === 1,
      stratum: n(7),
      level: n(8),
      rect: [n(9), n(10), n(11), n(12)],
      alpha: n(13),
      ownAlpha: n(63),
      scale: n(14),
      fontHeight: n(15),
      shadow: [n(16), n(17)],
      backdrop: !!n(18),
      tile: !!n(19),
      outline: n(20),
      htmlLoaded: !!n(27),
      clip: n(28) ? [n(56), n(57), n(58), n(59)] : null,
      edgeSize: n(21),
      tileSize: n(22),
      insets: [n(23), n(24), n(25), n(26)],
      uv: Array.from({ length: 8 }, (_, i) => n(32 + i)),
      color: Array.from({ length: 4 }, (_, i) => n(40 + i)),
      backdropColor: Array.from({ length: 4 }, (_, i) => n(44 + i)),
      borderColor: Array.from({ length: 4 }, (_, i) => n(48 + i)),
      shadowColor: Array.from({ length: 4 }, (_, i) => n(52 + i)),
      name: s(0),
      texture: s(1),
      text: s(2),
      font: s(3),
      fontFlags: s(4),
      justifyH: s(5),
      justifyV: s(6),
      background: s(7),
      edge: s(8),
    });
  }
  return items;
}

// Pixel coverage and all glyph positions are supplied by C/FreeType. This host
// only copies immutable device payloads out of WASM before the VM can close.
export function textWithShadow(quads, item) {
  const [dx, dy] = item.shadow || [0, 0];
  if ((!dx && !dy) || !item.shadowColor?.[3]) return quads;
  return [
    ...quads.map((q) => ({
      ...q,
      rect: [q.rect[0] + dx, q.rect[1] + dy, q.rect[2], q.rect[3]],
      color: item.shadowColor,
    })),
    ...quads,
  ];
}
export function readTextQuads(e, item) {
  const quads = [];
  for (let ordinal = 0; ordinal < 16384; ordinal++) {
    const status = e.frame_text_quad(item.object, ordinal);
    if (status === 2) return textWithShadow(quads, item);
    if (status) {
      const raw = new Uint8Array(e.memory.buffer, e.frame_error()),
        end = raw.indexOf(0),
        detail = new TextDecoder().decode(raw.subarray(0, end < 0 ? raw.length : end));
      throw Error('C text quad contract rejected ' + item.name + ': ' + detail);
    }
    const n = (field) => e.frame_text_quad_number(field),
      width = n(1),
      height = n(2),
      coverage = new Uint8Array(
        e.memory.buffer,
        e.frame_glyph_pixels_ptr(),
        width * height,
      ).slice();
    quads.push({
      codepoint: n(0),
      rect: [n(3), n(4), width, height],
      color: [n(5), n(6), n(7), n(8)],
      coverage,
    });
  }
  throw Error('text quad capacity');
}
export function readRenderPacket(
  e,
  { cacheText = true, items = readDrawList(e), visibleOnly = false } = {},
) {
  let textCache = textPacketCaches.get(e);
  if (!textCache) {
    textCache = new Map();
    textPacketCaches.set(e, textCache);
  }
  const byObject = new Map(items.map((item) => [item.object, item]));
  const count = e.frame_draw_begin() >>> 0;
  if (count === 0xffffffff || count > (visibleOnly ? e.frame_count() : items.length))
    throw Error('C draw order rejected');
  const commands = [],
    issues = [];
  for (let ordinal = 0; ordinal < count; ordinal++) {
    const item = byObject.get(e.frame_draw_index(ordinal));
    if (!item) {
      if (visibleOnly) continue;
      throw Error('invalid C draw index');
    }
    if (item.type === 7) {
      if (item.htmlLoaded) continue;
      issues.push({
        object: item.object,
        name: item.name,
        kind: 'simple-html',
        message: 'Native SimpleHTML content/layout not reconstructed',
      });
      continue;
    }
    if (item.geometryStatus) {
      issues.push({
        object: item.object,
        name: item.name,
        kind: 'geometry',
        status: item.geometryStatus,
        message: item.geometryError,
      });
      continue;
    }
    // C still owns layout and clip. Avoid rasterizing legal paragraphs that are
    // entirely outside that clip; scrolling rebuilds the packet at its new offset.
    if (item.clip) {
      const [x, y, w, h] = item.rect,
        [cx, cy, cw, ch] = item.clip;
      if (x + w <= cx || y + h <= cy || x >= cx + cw || y >= cy + ch) continue;
    }
    if (item.type === 12) {
      try {
        let quads;
        const cacheable = cacheText && byObject.get(item.parent)?.type !== 4;
        const key = JSON.stringify([
          item.text,
          item.font,
          item.fontHeight,
          item.fontFlags,
          item.rect.slice(2),
          item.justifyH,
          item.justifyV,
          item.outline,
          item.color,
          item.shadow,
          item.shadowColor,
        ]);
        const previous = cacheable ? textCache.get(item.object) : null;
        if (previous?.key === key) {
          const dx = item.rect[0] - previous.origin[0],
            dy = item.rect[1] - previous.origin[1];
          quads = previous.quads.map((q) => ({
            ...q,
            rect: [q.rect[0] + dx, q.rect[1] + dy, q.rect[2], q.rect[3]],
          }));
        } else {
          quads = readTextQuads(e, item);
          if (cacheable) textCache.set(item.object, { key, origin: item.rect.slice(0, 2), quads });
        }
        commands.push({ kind: 'text', ...item, quads });
      } catch (error) {
        issues.push({ object: item.object, name: item.name, kind: 'text', message: error.message });
      }
    } else {
      const quads = [];
      let rejected = false;
      for (let part = 0; part < 10; part++) {
        const status = e.frame_image_quad(item.object, part);
        if (status === 2) break;
        if (status === 3) continue;
        if (status) {
          issues.push({ object: item.object, name: item.name, kind: 'image-quad-rejected' });
          rejected = true;
          break;
        }
        const n = (f) => e.frame_image_quad_number(f),
          p = e.frame_image_quad_asset(),
          bytes = new Uint8Array(e.memory.buffer, p),
          asset = new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0)));
        quads.push({
          asset,
          rect: [n(0), n(1), n(2), n(3)],
          uv: Array.from({ length: 8 }, (_, i) => n(4 + i)),
          color: Array.from({ length: 4 }, (_, i) => n(12 + i)),
          vertexColors: n(16) === 1 ? Array.from({ length: 16 }, (_, i) => n(17 + i)) : null,
          desaturated: n(33) === 1,
        });
      }
      if (!rejected) commands.push({ kind: 'images', ...item, quads });
    }
  }
  const preloadAssets = [
    ...new Set(
      items
        .filter(
          (r) =>
            r.type === 11 &&
            r.texture &&
            ((byObject.get(r.parent)?.visible && [2, 3].includes(byObject.get(r.parent)?.type)) ||
              (r.name.startsWith('CreditsArtCache') &&
                items.some((item) => item.name === 'CreditsFrame' && item.visible))),
        )
        .map((r) => r.texture),
    ),
  ];
  return normalizeLoginFade(
    {
      commands,
      issues,
      preloadAssets,
      scope:
        'C sorted geometry/text payload; public FreeType baseline, selected normal line layout; unsupported geometry/inline media/ellipsis explicit. Native clipping, baseline/pixels and native border-pixel equivalence remain separate.',
    },
    items,
  );
}
