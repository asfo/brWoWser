#include <stdint.h>
static uint8_t model[8388608], skin[262144], output[32768 * 48], seen[32768];
static uint32_t info[4];
uint32_t model_ptr(void) {
  return (uint32_t)(uintptr_t)model;
}
uint32_t skin_ptr(void) {
  return (uint32_t)(uintptr_t)skin;
}
uint32_t output_ptr(void) {
  return (uint32_t)(uintptr_t)output;
}
uint32_t info_ptr(void) {
  return (uint32_t)(uintptr_t)info;
}
static uint32_t u32(const uint8_t *b, uint32_t o) {
  return b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}
static uint32_t u16(const uint8_t *b, uint32_t o) {
  return b[o] | ((uint32_t)b[o + 1] << 8);
}
static int span(const uint8_t *b, uint32_t n, uint32_t f, uint32_t stride) {
  uint32_t c = u32(b, f), o = u32(b, f + 4);
  return o <= n && c <= (n - o) / stride;
}
// Reconstruct selected CPU copy/remap only. Always-safe read-only file policy.
uint32_t prepare_records(uint32_t mn, uint32_t sn) {
  for (uint32_t i = 0; i < 4; i++)
    info[i] = 0;
  if (mn < 0x138 || mn > sizeof(model) || sn < 48 || sn > sizeof(skin))
    return 1;
  if (u32(model, 0) != 0x3032444d || u32(model, 4) != 264 || u32(skin, 0) != 0x4e494b53)
    return 2;
  if (!span(model, mn, 60, 48) || !span(model, mn, 120, 2))
    return 3;
  if (!span(skin, sn, 4, 2) || !span(skin, sn, 20, 4) || !span(skin, sn, 28, 48))
    return 4;
  uint32_t mc = u32(model, 60), mo = u32(model, 64), bc = u32(model, 120), bo = u32(model, 124);
  uint32_t sc = u32(skin, 4), so = u32(skin, 8), pc = u32(skin, 20), po = u32(skin, 24),
           nc = u32(skin, 28), no = u32(skin, 32);
  if (!sc || sc > 32768 || pc < sc)
    return 5;
  for (uint32_t i = 0; i < sc; i++)
    seen[i] = 0;
  // Preflight all reads; reject ambiguous coverage before publishing output size.
  for (uint32_t i = 0; i < nc; i++) {
    uint32_t p = no + i * 48, start = u16(skin, p + 4), count = u16(skin, p + 6),
             base = u16(skin, p + 14), slots = u16(skin, p + 16);
    if (start > sc || count > sc - start || slots > 4)
      return 6;
    for (uint32_t j = start; j < start + count; j++) {
      if (seen[j])
        return 7;
      seen[j] = 1;
      if (u16(skin, so + 2 * j) >= mc)
        return 8;
      for (uint32_t k = 0; k < slots; k++)
        if (base + skin[po + 4 * j + k] >= bc)
          return 9;
    }
  }
  for (uint32_t j = 0; j < sc; j++)
    if (!seen[j])
      return 10;
  uint32_t changed = 0;
  for (uint32_t i = 0; i < nc; i++) {
    uint32_t p = no + i * 48, start = u16(skin, p + 4), count = u16(skin, p + 6),
             base = u16(skin, p + 14), slots = u16(skin, p + 16);
    for (uint32_t j = start; j < start + count; j++) {
      uint32_t source = mo + u16(skin, so + 2 * j) * 48, dest = j * 48;
      for (uint32_t k = 0; k < 48; k++)
        output[dest + k] = model[source + k];
      for (uint32_t k = 0; k < slots; k++) {
        uint8_t value = model[bo + (base + skin[po + 4 * j + k]) * 2];
        changed += output[dest + 16 + k] != value;
        output[dest + 16 + k] = value;
      }
    }
  }
  info[0] = sc;
  info[1] = sc * 48;
  info[2] = nc;
  info[3] = changed;
  return 0;
}
