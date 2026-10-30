#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* Bounded owner-asset decoding and ground movement. The host only uploads
 * these arrays and forwards input; no terrain or camera math lives in JS. */
#define CAP (16 * 1024 * 1024)
#define CELLS 256
#define VERTS 145
#define TILE_CACHE 16
#define OBJECTS 4096
#define OBJECT_WORDS (OBJECTS / 32)
#define TILE_SIZE (1600.0f / 3.0f)
#define CELL_SIZE (100.0f / 3.0f)
static uint8_t input[CAP], alpha[CELLS * 64 * 64 * 4];
static float vertices[CELLS * VERTS * 11];
static uint32_t indices[CELLS * 768], chunks[CELLS * 8], meta[8];
static float controls[16], player[8], camera[10], origin[4];
static uint32_t camera_length, camera_duration;
static uint8_t camera_bytes[2 * 1024 * 1024];
typedef struct {
  float x, y, heights[CELLS * VERTS], chunk_bounds[CELLS][4];
  uint16_t holes[CELLS];
  int used;
  uint32_t object_count;
  float objects[OBJECTS][4];
  uint32_t kinds[OBJECTS];
} GroundTile;
static GroundTile ground[TILE_CACHE];
static uint32_t tile_next, current_tile;
uint32_t world_input(void) {
  return (uint32_t)(uintptr_t)input;
}
uint32_t world_capacity(void) {
  return CAP;
}
uint32_t world_vertices(void) {
  return (uint32_t)(uintptr_t)vertices;
}
uint32_t world_indices(void) {
  return (uint32_t)(uintptr_t)indices;
}
uint32_t world_chunks(void) {
  return (uint32_t)(uintptr_t)chunks;
}
uint32_t world_alpha(void) {
  return (uint32_t)(uintptr_t)alpha;
}
uint32_t world_meta(void) {
  return (uint32_t)(uintptr_t)meta;
}
uint32_t world_controls(void) {
  return (uint32_t)(uintptr_t)controls;
}
uint32_t world_player(void) {
  return (uint32_t)(uintptr_t)player;
}
uint32_t world_camera(void) {
  return (uint32_t)(uintptr_t)camera;
}
uint32_t world_origin(void) {
  return (uint32_t)(uintptr_t)origin;
}
static uint32_t u32(const uint8_t *b, uint32_t a) {
  return b[a] | (uint32_t)b[a + 1] << 8 | (uint32_t)b[a + 2] << 16 | (uint32_t)b[a + 3] << 24;
}
static float f32(const uint8_t *b, uint32_t a) {
  union {
    uint32_t u;
    float f;
  } v = {.u = u32(b, a)};
  return v.f;
}
static int span(uint32_t n, uint32_t a, uint32_t count, uint32_t stride) {
  return stride && a <= n && count <= (n - a) / stride;
}
/* Chunk tags are stored little endian with the on-disk FourCC reversed. */
#define TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (d))
static uint32_t find(const uint8_t *b, uint32_t n, uint32_t start, uint32_t tag) {
  for (uint32_t a = start; span(n, a, 1, 8);) {
    uint32_t size = u32(b, a + 4);
    if (!span(n, a + 8, size, 1))
      return 0;
    if (u32(b, a) == tag)
      return a;
    a += 8 + size;
  }
  return 0;
}
static int alpha_layer(uint32_t start, uint32_t end, uint32_t flags, int big, uint32_t channel,
                       uint8_t *out) {
  uint32_t p = start, pixel = 0;
  if (flags & 0x200) {
    while (pixel < 4096) {
      if (p >= end)
        return 1;
      uint32_t run = input[p] & 127, fill = input[p++] & 128;
      if (!run || run > 4096 - pixel || !span(end, p, fill ? 1 : run, 1))
        return 1;
      for (uint32_t i = 0; i < run; i++)
        out[(pixel++) * 4 + channel] = input[fill ? p : p++];
      if (fill)
        p++;
    }
  } else if (big) {
    if (!span(end, p, 4096, 1))
      return 1;
    for (; pixel < 4096; pixel++)
      out[pixel * 4 + channel] = input[p + pixel];
  } else {
    if (!span(end, p, 2048, 1))
      return 1;
    for (; pixel < 4096; pixel++)
      out[pixel * 4 + channel] = ((input[p + pixel / 2] >> ((pixel & 1) * 4)) & 15) * 17;
  }
  /* Legacy alpha maps replicate their final row and column. */
  if (!big) {
    for (uint32_t y = 0; y < 64; y++)
      out[(y * 64 + 63) * 4 + channel] = out[(y * 64 + 62) * 4 + channel];
    for (uint32_t x = 0; x < 64; x++)
      out[(63 * 64 + x) * 4 + channel] = out[(62 * 64 + x) * 4 + channel];
  }
  return 0;
}
uint32_t world_terrain(uint32_t length, uint32_t big_alpha) {
  memset(meta, 0, sizeof(meta));
  if (length > CAP || length < 20 || u32(input, 0) != TAG('M', 'V', 'E', 'R') ||
      u32(input, 8) != 18)
    return 1;
  current_tile = tile_next++ % TILE_CACHE;
  GroundTile *tile = &ground[current_tile];
  tile->object_count = 0;
  tile->used = 0;
  memset(tile->heights, 0, sizeof(tile->heights));
  memset(tile->holes, 0, sizeof(tile->holes));
  uint32_t count = 0, total_indices = 0;
  uint8_t seen[CELLS] = {0};
  for (uint32_t a = 0; span(length, a, 1, 8);) {
    uint32_t size = u32(input, a + 4), base = a + 8, end = base + size;
    if (!span(length, base, size, 1))
      return 2;
    if (u32(input, a) != TAG('M', 'C', 'N', 'K')) {
      a = end;
      continue;
    }
    if (size < 128 || count >= CELLS)
      return 3;
    uint32_t ix = u32(input, base + 4), iy = u32(input, base + 8), layers = u32(input, base + 12);
    if (ix >= 16 || iy >= 16 || seen[iy * 16 + ix] || layers > 4)
      return 4;
    seen[iy * 16 + ix] = 1;
    uint32_t offsets[5] = {20, 24, 116, 28, 36}, sub[5] = {0};
    uint32_t tags[5] = {TAG('M', 'C', 'V', 'T'), TAG('M', 'C', 'N', 'R'), TAG('M', 'C', 'C', 'V'),
                        TAG('M', 'C', 'L', 'Y'), TAG('M', 'C', 'A', 'L')};
    for (uint32_t i = 0; i < 5; i++) {
      uint32_t off = u32(input, base + offsets[i]);
      if (!off)
        continue;
      if (off > size || !span(end, a + off, 1, 8) || u32(input, a + off) != tags[i] ||
          !span(end, a + off + 8, u32(input, a + off + 4), 1))
        return 5;
      sub[i] = a + off;
    }
    uint32_t heights = sub[0], normals = sub[1], colors = sub[2], layer = sub[3], alph = sub[4];
    if (!heights || u32(input, heights + 4) < VERTS * 4 ||
        (layers && (!layer || u32(input, layer + 4) < layers * 16)))
      return 5;
    float x = f32(input, base + 104), y = f32(input, base + 108), z = f32(input, base + 112);
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
      return 6;
    tile->x = x + iy * CELL_SIZE;
    tile->y = y + ix * CELL_SIZE;
    uint32_t cell = iy * 16 + ix, vbase = count * VERTS;
    tile->holes[cell] = (uint16_t)u32(input, base + 60);
    uint32_t v = 0;
    for (uint32_t row = 0; row < 17; row++)
      for (uint32_t col = 0; col < ((row & 1) ? 8u : 9u); col++, v++) {
        float h = z + f32(input, heights + 8 + v * 4), *p = vertices + (vbase + v) * 11;
        if (!isfinite(h))
          return 6;
        tile->heights[cell * VERTS + v] = h;
        p[0] = x - row * CELL_SIZE / 16;
        p[1] = y - (col + ((row & 1) ? .5f : 0)) * CELL_SIZE / 8;
        p[2] = h;
        p[3] = p[4] = 0;
        p[5] = 1;
        if (normals && u32(input, normals + 4) >= VERTS * 3) {
          const int8_t *n = (const int8_t *)input + normals + 8 + v * 3;
          p[3] = n[0] / 127.0f;
          p[4] = n[1] / 127.0f;
          p[5] = n[2] / 127.0f;
        }
        p[6] = (col + ((row & 1) ? .5f : 0)) / 8;
        p[7] = row / 16.0f;
        p[8] = p[9] = p[10] = 1;
        if (colors && u32(input, colors + 4) >= VERTS * 4) {
          p[8] = input[colors + 8 + v * 4 + 2] / 127.0f;
          p[9] = input[colors + 8 + v * 4 + 1] / 127.0f;
          p[10] = input[colors + 8 + v * 4] / 127.0f;
        }
      }
    float *bound = tile->chunk_bounds[cell];
    bound[0] = x - CELL_SIZE * .5f;
    bound[1] = y - CELL_SIZE * .5f;
    float low = tile->heights[cell * VERTS], high = low;
    for (uint32_t j = 1; j < VERTS; j++) {
      float h = tile->heights[cell * VERTS + j];
      low = fminf(low, h);
      high = fmaxf(high, h);
    }
    bound[2] = (low + high) * .5f;
    bound[3] = sqrtf(CELL_SIZE * CELL_SIZE * .5f + (high - low) * (high - low) * .25f);
    uint32_t *info = chunks + count * 8;
    info[0] = total_indices;
    info[2] = layers;
    for (uint32_t l = 0; l < 4; l++)
      info[3 + l] = l < layers ? u32(input, layer + 8 + l * 16) : 0;
    info[7] = cell;
    for (uint32_t row = 0; row < 8; row++)
      for (uint32_t col = 0; col < 8; col++) {
        if (tile->holes[cell] & (1u << ((row / 2) * 4 + col / 2)))
          continue;
        uint32_t tl = vbase + row * 17 + col, tr = tl + 1, mid = tl + 9, bl = tl + 17, br = bl + 1;
        uint32_t quad[12] = {tl, mid, tr, tr, mid, br, br, mid, bl, bl, mid, tl};
        memcpy(indices + total_indices, quad, sizeof(quad));
        total_indices += 12;
      }
    info[1] = total_indices - info[0];
    uint8_t *out = alpha + count * 4096 * 4;
    memset(out, 0, 4096 * 4);
    for (uint32_t l = 1; l < layers; l++) {
      uint32_t flags = u32(input, layer + 8 + l * 16 + 4), off = u32(input, layer + 8 + l * 16 + 8);
      if (!(flags & 0x100))
        continue;
      if (!alph || off > u32(input, alph + 4) ||
          alpha_layer(alph + 8 + off, alph + 8 + u32(input, alph + 4), flags, big_alpha, l - 1,
                      out))
        return 7;
    }
    count++;
    a = end;
  }
  if (count != CELLS)
    return 8;
  tile->used = 1;
  meta[0] = count;
  meta[1] = count * VERTS;
  meta[2] = total_indices;
  uint32_t tex = find(input, length, 0, TAG('M', 'T', 'E', 'X'));
  if (!tex)
    return 9;
  meta[3] = tex + 8;
  meta[4] = u32(input, tex + 4);
  return 0;
}
static float terrain_height(float x, float y) {
  for (uint32_t t = 0; t < TILE_CACHE; t++) {
    GroundTile *tile = &ground[t];
    float row = (tile->x - x) / CELL_SIZE, col = (tile->y - y) / CELL_SIZE;
    if (!tile->used || row < 0 || row >= 16 || col < 0 || col >= 16)
      continue;
    uint32_t r = (uint32_t)row, c = (uint32_t)col, cell = r * 16 + c;
    float fx = (col - c) * 8, fy = (row - r) * 8;
    uint32_t xx = (uint32_t)fx, yy = (uint32_t)fy;
    if (tile->holes[cell] & (1u << ((yy / 2) * 4 + xx / 2)))
      return NAN;
    fx -= xx;
    fy -= yy;
    const float *h = tile->heights + cell * VERTS + yy * 17 + xx;
    float a = h[0], b = h[1], m = h[9], d = h[17], e = h[18];
    /* Barycentric interpolation over the same four triangles as the GPU. */
    if (fy <= fx && fy <= 1 - fx)
      return a + (b - a) * fx + (m - (a + b) * .5f) * fy * 2;
    if (fx >= fy && fx >= 1 - fy)
      return b + (e - b) * fy + (m - (b + e) * .5f) * (1 - fx) * 2;
    if (fy >= fx && fy >= 1 - fx)
      return d + (e - d) * fx + (m - (d + e) * .5f) * (1 - fy) * 2;
    return a + (d - a) * fy + (m - (a + d) * .5f) * fx * 2;
  }
  return NAN;
}
uint32_t world_spawn(void) {
  for (uint32_t i = 0; i < 4; i++)
    if (!isfinite(controls[i]))
      return 1;
  memcpy(player, controls, 4 * sizeof(float));
  player[4] = 0;
  return 0;
}
uint32_t world_step(float dt) {
  if (!isfinite(dt) || dt < 0 || dt > .1f)
    return 1;
  for (uint32_t i = 0; i < 4; i++)
    if (!isfinite(controls[i]) || fabsf(controls[i]) > 1)
      return 1;
  float forward = controls[0], side = controls[1], turn = controls[2];
  player[3] = fmodf(player[3] + turn * 2.25f * dt + 6.283185307f, 6.283185307f);
  float x = player[0], y = player[1], c = cosf(player[3]), s = sinf(player[3]);
  float norm = sqrtf(forward * forward + side * side);
  if (norm > 1) {
    forward /= norm;
    side /= norm;
  }
  float speed = forward < 0 ? 4.5f : 7;
  x += (forward * c - side * s) * speed * dt;
  y += (forward * s + side * c) * speed * dt;
  float z = terrain_height(x, y);
  if (isfinite(z) && fabsf(z - player[2]) < 2) {
    player[0] = x;
    player[1] = y;
    player[2] = z;
  }
  /* Unloaded ground, holes and steep steps stop movement instead of teleporting. */
  player[4] = (forward > 0   ? 1
               : forward < 0 ? 2
                             : 0) |
              (side > 0   ? 4
               : side < 0 ? 8
                          : 0) |
              (turn > 0   ? 16
               : turn < 0 ? 32
                          : 0);
  return 0;
}
uint32_t world_follow(float yaw, float distance, float pitch) {
  if (!isfinite(yaw) || !isfinite(distance) || !isfinite(pitch) || distance < 2 || distance > 50)
    return 1;
  camera[0] = 1.05f;
  camera[1] = 1200;
  camera[2] = .1f;
  camera[3] = player[0] - cosf(yaw) * distance * cosf(pitch);
  camera[4] = player[1] - sinf(yaw) * distance * cosf(pitch);
  camera[5] = player[2] + 1.7f + sinf(pitch) * distance;
  camera[6] = player[0];
  camera[7] = player[1];
  camera[8] = player[2] + 1.7f;
  camera[9] = 0;
  return 0;
}
static int track(uint32_t at, uint32_t ms, uint32_t dimensions, float *out) {
  const uint8_t *b = camera_bytes;
  uint32_t n = camera_length;
  if (!span(n, at, 1, 20))
    return 1;
  uint32_t mode = b[at] | (uint32_t)b[at + 1] << 8, ta = u32(b, at + 8), va = u32(b, at + 16);
  if (!u32(b, at + 4) && !u32(b, at + 12))
    return 0;
  if (mode > 3 || !span(n, ta, 1, 8) || !span(n, va, 1, 8))
    return 1;
  uint32_t count = u32(b, ta), times = u32(b, ta + 4), values = u32(b, va + 4),
           stride = dimensions * 12;
  if (!count || count > 8192 || count != u32(b, va) || !span(n, times, count, 4) ||
      !span(n, values, count, stride))
    return 1;
  uint32_t last = u32(b, times + (count - 1) * 4);
  if (last > camera_duration)
    camera_duration = last;
  uint32_t lo = 0;
  while (lo + 1 < count && u32(b, times + (lo + 1) * 4) <= ms)
    lo++;
  uint32_t hi = lo + 1 < count ? lo + 1 : lo, t0 = u32(b, times + lo * 4),
           t1 = u32(b, times + hi * 4);
  float t = t1 > t0 && ms >= t0 ? fminf(1, (float)(ms - t0) / (t1 - t0)) : 0;
  for (uint32_t k = 0; k < dimensions; k++) {
    float a = f32(b, values + lo * stride + k * 4), v = f32(b, values + hi * stride + k * 4), r = a;
    if (mode == 1)
      r = a + (v - a) * t;
    if (mode >= 2) {
      float ao = f32(b, values + lo * stride + (2 * dimensions + k) * 4),
            vi = f32(b, values + hi * stride + (dimensions + k) * 4);
      float t2 = t * t, t3 = t2 * t;
      if (mode == 2)
        r = (2 * t3 - 3 * t2 + 1) * a + (t3 - 2 * t2 + t) * ao + (-2 * t3 + 3 * t2) * v +
            (t3 - t2) * vi;
      else {
        float s = 1 - t;
        r = s * s * s * a + 3 * s * s * t * ao + 3 * s * t * t * vi + t3 * v;
      }
    }
    if (!isfinite(r))
      return 1;
    out[k] = r;
  }
  return 0;
}
uint32_t world_cinematic_sample(uint32_t ms) {
  if (!camera_length)
    return 1;
  uint32_t at = u32(camera_bytes, 0x114);
  float p[3] = {0}, t[3] = {0}, roll = 0;
  if (track(at + 16, ms, 3, p) || track(at + 48, ms, 3, t) || track(at + 80, ms, 1, &roll))
    return 2;
  float c = cosf(origin[3]), s = sinf(origin[3]);
  camera[0] = f32(camera_bytes, at + 4);
  camera[1] = 1600;
  camera[2] = .1f;
  for (uint32_t which = 0; which < 2; which++) {
    float *v = which ? t : p;
    uint32_t base = at + (which ? 68 : 36);
    float x = v[0] + f32(camera_bytes, base), y = v[1] + f32(camera_bytes, base + 4);
    camera[3 + which * 3] = origin[0] + c * x - s * y;
    camera[4 + which * 3] = origin[1] + s * x + c * y;
    camera[5 + which * 3] = origin[2] + v[2] + f32(camera_bytes, base + 8);
  }
  camera[9] = roll;
  return 0;
}
uint32_t world_cinematic_load(uint32_t n) {
  camera_length = camera_duration = 0;
  if (n < 0x138 || n > sizeof(camera_bytes) || u32(input, 0) != 0x3032444d ||
      u32(input, 4) != 264 || u32(input, 0x110) != 1)
    return 1;
  uint32_t at = u32(input, 0x114);
  if (!span(n, at, 1, 100))
    return 2;
  memcpy(camera_bytes, input, n);
  camera_length = n;
  if (world_cinematic_sample(0) || !camera_duration || camera_duration > 600000) {
    camera_length = 0;
    return 3;
  }
  return 0;
}
uint32_t world_cinematic_duration(void) {
  return camera_duration;
}

static float world_view_output[83];
uint32_t world_view_ptr(void) {
  return (uint32_t)(uintptr_t)world_view_output;
}
static float dot(const float *a, const float *b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static int normalize(float *v) {
  float length = sqrtf(dot(v, v));
  if (!isfinite(length) || length < 0.000001f)
    return 0;
  for (int i = 0; i < 3; i++)
    v[i] /= length;
  return 1;
}
uint32_t world_view(float aspect) {
  const uint32_t authored = 1;
  float *camera_input = camera, *camera_output = world_view_output;
  if (!isfinite(aspect) || aspect <= 0)
    return 1;
  for (int i = 0; i < 10; i++)
    if (!isfinite(camera_input[i]))
      return 1;
  float eye[3], target[3], forward[3], right[3], up[3];
  float height = fmaxf(.1f, 2);
  /* Preserve the authored vertical framing. Cropping the vertical field on
   * wide screens also magnifies every character and clips tall races. */
  float fov = authored == 2 ? camera_input[0] / sqrtf(1 + aspect * aspect)
              : authored    ? camera_input[0]
                            : .65f;
  float near = authored ? camera_input[2] : .01f;
  float far = authored ? camera_input[1] : height * 20;
  if (!(fov > 0 && fov < 3.141593f && near > 0 && far > near))
    return 2;
  for (int i = 0; i < 3; i++) {
    target[i] = authored ? camera_input[6 + i] : i == 2 ? camera_input[12] + height * .5f : 0;
    eye[i] = authored ? camera_input[3 + i]
             : i == 0 ? height * 1.65f
             : i == 2 ? target[2] + height * .03f
                      : 0;
    forward[i] = target[i] - eye[i];
  }
  if (!normalize(forward))
    return 3;
  right[0] = forward[1];
  right[1] = -forward[0];
  right[2] = 0;
  if (!normalize(right))
    return 3;
  up[0] = right[1] * forward[2] - right[2] * forward[1];
  up[1] = right[2] * forward[0] - right[0] * forward[2];
  up[2] = right[0] * forward[1] - right[1] * forward[0];
  float roll = authored ? camera_input[9] : 0, c = cosf(roll), s = sinf(roll);
  for (int i = 0; i < 3; i++) {
    float r = right[i], u = up[i];
    right[i] = r * c + u * s;
    up[i] = u * c - r * s;
  }
  float *v = camera_output, *p = v + 16, *combined = v + 32, *particle = v + 48;
  for (int i = 0; i < 64; i++)
    camera_output[i] = 0;
  for (int i = 0; i < 3; i++) {
    v[i * 4] = right[i];
    v[i * 4 + 1] = up[i];
    v[i * 4 + 2] = -forward[i];
  }
  v[12] = -dot(right, eye);
  v[13] = -dot(up, eye);
  v[14] = dot(forward, eye);
  v[15] = 1;
  float cot = 1 / tanf(fov / 2);
  p[0] = cot / aspect;
  p[5] = cot;
  p[10] = (far + near) / (near - far);
  p[11] = -1;
  p[14] = 2 * far * near / (near - far);
  for (int column = 0; column < 4; column++)
    for (int row = 0; row < 4; row++)
      for (int k = 0; k < 4; k++)
        combined[column * 4 + row] += p[k * 4 + row] * v[column * 4 + k];
  /* Keep culling in world coordinates, but draw camera-relative positions to
   * avoid subtracting large translations after the projection multiply. */
  float *relative = camera_output + 67;
  for (int column = 0; column < 4; column++)
    for (int row = 0; row < 4; row++) {
      relative[column * 4 + row] = column == 3 ? p[12 + row] : 0;
      if (column < 3)
        for (int k = 0; k < 3; k++)
          relative[column * 4 + row] += p[k * 4 + row] * v[column * 4 + k];
    }
  for (int i = 0; i < 10; i++)
    particle[i] = (i % 4 == 2 ? -v[i] : v[i]);
  for (int i = 0; i < 3; i++)
    camera_output[64 + i] = eye[i];
  return 0;
}

static float player_placement[16];
static float orbit[3] = {0, 12, .32f};
uint32_t world_placement(void) {
  return (uint32_t)(uintptr_t)player_placement;
}
uint32_t world_orbit(void) {
  for (uint32_t i = 4; i < 7; i++)
    if (!isfinite(controls[i]) || fabsf(controls[i]) > 10000)
      return 1;
  orbit[0] += controls[4] * .005f;
  orbit[1] = fmaxf(2, fminf(50, orbit[1] + controls[6] * .01f));
  orbit[2] = fmaxf(.05f, fminf(1.3f, orbit[2] + controls[5] * .005f));
  return world_follow(player[3] + orbit[0], orbit[1], orbit[2]);
}
void world_frame_meta(void) {
  memset(player_placement, 0, sizeof(player_placement));
  player_placement[0] = player_placement[5] = player_placement[10] = player_placement[15] = 1;
  for (uint32_t i = 0; i < 3; i++)
    player_placement[12 + i] = player[i];
  meta[0] = (uint32_t)(int32_t)floorf(32 - camera[4] / TILE_SIZE);
  meta[1] = (uint32_t)(int32_t)floorf(32 - camera[3] / TILE_SIZE);
  meta[2] = (uint32_t)(int32_t)floorf(32 - player[1] / TILE_SIZE);
  meta[3] = (uint32_t)(int32_t)floorf(32 - player[0] / TILE_SIZE);
}

/* Static world structures. Coordinate conversion matches the map's Y-up
 * instance records, while model vertices and simulation remain Z-up. */
static char object_names[OBJECTS][256], material_names[256][256];
static float object_matrices[OBJECTS][16];
static uint32_t object_ids[OBJECTS], object_kinds[OBJECTS], object_sets[OBJECTS],
    material_modes[256], exterior_groups[512];
uint32_t world_exterior_groups(void) {
  return (uint32_t)(uintptr_t)exterior_groups;
}
uint32_t world_object_sets(void) {
  return (uint32_t)(uintptr_t)object_sets;
}
uint32_t world_object_kinds(void) {
  return (uint32_t)(uintptr_t)object_kinds;
}
uint32_t world_object_names(void) {
  return (uint32_t)(uintptr_t)object_names;
}
uint32_t world_object_matrices(void) {
  return (uint32_t)(uintptr_t)object_matrices;
}
uint32_t world_object_ids(void) {
  return (uint32_t)(uintptr_t)object_ids;
}
uint32_t world_material_names(void) {
  return (uint32_t)(uintptr_t)material_names;
}
uint32_t world_material_modes(void) {
  return (uint32_t)(uintptr_t)material_modes;
}
static int path_copy(char *out, uint32_t at, uint32_t end) {
  uint32_t i = 0;
  for (; at < end && input[at] && i < 255; at++, i++) {
    if (input[at] < 32 || input[at] > 126)
      return 1;
    out[i] = (char)input[at];
  }
  if (at >= end || input[at] || !i)
    return 1;
  out[i] = 0;
  return 0;
}
static uint32_t read_objects(uint32_t n, uint32_t kind) {
  if (n > CAP)
    return 1;
  uint32_t names = find(input, n, 0, kind ? TAG('M', 'M', 'D', 'X') : TAG('M', 'W', 'M', 'O')),
           lookup = find(input, n, 0, kind ? TAG('M', 'M', 'I', 'D') : TAG('M', 'W', 'I', 'D')),
           defs = find(input, n, 0, kind ? TAG('M', 'D', 'D', 'F') : TAG('M', 'O', 'D', 'F'));
  if (!names || !lookup || !defs)
    return 0;
  uint32_t stride = kind ? 36 : 64;
  uint32_t count = u32(input, defs + 4) / stride;
  if (u32(input, defs + 4) % stride || count > 4096)
    return 2;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t at = defs + 8 + i * stride, id = u32(input, at);
    if (id >= u32(input, lookup + 4) / 4)
      return 3;
    float x = 32 * TILE_SIZE - f32(input, at + 16), y = 32 * TILE_SIZE - f32(input, at + 8),
          z = f32(input, at + 12);
    if (meta[0] >= OBJECTS)
      return 4;
    uint32_t index = meta[0]++, off = u32(input, lookup + 8 + id * 4);
    if (off >= u32(input, names + 4) ||
        path_copy(object_names[index], names + 8 + off, names + 8 + u32(input, names + 4)))
      return 5;
    object_ids[index] = u32(input, at + 4);
    object_kinds[index] = kind;
    object_sets[index] = kind ? 0 : input[at + 58] | (uint32_t)input[at + 59] << 8;
    float angles[3] = {f32(input, at + 20) * .01745329252f,
                       f32(input, at + 24) * .01745329252f + 3.141592654f,
                       f32(input, at + 28) * .01745329252f};
    float cx = cosf(angles[2]), sx = sinf(angles[2]), cy = cosf(angles[0]), sy = sinf(angles[0]),
          cz = cosf(angles[1]), sz = sinf(angles[1]);
    float *m = object_matrices[index];
    memset(m, 0, 16 * sizeof(float));
    m[0] = cz * cy;
    m[1] = sz * cy;
    m[2] = -sy;
    m[4] = cz * sy * sx - sz * cx;
    m[5] = sz * sy * sx + cz * cx;
    m[6] = cy * sx;
    m[8] = cz * sy * cx + sz * sx;
    m[9] = sz * sy * cx - cz * sx;
    m[10] = cy * cx;
    m[12] = x;
    m[13] = y;
    m[14] = z;
    m[15] = 1;
    if (kind) {
      float scale = (input[at + 32] | (uint32_t)input[at + 33] << 8) / 1024.f;
      for (uint32_t c = 0; c < 3; c++)
        for (uint32_t r = 0; r < 3; r++)
          m[c * 4 + r] *= scale;
    }
    for (uint32_t k = 0; k < 16; k++)
      if (!isfinite(m[k]))
        return 6;
    float *bounds = ground[current_tile].objects[index];
    bounds[0] = x;
    bounds[1] = y;
    bounds[2] = z;
    bounds[3] = 40;
    if (!kind) {
      float minx = f32(input, at + 32), miny = f32(input, at + 36), minz = f32(input, at + 40),
            maxx = f32(input, at + 44), maxy = f32(input, at + 48), maxz = f32(input, at + 52);
      bounds[0] = 32 * TILE_SIZE - (minz + maxz) * .5f;
      bounds[1] = 32 * TILE_SIZE - (minx + maxx) * .5f;
      bounds[2] = (miny + maxy) * .5f;
      bounds[3] = sqrtf((maxx - minx) * (maxx - minx) + (maxy - miny) * (maxy - miny) +
                        (maxz - minz) * (maxz - minz)) *
                  .5f;
    }
    for (uint32_t k = 0; k < 4; k++)
      if (!isfinite(bounds[k]))
        return 6;
    ground[current_tile].kinds[index] = kind;
  }
  return 0;
}
uint32_t world_objects(uint32_t n) {
  meta[0] = 0;
  uint32_t r = read_objects(n, 0);
  if (!r)
    r = read_objects(n, 1);
  ground[current_tile].object_count = r ? 0 : meta[0];
  return r;
}
uint32_t world_wmo_root(uint32_t n) {
  memset(meta, 0, sizeof(meta));
  if (n > CAP)
    return 1;
  uint32_t header = find(input, n, 0, TAG('M', 'O', 'H', 'D')),
           names = find(input, n, 0, TAG('M', 'O', 'T', 'X')),
           mats = find(input, n, 0, TAG('M', 'O', 'M', 'T'));
  if (!header || u32(input, header + 4) < 64 || !names || !mats)
    return 2;
  uint32_t count = u32(input, mats + 4) / 64;
  if (count > 256 || u32(input, mats + 4) % 64 || u32(input, header + 12) > 512)
    return 3;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t at = mats + 8 + i * 64, off = u32(input, at + 12);
    if (off >= u32(input, names + 4) ||
        path_copy(material_names[i], names + 8 + off, names + 8 + u32(input, names + 4)))
      return 4;
    material_modes[i] = u32(input, at + 8);
  }
  meta[0] = u32(input, header + 12);
  uint32_t groups = find(input, n, 0, TAG('M', 'O', 'G', 'I'));
  if (!groups || u32(input, groups + 4) != meta[0] * 32)
    return 5;
  for (uint32_t i = 0; i < meta[0]; i++)
    if (u32(input, groups + 8 + i * 32) & 8)
      exterior_groups[meta[2]++] = i;
  meta[1] = count;
  return 0;
}
static uint32_t group_decor[8192];
uint32_t world_group_decor(void) {
  return (uint32_t)(uintptr_t)group_decor;
}
uint32_t world_wmo_group(uint32_t n) {
  memset(meta, 0, sizeof(meta));
  if (n > CAP)
    return 1;
  uint32_t group = find(input, n, 0, TAG('M', 'O', 'G', 'P'));
  if (!group || u32(input, group + 4) < 68)
    return 2;
  uint32_t end = group + 8 + u32(input, group + 4), start = group + 76;
  uint32_t pos = find(input, end, start, TAG('M', 'O', 'V', 'T')),
           norm = find(input, end, start, TAG('M', 'O', 'N', 'R')),
           uv = find(input, end, start, TAG('M', 'O', 'T', 'V')),
           idx = find(input, end, start, TAG('M', 'O', 'V', 'I')),
           batches = find(input, end, start, TAG('M', 'O', 'B', 'A')),
           colors = find(input, end, start, TAG('M', 'O', 'C', 'V'));
  if (!pos || !norm || !uv || !idx)
    return 3;
  uint32_t vc = u32(input, pos + 4) / 12, ic = u32(input, idx + 4) / 2,
           bc = batches ? u32(input, batches + 4) / 24 : 0;
  if (vc > CELLS * VERTS || ic > CELLS * 768 || bc > CELLS || u32(input, norm + 4) < vc * 12 ||
      u32(input, uv + 4) < vc * 8)
    return 4;
  for (uint32_t i = 0; i < vc; i++) {
    float *p = vertices + i * 11;
    for (uint32_t k = 0; k < 3; k++) {
      p[k] = f32(input, pos + 8 + i * 12 + k * 4);
      p[k + 3] = f32(input, norm + 8 + i * 12 + k * 4);
      p[k + 8] = 1;
    }
    p[6] = f32(input, uv + 8 + i * 8);
    p[7] = f32(input, uv + 12 + i * 8);
    if (colors && u32(input, colors + 4) >= vc * 4) {
      for (uint32_t k = 0; k < 3; k++)
        p[8 + k] = input[colors + 8 + i * 4 + 2 - k] / 255.f;
    }
    for (uint32_t k = 0; k < 11; k++)
      if (!isfinite(p[k]))
        return 5;
  }
  for (uint32_t i = 0; i < ic; i++) {
    uint32_t a = idx + 8 + i * 2;
    indices[i] = input[a] | (uint32_t)input[a + 1] << 8;
    if (indices[i] >= vc)
      return 6;
  }
  for (uint32_t i = 0; i < bc; i++) {
    uint32_t a = batches + 8 + i * 24, *p = chunks + i * 8;
    p[0] = u32(input, a + 12);
    p[1] = input[a + 16] | (uint32_t)input[a + 17] << 8;
    p[2] = input[a + 23];
    if (p[0] > ic || p[1] > ic - p[0] || p[1] % 3)
      return 7;
  }
  meta[0] = vc;
  meta[1] = ic;
  meta[2] = bc;
  uint32_t refs = find(input, end, start, TAG('M', 'O', 'D', 'R'));
  if (refs) {
    uint32_t count = u32(input, refs + 4) / 2;
    if (count > 8192 || u32(input, refs + 4) % 2)
      return 8;
    for (uint32_t i = 0; i < count; i++)
      group_decor[i] = input[refs + 8 + i * 2] | (uint32_t)input[refs + 9 + i * 2] << 8;
    meta[3] = count;
  }
  return 0;
}

static uint8_t *static_model;
static uint32_t static_length, static_vertices, static_textures, static_emitters, static_uv;
uint32_t world_static_model(uint32_t n) {
  free(static_model);
  static_model = NULL;
  static_length = 0;
  memset(material_modes, 0, sizeof(material_modes));
  if (n < 0x138 || n > 8388608 || u32(input, 0) != 0x3032444d || u32(input, 4) != 264)
    return 1;
  uint32_t vc = u32(input, 60), vo = u32(input, 64), tc = u32(input, 80), to = u32(input, 84);
  if (vc > CELLS * VERTS || tc > 256 || !span(n, vo, vc, 48) || !span(n, to, tc, 16))
    return 2;
  for (uint32_t i = 0; i < vc; i++) {
    float *p = vertices + i * 11;
    uint32_t a = vo + i * 48;
    for (uint32_t k = 0; k < 3; k++) {
      p[k] = f32(input, a + k * 4);
      p[3 + k] = f32(input, a + 20 + k * 4);
      p[8 + k] = 1;
    }
    p[6] = f32(input, a + 32);
    p[7] = f32(input, a + 36);
    for (uint32_t k = 0; k < 11; k++)
      if (!isfinite(p[k]))
        return 3;
  }
  for (uint32_t i = 0; i < tc; i++) {
    uint32_t at = to + i * 16, off = u32(input, at + 12), len = u32(input, at + 8);
    if (u32(input, at) || len < 2 || len > 256 || !span(n, off, len, 1) ||
        path_copy(material_names[i], off, off + len))
      return 4;
  }
  static_model = malloc(n);
  if (!static_model)
    return 5;
  memcpy(static_model, input, n);
  static_length = n;
  static_vertices = vc;
  static_textures = tc;
  static_emitters = u32(input, 0x128);
  static_uv = u32(input, 96);
  return 0;
}
uint32_t world_static_skin(uint32_t n) {
  memset(meta, 0, sizeof(meta));
  if (!static_model || n < 48 || n > CAP || u32(input, 0) != 0x4e494b53)
    return 1;
  uint32_t vc = u32(input, 4), vo = u32(input, 8), ic = u32(input, 12), io = u32(input, 16),
           sc = u32(input, 28), so = u32(input, 32), bc = u32(input, 36), bo = u32(input, 40);
  if (ic > CELLS * 768 || bc > CELLS || !span(n, vo, vc, 2) || !span(n, io, ic, 2) ||
      !span(n, so, sc, 48) || !span(n, bo, bc, 24))
    return 2;
  for (uint32_t i = 0; i < ic; i++) {
    uint32_t p = io + i * 2, index = input[p] | (uint32_t)input[p + 1] << 8;
    if (index >= vc)
      return 3;
    p = vo + index * 2;
    indices[i] = input[p] | (uint32_t)input[p + 1] << 8;
    if (indices[i] >= static_vertices)
      return 3;
  }
  uint8_t *b = static_model;
  uint32_t sn = static_length;
  for (uint32_t i = 0; i < bc; i++) {
    uint32_t a = bo + i * 24, subset = input[a + 4] | (uint32_t)input[a + 5] << 8,
             material = input[a + 10] | (uint32_t)input[a + 11] << 8,
             tx = input[a + 16] | (uint32_t)input[a + 17] << 8;
    if (subset >= sc || material >= u32(b, 0x70) || tx >= u32(b, 0x80) ||
        !span(sn, u32(b, 0x74), u32(b, 0x70), 4) || !span(sn, u32(b, 0x84), u32(b, 0x80), 2))
      return 4;
    uint32_t at = so + subset * 48, p = u32(b, 0x84) + tx * 2, id = b[p] | (uint32_t)b[p + 1] << 8;
    if (id >= static_textures)
      return 5;
    uint32_t *out = chunks + i * 8;
    out[0] = input[at + 8] | (uint32_t)input[at + 9] << 8;
    out[1] = input[at + 10] | (uint32_t)input[at + 11] << 8;
    out[2] = id;
    if (out[0] > ic || out[1] > ic - out[0] || out[1] % 3)
      return 6;
    p = u32(b, 0x74) + material * 4;
    material_modes[id] = b[p + 2] | (uint32_t)b[p + 3] << 8;
  }
  meta[0] = static_vertices;
  meta[1] = ic;
  meta[2] = bc;
  meta[3] = static_textures;
  meta[4] = static_emitters;
  meta[5] = static_uv;
  return 0;
}

/* Frustum masks use the same WASM matrix as the GPU, so terrain outside the
 * current view never incurs a host draw call. Eight words cover one tile. */
static uint32_t visibility[TILE_CACHE * 10];
uint32_t world_visibility(void) {
  return (uint32_t)(uintptr_t)visibility;
}
void world_cull(void) {
  memset(visibility, 0, sizeof(visibility));
  const float *m = world_view_output + 32;
  for (uint32_t t = 0; t < TILE_CACHE; t++) {
    GroundTile *tile = &ground[t];
    uint32_t *out = visibility + t * 10;
    out[0] = out[1] = 0xffffffff;
    if (!tile->used)
      continue;
    out[0] = (uint32_t)(int32_t)roundf(32 - tile->y / TILE_SIZE);
    out[1] = (uint32_t)(int32_t)roundf(32 - tile->x / TILE_SIZE);
    for (uint32_t c = 0; c < CELLS; c++) {
      const float *b = tile->chunk_bounds[c];
      float x = b[0], y = b[1], z = b[2], r = b[3];
      int visible = 1;
      for (uint32_t axis = 0; axis < 3 && visible; axis++)
        for (int sign = -1; sign <= 1; sign += 2) {
          float nx = m[3] + sign * m[axis], ny = m[7] + sign * m[4 + axis],
                nz = m[11] + sign * m[8 + axis], d = m[15] + sign * m[12 + axis];
          if (nx * x + ny * y + nz * z + d < -r * sqrtf(nx * nx + ny * ny + nz * nz)) {
            visible = 0;
            break;
          }
        }
      if (visible)
        out[2 + c / 32] |= 1u << (c % 32);
    }
  }
}

static uint32_t object_visibility[TILE_CACHE * (OBJECT_WORDS + 2)];
uint32_t world_object_visibility(void) {
  return (uint32_t)(uintptr_t)object_visibility;
}
static int sphere_visible(const float *m, float x, float y, float z, float radius) {
  for (uint32_t axis = 0; axis < 3; axis++)
    for (int sign = -1; sign <= 1; sign += 2) {
      float nx = m[3] + sign * m[axis], ny = m[7] + sign * m[4 + axis],
            nz = m[11] + sign * m[8 + axis], d = m[15] + sign * m[12 + axis];
      if (nx * x + ny * y + nz * z + d < -radius * sqrtf(nx * nx + ny * ny + nz * nz))
        return 0;
    }
  return 1;
}
void world_cull_objects(void) {
  memset(object_visibility, 0, sizeof(object_visibility));
  for (uint32_t t = 0; t < TILE_CACHE; t++) {
    GroundTile *tile = &ground[t];
    uint32_t *out = object_visibility + t * (OBJECT_WORDS + 2);
    out[0] = out[1] = 0xffffffff;
    if (!tile->used)
      continue;
    out[0] = (uint32_t)(int32_t)roundf(32 - tile->y / TILE_SIZE);
    out[1] = (uint32_t)(int32_t)roundf(32 - tile->x / TILE_SIZE);
    for (uint32_t i = 0; i < tile->object_count; i++) {
      const float *b = tile->objects[i];
      float dx = b[0] - camera[3], dy = b[1] - camera[4],
            limit = (tile->kinds[i] ? 420.f : 1000.f) + b[3];
      if (dx * dx + dy * dy > limit * limit)
        continue;
      if (sphere_visible(world_view_output + 32, b[0], b[1], b[2], b[3]))
        out[2 + i / 32] |= 1u << (i % 32);
    }
  }
}

/* WMO-owned decoration uses local quaternion transforms, then the ADT parent
 * transform. Keep its visibility registry in WASM across camera transitions. */
#define DECOR 32768
#define DECOR_OUTPUT 8192
static float parent_input[16], decor_bounds[DECOR][4];
static char decor_names[DECOR_OUTPUT][256];
static float decor_matrices[DECOR_OUTPUT][16];
static uint32_t decor_ids[DECOR_OUTPUT], decor_sources[DECOR_OUTPUT], decor_count,
    decor_mask[DECOR / 32];
uint32_t world_decor_sources(void) {
  return (uint32_t)(uintptr_t)decor_sources;
}
uint32_t world_parent(void) {
  return (uint32_t)(uintptr_t)parent_input;
}
uint32_t world_decor_names(void) {
  return (uint32_t)(uintptr_t)decor_names;
}
uint32_t world_decor_matrices(void) {
  return (uint32_t)(uintptr_t)decor_matrices;
}
uint32_t world_decor_ids(void) {
  return (uint32_t)(uintptr_t)decor_ids;
}
uint32_t world_decor_visibility(void) {
  return (uint32_t)(uintptr_t)decor_mask;
}
uint32_t world_wmo_decor(uint32_t n, uint32_t selected) {
  meta[0] = 0;
  if (n > CAP)
    return 1;
  uint32_t names = find(input, n, 0, TAG('M', 'O', 'D', 'N')),
           defs = find(input, n, 0, TAG('M', 'O', 'D', 'D')),
           sets = find(input, n, 0, TAG('M', 'O', 'D', 'S'));
  if (!names || !defs || !sets)
    return 0;
  uint32_t count = u32(input, defs + 4) / 40, setcount = u32(input, sets + 4) / 32;
  if (u32(input, defs + 4) % 40 || u32(input, sets + 4) % 32 || selected >= setcount ||
      count > DECOR_OUTPUT)
    return 2;
  for (uint32_t set = 0; set < setcount; set++) {
    if (set != 0 && set != selected)
      continue;
    uint32_t sa = sets + 8 + set * 32, first = u32(input, sa + 20), number = u32(input, sa + 24);
    if (first > count || number > count - first)
      return 3;
    for (uint32_t j = first; j < first + number; j++) {
      if (meta[0] >= DECOR_OUTPUT || decor_count >= DECOR)
        return 4;
      uint32_t at = defs + 8 + j * 40, off = u32(input, at) & 0xffffff, index = meta[0];
      if (off >= u32(input, names + 4) ||
          path_copy(decor_names[index], names + 8 + off, names + 8 + u32(input, names + 4)))
        return 5;
      float x = f32(input, at + 16), y = f32(input, at + 20), z = f32(input, at + 24),
            w = f32(input, at + 28), scale = f32(input, at + 32);
      float norm = sqrtf(x * x + y * y + z * z + w * w);
      if (!isfinite(norm) || norm < .0001f || !isfinite(scale) || scale < 0)
        return 6;
      x /= norm;
      y /= norm;
      z /= norm;
      w /= norm;
      float local[16] = {
          1 - 2 * y * y - 2 * z * z, 2 * x * y + 2 * w * z,     2 * x * z - 2 * w * y,     0,
          2 * x * y - 2 * w * z,     1 - 2 * x * x - 2 * z * z, 2 * y * z + 2 * w * x,     0,
          2 * x * z + 2 * w * y,     2 * y * z - 2 * w * x,     1 - 2 * x * x - 2 * y * y, 0,
          f32(input, at + 4),        f32(input, at + 8),        f32(input, at + 12),       1};
      for (uint32_t c = 0; c < 3; c++)
        for (uint32_t r = 0; r < 3; r++)
          local[c * 4 + r] *= scale;
      float *out = decor_matrices[index];
      for (uint32_t c = 0; c < 4; c++)
        for (uint32_t r = 0; r < 4; r++) {
          out[c * 4 + r] = 0;
          for (uint32_t k = 0; k < 4; k++)
            out[c * 4 + r] += parent_input[k * 4 + r] * local[c * 4 + k];
          if (!isfinite(out[c * 4 + r]))
            return 6;
        }
      uint32_t id = decor_count++;
      decor_ids[index] = id;
      decor_sources[index] = j;
      for (uint32_t k = 0; k < 3; k++)
        decor_bounds[id][k] = out[12 + k];
      decor_bounds[id][3] = fmaxf(30, scale * 30);
      meta[0]++;
    }
  }
  return 0;
}
void world_cull_decor(void) {
  memset(decor_mask, 0, sizeof(decor_mask));
  for (uint32_t i = 0; i < decor_count; i++) {
    const float *b = decor_bounds[i];
    float x = b[0] - camera[3], y = b[1] - camera[4], limit = 550 + b[3];
    if (x * x + y * y <= limit * limit &&
        sphere_visible(world_view_output + 32, b[0], b[1], b[2], b[3]))
      decor_mask[i / 32] |= 1u << (i % 32);
  }
}

/* Owner Light/LightParams/LightIntBand tables. Spatial selection and periodic
 * color-band interpolation are evaluated in WASM, never in the DOM layer. */
static uint8_t *environment_tables[3];
static uint32_t environment_lengths[3], band_rows[65536], parameter_rows[4096];
static float sky_values[58];
static uint32_t selected_sky;
uint32_t world_sky_values(void) {
  return (uint32_t)(uintptr_t)sky_values;
}
uint32_t world_sky_id(void) {
  return selected_sky;
}
uint32_t world_environment_load(uint32_t kind, uint32_t n) {
  if (kind > 2 || n < 20 || n > 4 * 1024 * 1024 || u32(input, 0) != 0x43424457)
    return 1;
  uint32_t count = u32(input, 4), fields = u32(input, 8), stride = u32(input, 12),
           minimum = kind == 0   ? 15
                     : kind == 1 ? 9
                                 : 34;
  if (fields < minimum || stride != fields * 4 || !span(n, 20, count, stride) ||
      20 + count * stride + u32(input, 16) != n)
    return 2;
  uint8_t *copy = malloc(n);
  if (!copy)
    return 3;
  memcpy(copy, input, n);
  free(environment_tables[kind]);
  environment_tables[kind] = copy;
  environment_lengths[kind] = n;
  if (kind) {
    uint32_t *index = kind == 1 ? parameter_rows : band_rows, capacity = kind == 1 ? 4096 : 65536;
    memset(index, 0, capacity * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) {
      uint32_t a = 20 + i * stride, id = u32(copy, a);
      if (id >= capacity)
        return 4;
      index[id] = a;
    }
  }
  return 0;
}
static void sky_band(uint32_t parameter, uint32_t band, float time, float *out) {
  const uint8_t *b = environment_tables[2];
  uint32_t id = (parameter - 1) * 18 + band + 1;
  if (!b || id >= 65536 || !band_rows[id])
    return;
  uint32_t at = band_rows[id], count = u32(b, at + 4);
  if (!count || count > 16)
    return;
  uint32_t lo = count - 1, hi = 0;
  float t0 = (float)u32(b, at + 8 + lo * 4) - 2880, t1 = (float)u32(b, at + 8);
  for (uint32_t i = 0; i < count; i++) {
    float t = (float)u32(b, at + 8 + i * 4);
    if (t <= time) {
      lo = i;
      t0 = t;
    }
    if (t > time) {
      hi = i;
      t1 = t;
      break;
    }
    hi = 0;
    t1 = (float)u32(b, at + 8) + 2880;
  }
  float fraction = t1 > t0 ? (time - t0) / (t1 - t0) : 0;
  uint32_t a = u32(b, at + 72 + lo * 4), c = u32(b, at + 72 + hi * 4);
  for (uint32_t j = 0; j < 3; j++) {
    uint32_t shift = 16 - j * 8;
    float x = (a >> shift) & 255, y = (c >> shift) & 255;
    out[j] = (x + (y - x) * fraction) / 255;
  }
}
uint32_t world_environment(uint32_t map, float time, float elapsed) {
  if (!environment_tables[0] || !environment_tables[1] || !environment_tables[2] ||
      !isfinite(time) || !isfinite(elapsed))
    return 1;
  time = fmodf(fmaxf(0, time), 2880);
  const uint8_t *b = environment_tables[0];
  uint32_t count = u32(b, 4), stride = u32(b, 12), selected = 0, fallback = 0;
  float best = 0, best_radius = 1e9f;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t a = 20 + i * stride;
    if (u32(b, a + 4) != map)
      continue;
    float x = f32(b, a + 8), y = f32(b, a + 12), z = f32(b, a + 16);
    if (!x && !y && !z) {
      fallback = u32(b, a + 28);
      continue;
    }
    float dx = 32 * TILE_SIZE - z / 36 - camera[3], dy = 32 * TILE_SIZE - x / 36 - camera[4],
          dz = y / 36 - camera[5], inner = f32(b, a + 20) / 36, outer = f32(b, a + 24) / 36;
    float d = sqrtf(dx * dx + dy * dy + dz * dz),
          weight = outer > inner ? fminf(1, fmaxf(0, (outer - d) / (outer - inner))) : 0;
    if (weight > best || (weight == best && weight > 0 && outer < best_radius)) {
      selected = u32(b, a + 28);
      best = weight;
      best_radius = outer;
    }
  }
  if (!selected)
    selected = fallback;
  if (!fallback)
    fallback = selected;
  if (!selected || selected >= 4096 || !parameter_rows[selected])
    return 2;
  for (uint32_t band = 0; band < 18; band++) {
    float a[3] = {0}, c[3] = {0};
    sky_band(fallback, band, time, a);
    sky_band(selected, band, time, c);
    for (uint32_t j = 0; j < 3; j++)
      sky_values[band * 3 + j] = a[j] + (c[j] - a[j]) * best;
  }
  selected_sky = u32(environment_tables[1], parameter_rows[selected] + 8);
  float angle = (time / 2880 - .25f) * 6.283185307f;
  sky_values[54] = cosf(angle);
  sky_values[55] = .3f;
  sky_values[56] = sinf(angle);
  sky_values[57] = fmodf(elapsed * .000005f, 1.f);
  return 0;
}
