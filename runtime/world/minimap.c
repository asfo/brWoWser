#include <stdint.h>
#include <math.h>
#include <string.h>
static char directory[64], hashes[64][64][37], paths[9][64];
static uint32_t zoom;
void bw_minimap_zoom(uint32_t value) {
  zoom = value > 5 ? 5 : value;
}
static float geometry[9 * 24 + 24];
char *bw_minimap_directory(void) {
  return directory;
}
static int integer(const uint8_t **p, const uint8_t *end) {
  int v = 0, n = 0;
  while (*p < end && **p >= '0' && **p <= '9') {
    v = v * 10 + *(*p)++ - '0';
    if (++n > 2)
      return -1;
  }
  return n ? v : -1;
}
uint32_t bw_minimap_index(const uint8_t *b, uint32_t n) {
  size_t size = strlen(directory);
  if (!size || size >= 64 || n > 4 * 1024 * 1024)
    return 1;
  memset(hashes, 0, sizeof(hashes));
  const uint8_t *end = b + n;
  while (b < end) {
    const uint8_t *line = b, *next = memchr(b, '\n', end - b);
    b = next ? next + 1 : end;
    const uint8_t *limit = next ? next : end;
    if ((size_t)(limit - line) < size + 12 || memcmp(line, directory, size) || line[size] != '\\')
      continue;
    const uint8_t *p = line + size + 1;
    if (limit - p < 3 || memcmp(p, "map", 3))
      continue;
    p += 3;
    int x = integer(&p, limit);
    if (p == limit || *p++ != '_')
      continue;
    int y = integer(&p, limit);
    if (x < 0 || x >= 64 || y < 0 || y >= 64 || limit - p < 5 || memcmp(p, ".blp\t", 5))
      continue;
    p += 5;
    if (limit - p < 36 || memcmp(p + 32, ".blp", 4))
      continue;
    int valid = 1;
    for (int i = 0; i < 32; i++)
      if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f')))
        valid = 0;
    if (valid) {
      memcpy(hashes[x][y], p, 36);
      hashes[x][y][36] = 0;
    }
  }
  return 0;
}
static void quad(float *out, float x, float y, float w, float h) {
  const uint32_t order[] = {0, 1, 2, 2, 1, 3};
  for (uint32_t i = 0; i < 6; i++) {
    uint32_t k = order[i];
    out[i * 4] = (x + (k & 1 ? w : 0)) * 2 - 1;
    out[i * 4 + 1] = 1 - (y + (k & 2 ? h : 0)) * 2;
    out[i * 4 + 2] = k & 1 ? 1 : 0;
    out[i * 4 + 3] = k & 2 ? 1 : 0;
  }
}
const float *bw_minimap_sample(float x, float y, float facing) {
  const float tile = 1600.f / 3, diameter = (1400.f - zoom * 200.f) / 3;
  float tx = 32 - y / tile, ty = 32 - x / tile;
  int cx = (int)floorf(tx), cy = (int)floorf(ty);
  uint32_t at = 0;
  for (int row = -1; row <= 1; row++)
    for (int col = -1; col <= 1; col++, at++) {
      int a = cx + col, b = cy + row;
      paths[at][0] = 0;
      if (a >= 0 && a < 64 && b >= 0 && b < 64 && hashes[a][b][0]) {
        memcpy(paths[at], "Textures\\Minimap\\", 17);
        memcpy(paths[at] + 17, hashes[a][b], 37);
      }
      quad(geometry + at * 24, .5f + (a - tx) * tile / diameter, .5f + (b - ty) * tile / diameter,
           tile / diameter, tile / diameter);
    }
  /* North-up map: world +X points up and +Y points left. */
  quad(geometry + 216, .5f - .1f, .5f - .1f, .2f, .2f);
  float c = cosf(facing), s = sinf(facing);
  for (int i = 0; i < 6; i++) {
    float x = geometry[216 + i * 4], y = geometry[217 + i * 4];
    geometry[216 + i * 4] = x * c - y * s;
    geometry[217 + i * 4] = x * s + y * c;
  }
  return geometry;
}
const char *bw_minimap_paths(void) {
  return &paths[0][0];
}
