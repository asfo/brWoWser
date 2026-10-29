#include <stdint.h>
static uint8_t input[2097152];
static float output[3];
uint32_t input_ptr(void) {
  return (uint32_t)(uintptr_t)input;
}
uint32_t output_ptr(void) {
  return (uint32_t)(uintptr_t)output;
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
    float f;
    uint32_t u;
  } x;
  x.f = f;
  return (x.u & 0x7f800000) != 0x7f800000;
}
static int span(uint32_t n, uint32_t o, uint32_t c, uint32_t s) {
  return o <= n && c <= (n - o) / s;
}
/* Caller supplies an explicit bounded track-local millisecond; no native clock emulation. */
uint32_t sample(uint32_t n, uint32_t ms) {
  for (uint32_t j = 0; j < 3; j++)
    output[j] = 0;
  if (n < 0x138 || n > sizeof(input))
    return 1;
  if (u32(0) != 0x3032444d || u32(4) != 264 || u32(0x110) != 1)
    return 2;
  uint32_t c = u32(0x114);
  if (!span(n, c, 1, 100))
    return 3;
  uint32_t tr = c + 16;
  if (u16(tr) != 1 || u16(tr + 2) != 65535 || u32(tr + 4) != 1 || u32(tr + 12) != 1)
    return 4;
  uint32_t ta = u32(tr + 8), va = u32(tr + 16);
  if (!span(n, ta, 1, 8) || !span(n, va, 1, 8))
    return 5;
  uint32_t count = u32(ta), times = u32(ta + 4), values = u32(va + 4);
  if (!count || count > 4096 || count != u32(va) || !span(n, times, count, 4) ||
      !span(n, values, count, 36))
    return 6;
  if (u32(times) != 0 || ms > u32(times + (count - 1) * 4))
    return 7;
  for (uint32_t i = 0; i < count; i++) {
    if (i && u32(times + i * 4) <= u32(times + (i - 1) * 4))
      return 8;
    for (uint32_t j = 0; j < 3; j++)
      if (!finite(f32(values + i * 36 + j * 4)))
        return 9;
  }
  uint32_t lo = 0;
  while (lo + 1 < count && u32(times + (lo + 1) * 4) <= ms)
    lo++;
  uint32_t hi = lo + 1 < count ? lo + 1 : lo;
  float alpha = hi == lo ? 0
                         : (float)((double)(ms - u32(times + lo * 4)) /
                                   (double)(u32(times + hi * 4) - u32(times + lo * 4)));
  for (uint32_t j = 0; j < 3; j++) {
    double a = f32(values + lo * 36 + j * 4), b = f32(values + hi * 36 + j * 4);
    float r = (float)(a + (b - a) * (double)alpha);
    if (!finite(r))
      return 10;
    output[j] = r;
  }
  return 0;
}
