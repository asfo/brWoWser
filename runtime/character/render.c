#include <stdint.h>
#include <stdlib.h>
#include <math.h>

/* Fixed-size camera exchange; all matrices are column-major. */
static float camera_input[16], camera_output[67];
static float placement_input[33], placement_output[16];
uint32_t character_placement_input(void) {
  return (uint32_t)(uintptr_t)placement_input;
}
uint32_t character_placement_output(void) {
  return (uint32_t)(uintptr_t)placement_output;
}
uint32_t character_placement(void) {
  for (int i = 0; i < 33; i++)
    if (!isfinite(placement_input[i]))
      return 1;
  float rotated[16], c = cosf(placement_input[32]), s = sinf(placement_input[32]);
  for (int column = 0; column < 4; column++) {
    const float *local = placement_input + 16 + column * 4;
    rotated[column * 4] = c * local[0] - s * local[1];
    rotated[column * 4 + 1] = s * local[0] + c * local[1];
    rotated[column * 4 + 2] = local[2];
    rotated[column * 4 + 3] = local[3];
  }
  for (int column = 0; column < 4; column++)
    for (int row = 0; row < 4; row++) {
      float value = 0;
      for (int k = 0; k < 4; k++)
        value += placement_input[k * 4 + row] * rotated[column * 4 + k];
      if (!isfinite(value))
        return 1;
      placement_output[column * 4 + row] = value;
    }
  return 0;
}
extern uint32_t model_ptr(void);
static float light_bones[512 * 16], light_output[16 * 12];
static uint8_t light_valid[512];
static uint32_t light_count;
uint32_t character_light_bones(void) {
  return (uint32_t)(uintptr_t)light_bones;
}
uint32_t character_light_valid(void) {
  return (uint32_t)(uintptr_t)light_valid;
}
uint32_t character_light_output(void) {
  return (uint32_t)(uintptr_t)light_output;
}
uint32_t character_light_count(void) {
  return light_count;
}
static uint32_t light_u32(const uint8_t *b, uint32_t p) {
  return b[p] | (uint32_t)b[p + 1] << 8 | (uint32_t)b[p + 2] << 16 | (uint32_t)b[p + 3] << 24;
}
static uint32_t light_u16(const uint8_t *b, uint32_t p) {
  return b[p] | (uint32_t)b[p + 1] << 8;
}
static float light_f32(const uint8_t *b, uint32_t p) {
  union {
    uint32_t u;
    float f;
  } v = {.u = light_u32(b, p)};
  return v.f;
}
static int light_span(uint32_t n, uint32_t at, uint32_t count, uint32_t stride) {
  return at <= n && count <= (n - at) / stride;
}
static int light_track(const uint8_t *b, uint32_t n, uint32_t at, uint32_t seq, uint32_t global,
                       uint32_t dimensions, int visibility, float *out) {
  if (!light_span(n, at, 1, 20))
    return 1;
  uint32_t mode = light_u16(b, at), gs = light_u16(b, at + 2), nt = light_u32(b, at + 4),
           nv = light_u32(b, at + 12), ta = light_u32(b, at + 8), va = light_u32(b, at + 16);
  if (!nt && !nv)
    return 0;
  if (mode > 1 || !nt || !nv || !light_span(n, ta, nt, 8) || !light_span(n, va, nv, 8))
    return 1;
  uint32_t count = light_u32(b, ta), times = light_u32(b, ta + 4), values = light_u32(b, va + 4),
           stride = visibility ? 1 : dimensions * 4;
  if (!count && !light_u32(b, va))
    return 0;
  if (!count || count > 65536 || count != light_u32(b, va) || !light_span(n, times, count, 4) ||
      !light_span(n, values, count, stride))
    return 1;
  uint32_t ms = seq;
  if (gs != 65535) {
    uint32_t gc = light_u32(b, 20), go = light_u32(b, 24);
    if (gs >= gc || !light_span(n, go, gc, 4) || !light_u32(b, go + gs * 4))
      return 1;
    ms = global % light_u32(b, go + gs * 4);
  }
  uint32_t lo = 0;
  while (lo + 1 < count && light_u32(b, times + (lo + 1) * 4) <= ms)
    lo++;
  uint32_t hi = lo + 1 < count ? lo + 1 : lo, t0 = light_u32(b, times + lo * 4),
           t1 = light_u32(b, times + hi * 4);
  float fraction = mode && hi != lo && t1 > t0 && ms >= t0 ? (float)(ms - t0) / (t1 - t0) : 0;
  for (uint32_t j = 0; j < dimensions; j++) {
    float a = visibility ? b[values + lo] : light_f32(b, values + lo * stride + j * 4),
          c = visibility ? b[values + hi] : light_f32(b, values + hi * stride + j * 4);
    out[j] = a + (c - a) * fraction;
    if (!isfinite(out[j]))
      return 1;
  }
  return 0;
}
uint32_t character_lights(uint32_t n, uint32_t seq, uint32_t global) {
  light_count = 0;
  if (n < 0x138 || n > 8388608)
    return 1;
  const uint8_t *b = (const uint8_t *)(uintptr_t)model_ptr();
  uint32_t count = light_u32(b, 0x108), at = light_u32(b, 0x10c);
  if (count > 64 || !light_span(n, at, count, 156))
    return 1;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t p = at + i * 156, type = light_u16(b, p), bone = light_u16(b, p + 2);
    if (type != 1)
      continue; /* Lua replaces directional sets; point lights remain model-owned. */
    float enabled = 1, ambient[3] = {0}, diffuse[3] = {0}, ai = 1, di = 1, start = 0, end = 0;
    if (light_track(b, n, p + 136, seq, global, 1, 1, &enabled))
      return 2;
    if (enabled <= 0)
      continue;
    if (light_track(b, n, p + 16, seq, global, 3, 0, ambient) ||
        light_track(b, n, p + 36, seq, global, 1, 0, &ai) ||
        light_track(b, n, p + 56, seq, global, 3, 0, diffuse) ||
        light_track(b, n, p + 76, seq, global, 1, 0, &di) ||
        light_track(b, n, p + 96, seq, global, 1, 0, &start) ||
        light_track(b, n, p + 116, seq, global, 1, 0, &end))
      return 2;
    if (light_count >= 16)
      return 3;
    float *out = light_output + light_count++ * 12, position[3];
    for (uint32_t j = 0; j < 3; j++) {
      position[j] = light_f32(b, p + 4 + j * 4);
      if (!isfinite(position[j]))
        return 2;
    }
    for (uint32_t j = 0; j < 3; j++) {
      const float *m = bone < 512 && light_valid[bone] ? light_bones + bone * 16 : NULL;
      out[j] = m ? m[12 + j] + m[j] * position[0] + m[4 + j] * position[1] + m[8 + j] * position[2]
                 : position[j];
      out[3 + j] = ambient[j] * ai;
      out[6 + j] = diffuse[j] * di;
    }
    out[9] = start;
    out[10] = end;
    out[11] = 1;
  }
  return 0;
}
static float attachment_input[19], attachment_output[16];
uint32_t character_attachment_input(void) {
  return (uint32_t)(uintptr_t)attachment_input;
}
uint32_t character_attachment_output(void) {
  return (uint32_t)(uintptr_t)attachment_output;
}
uint32_t character_attachment(void) {
  for (uint32_t i = 0; i < 19; i++)
    if (!isfinite(attachment_input[i]))
      return 1;
  for (uint32_t i = 0; i < 16; i++)
    attachment_output[i] = attachment_input[i];
  for (uint32_t i = 0; i < 3; i++)
    attachment_output[12 + i] = attachment_input[12 + i] +
                                attachment_input[i] * attachment_input[16] +
                                attachment_input[4 + i] * attachment_input[17] +
                                attachment_input[8 + i] * attachment_input[18];
  return 0;
}
uint32_t character_view_input(void) {
  return (uint32_t)(uintptr_t)camera_input;
}
uint32_t character_view_output(void) {
  return (uint32_t)(uintptr_t)camera_output;
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
uint32_t character_view(uint32_t authored, float aspect) {
  if (authored > 2 || !isfinite(aspect) || aspect <= 0)
    return 1;
  for (int i = 0; i < 16; i++)
    if (!isfinite(camera_input[i]))
      return 1;
  float eye[3], target[3], forward[3], right[3], up[3];
  float height = fmaxf(.1f, camera_input[15] - camera_input[12]);
  /* Preserve the authored vertical framing. Cropping the vertical field on
   * wide screens also magnifies every character and clips tall races. */
  float fov = authored == 2 ? camera_input[0] / sqrtf(1 + aspect * aspect)
              : authored    ? camera_input[0] * .6f
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
  for (int i = 0; i < 16; i++)
    particle[i] = (i % 4 == 2 ? -v[i] : v[i]);
  for (int i = 0; i < 3; i++)
    camera_output[64 + i] = eye[i];
  return 0;
}

/* Allocate only actual atlas sizes, not a full-capacity buffer per model. */
static uint8_t *atlas, *layer;
static uint32_t atlas_width, layer_width, layer_height;
void character_skin_end(void) {
  free(atlas);
  free(layer);
  atlas = layer = NULL;
  atlas_width = layer_width = layer_height = 0;
}
uint32_t character_skin_begin(uint32_t width, uint32_t height) {
  character_skin_end();
  if (!width || width != height || width > 2048 || width % 256)
    return 1;
  atlas = malloc((size_t)width * height * 4);
  if (!atlas)
    return 2;
  atlas_width = width;
  return 0;
}
uint32_t character_skin_layer(uint32_t width, uint32_t height) {
  if (!atlas || !width || !height || width > 2048 || height > 2048)
    return 1;
  uint8_t *next = realloc(layer, (size_t)width * height * 4);
  if (!next)
    return 2;
  layer = next;
  layer_width = width;
  layer_height = height;
  return 0;
}
uint32_t character_skin_atlas(void) {
  return (uint32_t)(uintptr_t)atlas;
}
uint32_t character_skin_pixels(void) {
  return (uint32_t)(uintptr_t)layer;
}
uint32_t character_skin_blend(uint32_t region) {
  const uint32_t rects[10][4] = {{0, 160, 128, 32},  {0, 192, 128, 64},  {128, 0, 128, 64},
                                 {128, 96, 128, 64}, {0, 0, 128, 64},    {0, 64, 128, 64},
                                 {0, 128, 128, 32},  {128, 64, 128, 32}, {128, 160, 128, 64},
                                 {128, 224, 128, 32}};
  if (!atlas || !layer || region >= 10)
    return 1;
  uint32_t scale = atlas_width / 256, x = rects[region][0] * scale, y = rects[region][1] * scale;
  uint32_t w = rects[region][2] * scale, h = rects[region][3] * scale;
  for (uint32_t j = 0; j < h; j++)
    for (uint32_t i = 0; i < w; i++) {
      const uint8_t *src = layer + ((j * layer_height / h) * layer_width + i * layer_width / w) * 4;
      uint8_t *dst = atlas + ((y + j) * atlas_width + x + i) * 4;
      double a = src[3] / 255.0;
      for (int k = 0; k < 3; k++)
        dst[k] = (uint8_t)(src[k] * a + dst[k] * (1 - a) + .5);
      dst[3] = (uint8_t)((a + dst[3] / 255.0 * (1 - a)) * 255 + .5);
    }
  return 0;
}

/* Compose a world instance with a camera without moving shared particle state
 * out of model space. Multiple instances can reuse one local simulation. */
static float instance_matrix_input[32], instance_matrix_output[16];
uint32_t character_instance_input(void) {
  return (uint32_t)(uintptr_t)instance_matrix_input;
}
uint32_t character_instance_output(void) {
  return (uint32_t)(uintptr_t)instance_matrix_output;
}
uint32_t character_instance_matrix(void) {
  for (uint32_t i = 0; i < 32; i++)
    if (!isfinite(instance_matrix_input[i]))
      return 1;
  for (uint32_t c = 0; c < 4; c++)
    for (uint32_t r = 0; r < 4; r++) {
      float v = 0;
      for (uint32_t k = 0; k < 4; k++)
        v += instance_matrix_input[k * 4 + r] * instance_matrix_input[16 + c * 4 + k];
      instance_matrix_output[c * 4 + r] = v;
    }
  return 0;
}
