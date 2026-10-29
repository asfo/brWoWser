#include <stdint.h>
static uint8_t model[2097152], skin[262144];
static uint32_t output[256 * 4], count;
static float inputs[12], rgba[4];
uint32_t model_ptr(void) {
  return (uint32_t)(uintptr_t)model;
}
uint32_t skin_ptr(void) {
  return (uint32_t)(uintptr_t)skin;
}
uint32_t output_ptr(void) {
  return (uint32_t)(uintptr_t)output;
}
uint32_t batch_count(void) {
  return count;
}
uint32_t inputs_ptr(void) {
  return (uint32_t)(uintptr_t)inputs;
}
uint32_t rgba_ptr(void) {
  return (uint32_t)(uintptr_t)rgba;
}
static uint32_t u32(uint8_t *b, uint32_t p) {
  return b[p] | ((uint32_t)b[p + 1] << 8) | ((uint32_t)b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24);
}
static uint32_t u16(uint8_t *b, uint32_t p) {
  return b[p] | ((uint32_t)b[p + 1] << 8);
}
static int span(uint32_t n, uint32_t p, uint32_t c, uint32_t stride) {
  return p <= n && c <= (n - p) / stride;
}
// Selected original 0x836980 flag8 branch, at most two stages; special high-bit branch rejects.
uint32_t resolve(uint32_t n, uint32_t sn) {
  count = 0;
  if (n < 0x138 || n > sizeof(model) || sn < 48 || sn > sizeof(skin))
    return 1;
  if (u32(model, 0) != 0x3032444d || u32(model, 4) != 264 || u32(skin, 0) != 0x4e494b53 ||
      (u32(model, 16) & 8) == 0)
    return 2;
  uint32_t bc = u32(skin, 36), bo = u32(skin, 40), mc = u32(model, 0x70), mo = u32(model, 0x74),
           cc = u32(model, 0x88), co = u32(model, 0x8c), ec = u32(model, 0x130),
           eo = u32(model, 0x134);
  if (!bc || bc > 256 || !span(sn, bo, bc, 24) || !mc || !span(n, mo, mc, 4) || !cc ||
      !span(n, co, cc, 2) || !ec || ec > 256 || !span(n, eo, ec, 2))
    return 3;
  for (uint32_t i = 0; i < bc; i++) {
    uint32_t p = bo + i * 24, raw = u16(skin, p + 2), mi = u16(skin, p + 10),
             stages = u16(skin, p + 14), uv = u16(skin, p + 18);
    if ((raw & 0x8000) || mi >= mc || !stages || stages > 2 || raw > ec || stages > ec - raw ||
        uv > cc || stages > cc - uv)
      return 4;
    uint32_t mode = u16(model, mo + 4 * mi + 2), codes[2] = {0, 0}, word = 0;
    for (uint32_t j = 0; j < stages; j++) {
      uint32_t code = u16(model, eo + 2 * (raw + j)), coord = u16(model, co + 2 * (uv + j));
      if (code > 7 || (coord != 0 && coord != 1 && coord != 65535))
        return 5;
      if (j == 0 && mode == 0)
        code = 0;
      codes[j] = code;
      if (coord > 2)
        code |= 8;
      word |= code << (j ? 0 : 4);
      if (j == stages - 1 && coord == 1)
        word |= 0x4000;
    }
    output[4 * i] = word;
    output[4 * i + 1] = stages;
    output[4 * i + 2] = codes[0];
    output[4 * i + 3] = codes[1];
  }
  count = bc;
  return 0;
}
// Explicit original ps_2_0 record0 texturing arithmetic; caller supplies diffuse RGBA.
uint32_t combine(uint32_t kind) {
  if (kind > 4)
    return 1;
  for (uint32_t i = 0; i < 12; i++)
    if (!(inputs[i] >= 0.f && inputs[i] <= 1.f))
      return 2;
  for (uint32_t i = 0; i < 3; i++) {
    float c = inputs[i];
    if (kind >= 2)
      c = c * inputs[4 + i];
    if (kind == 2 || kind == 3)
      c = c + c;
    rgba[i] = c * inputs[8 + i];
  }
  float alpha = inputs[11];
  if (kind == 1 || kind == 3 || kind == 4)
    alpha = inputs[3] * alpha;
  if (kind >= 2)
    alpha = inputs[7] * alpha;
  if (kind == 2 || kind == 3)
    alpha = alpha + alpha;
  rgba[3] = alpha;
  return 0;
}
