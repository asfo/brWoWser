#include <stdint.h>
#include <math.h>
#include <string.h>

/* Owner Map/LoadingScreens records select the artwork. Geometry and progress
 * stay in WASM; the host only uploads images and submits these four quads. */
static char artwork[256];
static uint32_t screen_id, wide;
static float quads[4 * 24], completed;
static uint32_t read_u32(const uint8_t *b, uint32_t at) {
  return b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24;
}
static int table(const uint8_t *b, uint32_t n, uint32_t fields) {
  if (n < 20 || read_u32(b, 0) != 0x43424457 || read_u32(b, 8) != fields ||
      read_u32(b, 12) != fields * 4 || read_u32(b, 4) > (n - 20) / (fields * 4))
    return 0;
  return 20 + read_u32(b, 4) * fields * 4 + read_u32(b, 16) == n;
}
uint32_t bw_loading_map(const uint8_t *b, uint32_t n, uint32_t map) {
  artwork[0] = 0;
  screen_id = wide = 0;
  completed = 0;
  if (!table(b, n, 66))
    return 1;
  for (uint32_t i = 0; i < read_u32(b, 4); i++) {
    uint32_t at = 20 + i * 264;
    if (read_u32(b, at) == map) {
      screen_id = read_u32(b, at + 57 * 4);
      return 0;
    }
  }
  return 2;
}
uint32_t bw_loading_screen(const uint8_t *b, uint32_t n) {
  if (!table(b, n, 4))
    return 1;
  for (uint32_t i = 0; i < read_u32(b, 4); i++) {
    uint32_t at = 20 + i * 16;
    if (read_u32(b, at) != screen_id)
      continue;
    uint32_t base = 20 + read_u32(b, 4) * 16, off = read_u32(b, at + 8);
    if (off >= n - base)
      return 3;
    const char *path = (const char *)(b + base + off);
    const char *end = memchr(path, 0, n - base - off);
    if (!end || end == path || end - path >= 256)
      return 3;
    memcpy(artwork, path, end - path + 1);
    wide = read_u32(b, at + 12);
    return 0;
  }
  return 2;
}
const char *bw_loading_path(void) {
  return artwork;
}
uint32_t bw_loading_wide(void) {
  return wide;
}
static void quad(uint32_t index, float x, float y, float w, float h, float u, float v) {
  const uint32_t corners[] = {0, 1, 2, 2, 1, 3};
  for (uint32_t i = 0; i < 6; i++) {
    uint32_t k = corners[i];
    float *p = quads + index * 24 + i * 4;
    p[0] = (x + (k & 1 ? w : 0)) * 2 - 1;
    p[1] = 1 - (y + (k & 2 ? h : 0)) * 2;
    p[2] = k & 1 ? u : 0;
    p[3] = k & 2 ? v : 0;
  }
}
const float *bw_loading_layout(float width, float height, float progress) {
  if (!isfinite(width) || !isfinite(height) || width < 1 || height < 1 || !isfinite(progress))
    return 0;
  completed = fmaxf(completed, fminf(1, fmaxf(0, progress)));
  /* Original BLPs use the full texture; separate wide art preserves composition. */
  quad(0, 0, 0, 1, 1, 1, 1);
  float scale = fminf(width / 1024, height / 768), w = 512 * scale / width, h = 64 * scale / height,
        x = (1 - w) * .5f, y = 1 - 110 * scale / height;
  quad(1, x + w * .0625f, y + h * .3125f, w * .875f, h * .375f, 1, 1);
  quad(2, x + w * .0625f, y + h * .3125f, w * .875f * completed, h * .375f, completed, 1);
  quad(3, x, y, w, h, 1, 1);
  return quads;
}
