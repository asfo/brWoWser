#include <stdint.h>
static uint8_t skin[262144], seen[131072];
static uint32_t indices[131072], ranges[512 * 4], info[3];
uint32_t skin_ptr(void) {
  return (uint32_t)(uintptr_t)skin;
}
uint32_t index_ptr(void) {
  return (uint32_t)(uintptr_t)indices;
}
uint32_t range_ptr(void) {
  return (uint32_t)(uintptr_t)ranges;
}
uint32_t info_ptr(void) {
  return (uint32_t)(uintptr_t)info;
}
static uint32_t u32(uint32_t o) {
  return skin[o] | ((uint32_t)skin[o + 1] << 8) | ((uint32_t)skin[o + 2] << 16) |
         ((uint32_t)skin[o + 3] << 24);
}
static uint32_t u16(uint32_t o) {
  return skin[o] | ((uint32_t)skin[o + 1] << 8);
}
static int span(uint32_t n, uint32_t f, uint32_t stride) {
  uint32_t c = u32(f), o = u32(f + 4);
  return o <= n && c <= (n - o) / stride;
}
uint32_t decode_mesh(uint32_t n, uint32_t vertices) {
  info[0] = info[1] = info[2] = 0;
  if (n < 48 || n > sizeof(skin))
    return 1;
  if (u32(0) != 0x4e494b53)
    return 2;
  if (!span(n, 12, 2) || !span(n, 28, 48))
    return 3;
  uint32_t count = u32(12), off = u32(16), nr = u32(28), ro = u32(32);
  if (!count || count > 131072 || !nr || nr > 512 || !vertices || vertices > 32768)
    return 4;
  for (uint32_t i = 0; i < count; i++)
    seen[i] = 0;
  for (uint32_t i = 0; i < nr; i++) {
    uint32_t p = ro + i * 48, vs = u16(p + 4), vc = u16(p + 6), start = u16(p + 8),
             length = u16(p + 10);
    if (u16(p + 2) != 0)
      return 5; // Explicit low-offset profile, no high-index extension.
    if (!vc || vs > vertices || vc > vertices - vs || start > count || length > count - start ||
        length % 3)
      return 6;
    for (uint32_t j = start; j < start + length; j++) {
      uint32_t index = u16(off + j * 2);
      if (index < vs || index - vs >= vc)
        return 7;
      if (seen[j])
        return 8;
      seen[j] = 1;
    }
  }
  for (uint32_t j = 0; j < count; j++)
    if (!seen[j])
      return 9;
  for (uint32_t j = 0; j < count; j++)
    indices[j] = u16(off + j * 2);
  for (uint32_t i = 0; i < nr; i++) {
    uint32_t p = ro + i * 48;
    for (uint32_t k = 0; k < 4; k++)
      ranges[i * 4 + k] = u16(p + 4 + k * 2);
  }
  info[0] = count;
  info[1] = nr;
  info[2] = count / 3;
  return 0;
}
