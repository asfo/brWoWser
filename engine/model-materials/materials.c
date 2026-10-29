#include <stdint.h>
static uint8_t model[8388608], skin[262144];
// row: subset, material, flags, mode, layer, shader-word, stage-start, stage-count, weight-index
static uint32_t rows[256 * 9], stages[512 * 3], info[3];
static float appearance[256 * 4];
static uint32_t draw_order[256];
static float draw_distance[256];
uint32_t order_ptr(void) {
  return (uint32_t)(uintptr_t)draw_order;
}
uint32_t appearance_ptr(void) {
  return (uint32_t)(uintptr_t)appearance;
}
uint32_t model_ptr(void) {
  return (uint32_t)(uintptr_t)model;
}
uint32_t skin_ptr(void) {
  return (uint32_t)(uintptr_t)skin;
}
uint32_t rows_ptr(void) {
  return (uint32_t)(uintptr_t)rows;
}
uint32_t stages_ptr(void) {
  return (uint32_t)(uintptr_t)stages;
}
uint32_t info_ptr(void) {
  return (uint32_t)(uintptr_t)info;
}
static uint32_t u32(uint8_t *b, uint32_t a) {
  return b[a] | ((uint32_t)b[a + 1] << 8) | ((uint32_t)b[a + 2] << 16) | ((uint32_t)b[a + 3] << 24);
}
static uint32_t u16(uint8_t *b, uint32_t a) {
  return b[a] | ((uint32_t)b[a + 1] << 8);
}
static int span(uint32_t n, uint32_t o, uint32_t c, uint32_t stride) {
  return o <= n && c <= (n - o) / stride;
}
static float f32(uint32_t at) {
  union {
    uint32_t u;
    float f;
  } value;
  value.u = u32(model, at);
  return value.f;
}
uint32_t sort_batches(uint32_t sn, float eye_x, float eye_y, float eye_z) {
  uint32_t count = info[0], batches = u32(skin, 40), subsets = u32(skin, 32),
           subset_count = u32(skin, 28);
  if (sn < 48 || sn > sizeof(skin) || count > 256 || !span(sn, batches, count, 24) ||
      !span(sn, subsets, subset_count, 48) || !__builtin_isfinite(eye_x) ||
      !__builtin_isfinite(eye_y) || !__builtin_isfinite(eye_z))
    return 1;
  float eye[3] = {eye_x, eye_y, eye_z};
  for (uint32_t i = 0; i < count; i++) {
    uint32_t subset = rows[i * 9];
    if (subset >= subset_count)
      return 1;
    draw_distance[i] = 0;
    for (uint32_t j = 0; j < 3; j++) {
      union {
        uint32_t u;
        float f;
      } value;
      value.u = u32(skin, subsets + subset * 48 + 32 + j * 4);
      float delta = value.f - eye[j];
      if (!__builtin_isfinite(delta))
        return 1;
      draw_distance[i] += delta * delta;
    }
    uint32_t at = i;
    while (at) {
      uint32_t previous = draw_order[at - 1];
      int blended = rows[i * 9 + 3] >= 2, previous_blended = rows[previous * 9 + 3] >= 2;
      int priority = (int8_t)skin[batches + i * 24 + 1],
          previous_priority = (int8_t)skin[batches + previous * 24 + 1];
      int before = blended != previous_blended     ? !blended
                   : !blended                      ? 0
                   : priority != previous_priority ? priority < previous_priority
                                                   : draw_distance[i] > draw_distance[previous];
      if (!before)
        break;
      draw_order[at] = previous;
      at--;
    }
    draw_order[at] = i;
  }
  return 0;
}
static int appearance_track(uint32_t n, uint32_t at, uint32_t sequence_ms, uint32_t global_ms,
                            uint32_t dimensions, float *out) {
  if (!span(n, at, 1, 20))
    return 1;
  uint32_t mode = u16(model, at), gs = u16(model, at + 2), nt = u32(model, at + 4),
           nv = u32(model, at + 12), ta = u32(model, at + 8), va = u32(model, at + 16);
  if (!nt && !nv)
    return 0;
  if (mode > 1 || !nt || !nv || !span(n, ta, nt, 8) || !span(n, va, nv, 8))
    return 1;
  uint32_t count = u32(model, ta), times = u32(model, ta + 4), values = u32(model, va + 4),
           stride = dimensions == 3 ? 12 : 2, time = sequence_ms;
  if (!count && !u32(model, va))
    return 0;
  if (!count || count > 65536 || count != u32(model, va) || !span(n, times, count, 4) ||
      !span(n, values, count, stride))
    return 1;
  if (gs != 65535) {
    uint32_t gc = u32(model, 20), go = u32(model, 24);
    if (gs >= gc || !span(n, go, gc, 4) || !u32(model, go + gs * 4))
      return 1;
    time = global_ms % u32(model, go + gs * 4);
  }
  uint32_t lo = 0;
  for (uint32_t i = 1; i < count; i++) {
    if (u32(model, times + i * 4) <= u32(model, times + (i - 1) * 4))
      return 1;
    if (u32(model, times + i * 4) <= time)
      lo = i;
  }
  uint32_t hi = lo + 1 < count ? lo + 1 : lo;
  float fraction = mode && hi != lo && time >= u32(model, times + lo * 4)
                       ? (float)(time - u32(model, times + lo * 4)) /
                             (float)(u32(model, times + hi * 4) - u32(model, times + lo * 4))
                       : 0;
  for (uint32_t j = 0; j < dimensions; j++) {
    float a = dimensions == 3 ? f32(values + lo * stride + j * 4)
                              : (float)(int16_t)u16(model, values + lo * stride) / 32767.f;
    float b = dimensions == 3 ? f32(values + hi * stride + j * 4)
                              : (float)(int16_t)u16(model, values + hi * stride) / 32767.f;
    out[j] = a + (b - a) * fraction;
    if (!__builtin_isfinite(out[j]))
      return 1;
  }
  return 0;
}
uint32_t sample_appearance(uint32_t n, uint32_t sn, uint32_t sequence_ms, uint32_t global_ms) {
  if (n < 0x138 || n > sizeof(model) || sn < 48 || sn > sizeof(skin))
    return 1;
  uint32_t cc = u32(model, 72), co = u32(model, 76), wc = u32(model, 88), wo = u32(model, 92),
           bc = u32(skin, 36), bo = u32(skin, 40);
  if (bc != info[0] || !span(n, co, cc, 40) || !span(n, wo, wc, 20) || !span(sn, bo, bc, 24))
    return 2;
  for (uint32_t i = 0; i < bc; i++) {
    float *value = appearance + i * 4;
    for (uint32_t j = 0; j < 4; j++)
      value[j] = 1;
    uint32_t color = u16(skin, bo + i * 24 + 8), weight = rows[i * 9 + 8];
    if (color != 65535) {
      if (color >= cc || appearance_track(n, co + color * 40, sequence_ms, global_ms, 3, value) ||
          appearance_track(n, co + color * 40 + 20, sequence_ms, global_ms, 1, value + 3))
        return 3;
    }
    if (weight != 65535) {
      float opacity = 1;
      if (weight >= wc ||
          appearance_track(n, wo + weight * 20, sequence_ms, global_ms, 1, &opacity))
        return 4;
      value[3] *= opacity;
    }
  }
  return 0;
}
uint32_t decode_materials(uint32_t n, uint32_t sn) {
  info[0] = info[1] = info[2] = 0;
  if (n < 0x138 || n > sizeof(model) || sn < 48 || sn > sizeof(skin))
    return 1;
  if (u32(model, 0) != 0x3032444d || u32(model, 4) != 264 || u32(skin, 0) != 0x4e494b53)
    return 2;
  uint32_t fields[6] = {0x50, 0x70, 0x80, 0x88, 0x90, 0x98}, strides[6] = {16, 4, 2, 2, 2, 2};
  for (uint32_t i = 0; i < 6; i++)
    if (!span(n, u32(model, fields[i] + 4), u32(model, fields[i]), strides[i]))
      return 3;
  uint32_t bc = u32(skin, 36), bo = u32(skin, 40), sc = u32(skin, 28), so = u32(skin, 32);
  if (!bc || bc > 256 || !sc || !span(sn, bo, bc, 24) || !span(sn, so, sc, 48))
    return 4;
  // Limit profile to literal ASCII named textures; reject unsupported dynamic replacement types.
  uint32_t tc = u32(model, 0x50);
  if (!tc || tc > 256)
    return 5;
  for (uint32_t i = 0; i < tc; i++) {
    uint32_t a = u32(model, 0x54) + i * 16, l = u32(model, a + 8), o = u32(model, a + 12);
    if (u32(model, a) != 0 || l < 2 || l > 256 || !span(n, o, l, 1) || model[o + l - 1])
      return 5;
    for (uint32_t j = 0; j < l - 1; j++)
      if (model[o + j] < 32 || model[o + j] > 126)
        return 5;
  }
  uint32_t ns = 0;
  for (uint32_t i = 0; i < bc; i++) {
    uint32_t a = bo + i * 24, subset = u16(skin, a + 4), material = u16(skin, a + 10),
             count = u16(skin, a + 14), tx = u16(skin, a + 16), uv = u16(skin, a + 18),
             weight = u16(skin, a + 20), transform = u16(skin, a + 22);
    if (subset >= sc || material >= u32(model, 0x70) || !count || count > 2 || ns + count > 512)
      return 6;
    if (tx > u32(model, 0x80) || count > u32(model, 0x80) - tx || uv > u32(model, 0x88) ||
        count > u32(model, 0x88) - uv || weight >= u32(model, 0x90) ||
        transform > u32(model, 0x98) || count > u32(model, 0x98) - transform)
      return 7;
    uint32_t ma = u32(model, 0x74) + material * 4;
    uint32_t *row = rows + i * 9;
    row[0] = subset;
    row[1] = material;
    row[2] = u16(model, ma);
    row[3] = u16(model, ma + 2);
    row[4] = u16(skin, a + 12);
    row[5] = u16(skin, a + 2);
    row[6] = ns;
    row[7] = count;
    row[8] = u16(model, u32(model, 0x94) + weight * 2);
    if (row[8] != 65535 && row[8] >= u32(model, 0x58))
      return 8;
    for (uint32_t j = 0; j < count; j++) {
      uint32_t texture = u16(model, u32(model, 0x84) + (tx + j) * 2),
               coord = u16(model, u32(model, 0x8c) + (uv + j) * 2),
               tr = u16(model, u32(model, 0x9c) + (transform + j) * 2);
      if (texture >= tc || (coord != 65535 && coord > 1) || (tr != 65535 && tr >= u32(model, 0x60)))
        return 8;
      stages[ns * 3] = texture;
      stages[ns * 3 + 1] = coord;
      stages[ns * 3 + 2] = tr;
      ns++;
    }
  }
  info[0] = bc;
  info[1] = ns;
  info[2] = tc;
  return 0;
}
