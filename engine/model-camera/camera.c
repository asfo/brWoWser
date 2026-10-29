#include <stdint.h>
static uint8_t input[2097152];
static float values[10];
static uint32_t counts[3];
uint32_t input_ptr(void) {
  return (uint32_t)(uintptr_t)input;
}
uint32_t values_ptr(void) {
  return (uint32_t)(uintptr_t)values;
}
uint32_t counts_ptr(void) {
  return (uint32_t)(uintptr_t)counts;
}
static uint32_t u32(uint32_t o) {
  return input[o] | ((uint32_t)input[o + 1] << 8) | ((uint32_t)input[o + 2] << 16) |
         ((uint32_t)input[o + 3] << 24);
}
static uint32_t u16(uint32_t o) {
  return input[o] | ((uint32_t)input[o + 1] << 8);
}
static float f32(uint32_t o) {
  union {
    uint32_t u;
    float f;
  } x;
  x.u = u32(o);
  return x.f;
}
static int finite(float f) {
  union {
    uint32_t u;
    float f;
  } x;
  x.f = f;
  return (x.u & 0x7f800000) != 0x7f800000;
}
static int span(uint32_t n, uint32_t o, uint32_t c, uint32_t stride) {
  return o <= n && c <= (n - o) / stride;
}
static uint32_t first_key(uint32_t n, uint32_t track, uint32_t stride, uint32_t *out,
                          uint32_t *count) {
  uint32_t interpolation = u16(track);
  uint32_t global = u16(track + 2);
  if (interpolation > 3 || u32(track + 4) != 1 || u32(track + 12) != 1)
    return 0;
  if (global != 65535) {
    uint32_t count = u32(20), offset = u32(24);
    if (global >= count || !span(n, offset, count, 4) || !u32(offset + global * 4))
      return 0;
  }
  uint32_t t = u32(track + 8), v = u32(track + 16);
  if (!span(n, t, 1, 8) || !span(n, v, 1, 8))
    return 0;
  uint32_t tc = u32(t), to = u32(t + 4), vc = u32(v), vo = u32(v + 4);
  if (!tc || tc > 4096 || tc != vc || !span(n, to, tc, 4) || !span(n, vo, vc, stride) ||
      u32(to) != 0)
    return 0;
  for (uint32_t i = 1; i < tc; i++)
    if (u32(to + i * 4) <= u32(to + (i - 1) * 4))
      return 0;
  *out = vo;
  *count = tc;
  return 1;
}
// Exact first key only, sequence0/time0. Full animation/interpolation not implemented.
uint32_t decode_camera_zero(uint32_t n) {
  for (uint32_t i = 0; i < 10; i++)
    values[i] = 0;
  for (uint32_t i = 0; i < 3; i++)
    counts[i] = 0;
  if (n < 0x138 || n > sizeof(input))
    return 1;
  if (u32(0) != 0x3032444d || u32(4) != 264)
    return 2;
  uint32_t nc = u32(0x110), at = u32(0x114);
  if (nc != 1 || !span(n, at, nc, 100))
    return 3;
  if (u32(at) != 0xffffffff)
    return 4;
  uint32_t keys[3], sizes[3];
  if (!first_key(n, at + 16, 36, keys, sizes) || !first_key(n, at + 48, 36, keys + 1, sizes + 1) ||
      !first_key(n, at + 80, 12, keys + 2, sizes + 2))
    return 5;
  float out[10] = {f32(at + 4), f32(at + 8), f32(at + 12), 0, 0, 0, 0, 0, 0, f32(keys[2])};
  for (uint32_t i = 0; i < 3; i++) {
    out[3 + i] = f32(at + 36 + i * 4) + f32(keys[0] + i * 4);
    out[6 + i] = f32(at + 68 + i * 4) + f32(keys[1] + i * 4);
  }
  for (uint32_t i = 0; i < 10; i++)
    if (!finite(out[i]))
      return 6;
  if (out[0] <= 0 || out[0] >= 3.141593f || out[2] <= 0 || out[1] <= out[2])
    return 7;
  if (out[3] == out[6] && out[4] == out[7] && out[5] == out[8])
    return 8;
  for (uint32_t i = 0; i < 10; i++)
    values[i] = out[i];
  for (uint32_t i = 0; i < 3; i++)
    counts[i] = sizes[i];
  return 0;
}
