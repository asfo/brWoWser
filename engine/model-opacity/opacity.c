#include <stdint.h>
#define MCAP 2097152u
#define SCAP 524288u
#define ROWS 512u
static uint8_t model[MCAP], skin[SCAP];
static float values[ROWS * 6];
static uint32_t visible[ROWS];
static uint32_t length;
static uint32_t global_time;
static float colors[1024 * 3];
uint32_t model_ptr(void) {
  return (uint32_t)(uintptr_t)model;
}
uint32_t skin_ptr(void) {
  return (uint32_t)(uintptr_t)skin;
}
uint32_t values_ptr(void) {
  return (uint32_t)(uintptr_t)values;
}
uint32_t visible_ptr(void) {
  return (uint32_t)(uintptr_t)visible;
}
uint32_t count(void) {
  return length;
}
static uint16_t h(const uint8_t *b, uint32_t a) {
  return (uint16_t)(b[a] | b[a + 1] << 8);
}
static uint32_t u(const uint8_t *b, uint32_t a) {
  return b[a] | (uint32_t)b[a + 1] << 8 | (uint32_t)b[a + 2] << 16 | (uint32_t)b[a + 3] << 24;
}
static int span(uint32_t n, uint32_t o, uint32_t c, uint32_t stride) {
  return o <= n && c <= (n - o) / stride;
}
static int track(uint32_t n, uint32_t at, uint32_t ms, int constant, float *out) {
  if (!span(n, at, 1, 20))
    return 0;
  uint32_t interpolation = h(model, at), nt = u(model, at + 4), ot = u(model, at + 8),
           nv = u(model, at + 12), ov = u(model, at + 16);
  uint32_t gs = h(model, at + 2);
  if (interpolation > 1 || (gs != 65535 && gs != 10) || nt != 1 || nv != 1 || !span(n, ot, 1, 8) ||
      !span(n, ov, 1, 8))
    return 0;
  if (gs == 10)
    ms = global_time;
  uint32_t tc = u(model, ot), to = u(model, ot + 4), vc = u(model, ov), vo = u(model, ov + 4);
  if (!tc || tc > 4096 || tc != vc || !span(n, to, tc, 4) || !span(n, vo, vc, 2) ||
      u(model, to) != 0)
    return 0;
  if (constant && (interpolation || tc != 1 || gs != 65535))
    return 0;
  for (uint32_t i = 0; i < tc; i++)
    if (h(model, vo + 2 * i) > 32767 || (i && u(model, to + 4 * i) <= u(model, to + 4 * (i - 1))))
      return 0;
  uint32_t left = 0;
  while (left + 1 < tc && u(model, to + 4 * (left + 1)) <= ms)
    left++;
  float a = (float)h(model, vo + left * 2) * 3.0518509447574615e-05f;
  if (interpolation && left + 1 < tc) {
    float b = (float)h(model, vo + (left + 1) * 2) * 3.0518509447574615e-05f;
    float t = (float)(ms - u(model, to + left * 4)) /
              (float)(u(model, to + (left + 1) * 4) - u(model, to + left * 4));
    a = a + (b - a) * t;
  }
  *out = a;
  return 1;
}
int sample_opacity(uint32_t mn, uint32_t sn, uint32_t ms, uint32_t global_ms) {
  global_time = global_ms;
  length = 0;
  for (uint32_t i = 0; i < ROWS; i++) {
    visible[i] = 0;
    for (uint32_t j = 0; j < 6; j++)
      values[i * 6 + j] = 0;
  }
  if (mn > MCAP || sn > SCAP || mn < 0x138 || sn < 48)
    return -1;
  if (u(model, 0x14) <= 10 || !span(mn, u(model, 0x18), u(model, 0x14), 4) ||
      !u(model, u(model, 0x18) + 40))
    return -10;
  if (u(model, 0) != 0x3032444d || u(model, 4) != 264 || u(skin, 0) != 0x4e494b53)
    return -2;
  uint32_t cc = u(model, 0x48), co = u(model, 0x4c), wc = u(model, 0x58), wo = u(model, 0x5c),
           lc = u(model, 0x90), lo = u(model, 0x94), bc = u(skin, 36), bo = u(skin, 40);
  if (cc > 1024 || wc > 1024 || lc > 4096 || !bc || bc > ROWS || !span(mn, co, cc, 40) ||
      !span(mn, wo, wc, 20) || !span(mn, lo, lc, 2) || !span(sn, bo, bc, 24))
    return -3;
  // Validate every selected-profile track, even if no batch references it.
  float unused;
  for (uint32_t i = 0; i < wc; i++)
    if (!track(mn, wo + i * 20, ms, 1, &unused))
      return -4;
  for (uint32_t i = 0; i < cc; i++) {
    uint32_t a = co + i * 40;
    if (!track(mn, a + 20, ms, 0, &unused))
      return -5;
    if (h(model, a) != 0 || h(model, a + 2) != 65535 || u(model, a + 4) != 1 ||
        u(model, a + 12) != 1)
      return -9;
    uint32_t ot = u(model, a + 8), ov = u(model, a + 16);
    if (!span(mn, ot, 1, 8) || !span(mn, ov, 1, 8) || u(model, ot) != 1 || u(model, ov) != 1)
      return -9;
    uint32_t to = u(model, ot + 4), vo = u(model, ov + 4);
    if (!span(mn, to, 1, 4) || u(model, to) != 0 || !span(mn, vo, 1, 12))
      return -9;
    for (uint32_t j = 0; j < 3; j++) {
      union {
        uint32_t u;
        float f;
      } x;
      x.u = u(model, vo + j * 4);
      if ((x.u & 0x7f800000u) == 0x7f800000u || x.f < 0 || x.f > 16)
        return -9;
      colors[i * 3 + j] = x.f;
    }
  }
  for (uint32_t i = 0; i < bc; i++) {
    uint32_t b = bo + i * 24, ci = h(skin, b + 8), li = h(skin, b + 20);
    float w = 1, c = 1;
    if (h(skin, b + 14)) {
      if (li >= lc)
        return -6;
      uint32_t wi = h(model, lo + li * 2);
      if (wi >= wc)
        return -7;
      if (!track(mn, wo + wi * 20, ms, 1, &w))
        return -4;
    }
    if (ci != 65535) {
      if (ci >= cc)
        return -8;
      if (!track(mn, co + ci * 40 + 20, ms, 0, &c))
        return -5;
    }
    values[i * 6] = w;
    values[i * 6 + 1] = c;
    values[i * 6 + 2] = w * c;
    for (uint32_t j = 0; j < 3; j++)
      values[i * 6 + 3 + j] = ci == 65535 ? 1 : colors[ci * 3 + j];
    visible[i] = (w * c) >= 9.999999747378752e-05f;
  }
  length = bc;
  return (int)bc;
}
