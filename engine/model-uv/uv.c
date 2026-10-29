#include <stdint.h>
static uint8_t model[2097152];
static float matrices[256 * 16];
static uint32_t clocks[256], info[3];
uint32_t model_ptr(void) {
  return (uint32_t)(uintptr_t)model;
}
uint32_t matrices_ptr(void) {
  return (uint32_t)(uintptr_t)matrices;
}
uint32_t clocks_ptr(void) {
  return (uint32_t)(uintptr_t)clocks;
}
uint32_t info_ptr(void) {
  return (uint32_t)(uintptr_t)info;
}
static uint32_t u32(uint32_t o) {
  return model[o] | ((uint32_t)model[o + 1] << 8) | ((uint32_t)model[o + 2] << 16) |
         ((uint32_t)model[o + 3] << 24);
}
static uint32_t u16(uint32_t o) {
  return model[o] | ((uint32_t)model[o + 1] << 8);
}
static float f32(uint32_t o) {
  union {
    uint32_t u;
    float f;
  } x;
  x.u = u32(o);
  return x.f;
}
static int span(uint32_t n, uint32_t o, uint32_t c, uint32_t s) {
  return o <= n && c <= (n - o) / s;
}
static uint32_t run(uint32_t n, uint32_t ms, uint32_t explicit_clocks) {
  info[0] = info[1] = info[2] = 0;
  for (uint32_t i = 0; i < 4096; i++)
    matrices[i] = 0;
  if (n < 0x138 || n > sizeof(model))
    return 1;
  if (u32(0) != 0x3032444d || u32(4) != 264)
    return 2;
  uint32_t count = u32(96), offset = u32(100), gc = u32(20), go = u32(24);
  if (!count || count > 256 || !span(n, offset, count, 60) || !gc || gc > 256 ||
      !span(n, go, gc, 4))
    return 3;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t tr = offset + i * 60, mode = u16(tr), gs = u16(tr + 2);
    if (mode > 1 || gs == 65535 || gs >= gc)
      return 4;
    for (uint32_t o = 20; o <= 40; o += 20)
      if (u32(tr + o + 4) || u32(tr + o + 12))
        return 5;
    uint32_t nt = u32(tr + 4), nv = u32(tr + 12), ta = u32(tr + 8), va = u32(tr + 16);
    if (nt != 1 || nv != 1 || !span(n, ta, 1, 8) || !span(n, va, 1, 8))
      return 6;
    uint32_t keys = u32(ta), times = u32(ta + 4), values = u32(va + 4);
    if (!keys || keys > 4096 || keys != u32(va) || !span(n, times, keys, 4) ||
        !span(n, values, keys, 12))
      return 7;
    uint32_t duration = u32(go + gs * 4);
    if (!duration || u32(times) != 0 || u32(times + (keys - 1) * 4) > duration)
      return 8;
    uint32_t t = explicit_clocks ? clocks[gs] : ms % duration;
    if (t >= duration)
      return 9;
    for (uint32_t k = 0; k < keys; k++) {
      if (k && u32(times + k * 4) <= u32(times + (k - 1) * 4))
        return 10;
      for (uint32_t j = 0; j < 3; j++)
        if (!__builtin_isfinite(f32(values + k * 12 + j * 4)))
          return 11;
    }
    uint32_t lo = 0;
    while (lo + 1 < keys && u32(times + (lo + 1) * 4) <= t)
      lo++;
    uint32_t hi = lo + 1 < keys ? lo + 1 : lo;
    float fraction = (mode && hi != lo)
                         ? (float)((double)(t - u32(times + lo * 4)) /
                                   (double)(u32(times + hi * 4) - u32(times + lo * 4)))
                         : 0;
    float *m = matrices + i * 16;
    for (uint32_t j = 0; j < 16; j++)
      m[j] = j % 5 == 0 ? 1 : 0;
    for (uint32_t j = 0; j < 3; j++) {
      double a = f32(values + lo * 12 + j * 4), b = f32(values + hi * 12 + j * 4);
      float v = (float)(a + (b - a) * fraction);
      if (!__builtin_isfinite(v))
        return 12;
      m[12 + j] = v;
    }
  }
  info[0] = count;
  info[1] = gc;
  info[2] = explicit_clocks;
  return 0;
}
uint32_t sample(uint32_t n, uint32_t ms) {
  return run(n, ms, 0);
}
uint32_t sample_clocks(uint32_t n) {
  return run(n, 0, 1);
}
/* Background models are not the Northrend login profile. Unusable translation
 * tracks stay at identity so one odd record does not reject the whole file.
 * sample() above remains the strict login sampler. */
static void identity(float *m) {
  for (uint32_t j = 0; j < 16; j++)
    m[j] = j % 5 == 0 ? 1 : 0;
}
static int translation_keys(uint32_t n, uint32_t tr, uint32_t *keys, uint32_t *times,
                            uint32_t *values) {
  uint32_t lists = u32(tr + 4), value_lists = u32(tr + 12), list_at = u32(tr + 8),
           value_at = u32(tr + 16);
  if (!lists || lists != value_lists || !span(n, list_at, 1, 8) || !span(n, value_at, 1, 8))
    return 0;
  uint32_t count = u32(list_at), time_at = u32(list_at + 4), value_count = u32(value_at),
           data = u32(value_at + 4);
  if (!count || count > 4096 || count != value_count || !span(n, time_at, count, 4) ||
      !span(n, data, count, 12))
    return 0;
  for (uint32_t i = 0; i < count; i++) {
    if (i && u32(time_at + i * 4) <= u32(time_at + (i - 1) * 4))
      return 0;
    for (uint32_t j = 0; j < 3; j++)
      if (!__builtin_isfinite(f32(data + i * 12 + j * 4)))
        return 0;
  }
  *keys = count;
  *times = time_at;
  *values = data;
  return 1;
}
uint32_t sample_scroll(uint32_t n, uint32_t ms) {
  info[0] = info[1] = info[2] = 0;
  for (uint32_t i = 0; i < 4096; i++)
    matrices[i] = 0;
  if (n < 0x138 || n > sizeof(model))
    return 1;
  if (u32(0) != 0x3032444d || u32(4) != 264)
    return 2;
  uint32_t count = u32(96), offset = u32(100), globals = u32(20), global_at = u32(24);
  if (count > 256 || (count && !span(n, offset, count, 60)) || globals > 256 ||
      (globals && !span(n, global_at, globals, 4)))
    return 3;
  uint32_t used = 0;
  for (uint32_t i = 0; i < count; i++) {
    float *m = matrices + i * 16;
    identity(m);
    uint32_t tr = offset + i * 60, mode = u16(tr), sequence = u16(tr + 2), keys, times, values;
    if (!translation_keys(n, tr, &keys, &times, &values))
      continue;
    uint32_t duration = sequence != 65535 && sequence < globals ? u32(global_at + sequence * 4) : 0;
    uint32_t t = duration ? ms % duration : ms;
    uint32_t lo = 0;
    while (lo + 1 < keys && u32(times + (lo + 1) * 4) <= t)
      lo++;
    uint32_t hi = lo + 1 < keys ? lo + 1 : lo;
    uint32_t start = u32(times + lo * 4), end = u32(times + hi * 4);
    float fraction = (mode && hi != lo && end > start && t >= start)
                         ? (float)((double)(t - start) / (double)(end - start))
                         : 0;
    float xyz[3];
    int finite = 1;
    for (uint32_t j = 0; j < 3; j++) {
      double a = f32(values + lo * 12 + j * 4), b = f32(values + hi * 12 + j * 4);
      xyz[j] = (float)(a + (b - a) * fraction);
      if (!__builtin_isfinite(xyz[j]))
        finite = 0;
    }
    if (!finite)
      continue;
    for (uint32_t j = 0; j < 3; j++)
      m[12 + j] = xyz[j];
    used++;
  }
  info[0] = count;
  info[1] = globals;
  info[2] = used;
  return used ? 0 : 4;
}
