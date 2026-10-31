#include "collision.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define MESH_LIMIT 1024
#define INSTANCE_LIMIT 8192
#define TRIANGLE_LIMIT 524288
#define TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (d))
typedef struct {
  float p[9], bounds[6];
} Triangle;
typedef struct {
  uint32_t key, count;
  Triangle *triangles;
  float bounds[6];
} Mesh;
typedef struct {
  uint32_t id, mesh;
  float matrix[16], inverse[12], bounds[6], scale;
} Instance;
static Mesh meshes[MESH_LIMIT];
static Instance instances[INSTANCE_LIMIT];
static uint32_t mesh_count, instance_count, triangle_count;
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
static int span(uint32_t n, uint32_t at, uint32_t count, uint32_t stride) {
  return stride && at <= n && count <= (n - at) / stride;
}
static uint32_t chunk(const uint8_t *b, uint32_t n, uint32_t at, uint32_t tag) {
  while (span(n, at, 1, 8)) {
    uint32_t size = u32(b, at + 4);
    if (!span(n, at + 8, size, 1))
      return 0;
    if (u32(b, at) == tag)
      return at;
    at += 8 + size;
  }
  return 0;
}
static uint32_t mesh_index(uint32_t key) {
  for (uint32_t i = 0; i < mesh_count; i++)
    if (meshes[i].key == key)
      return i;
  return UINT32_MAX;
}
static void empty_bounds(float *b) {
  for (int k = 0; k < 3; k++) {
    b[k] = INFINITY;
    b[k + 3] = -INFINITY;
  }
}
static void extend(float *b, const float *p) {
  for (int k = 0; k < 3; k++) {
    b[k] = fminf(b[k], p[k]);
    b[k + 3] = fmaxf(b[k + 3], p[k]);
  }
}
uint32_t bw_collision_mesh(uint32_t key, uint32_t kind, const uint8_t *b, uint32_t n) {
  if (!key || kind > 1)
    return 1;
  if (mesh_index(key) != UINT32_MAX)
    return 0;
  if (mesh_count >= MESH_LIMIT)
    return 2;
  uint32_t vc = 0, vo = 0, ic = 0, io = 0, flags = 0;
  if (kind == 1) {
    if (n < 0xf0 || u32(b, 0) != 0x3032444d || u32(b, 4) != 264)
      return 3;
    ic = u32(b, 0xd8);
    io = u32(b, 0xdc);
    vc = u32(b, 0xe0);
    vo = u32(b, 0xe4);
  } else {
    uint32_t group = chunk(b, n, 0, TAG('M', 'O', 'G', 'P'));
    if (!group || u32(b, group + 4) < 68)
      return 3;
    uint32_t end = group + 8 + u32(b, group + 4), start = group + 76;
    uint32_t pos = chunk(b, end, start, TAG('M', 'O', 'V', 'T')),
             idx = chunk(b, end, start, TAG('M', 'O', 'V', 'I'));
    flags = chunk(b, end, start, TAG('M', 'O', 'P', 'Y'));
    if (!pos || !idx || !flags || u32(b, pos + 4) % 12 || u32(b, idx + 4) % 6)
      return 3;
    vo = pos + 8;
    vc = u32(b, pos + 4) / 12;
    io = idx + 8;
    ic = u32(b, idx + 4) / 2;
    if (u32(b, flags + 4) < ic / 3 * 2)
      return 3;
  }
  if (ic % 3 || vc > 131072 || ic > 1572864 || !span(n, vo, vc, 12) || !span(n, io, ic, 2))
    return 4;
  uint32_t count = 0;
  for (uint32_t i = 0; i < ic / 3; i++) {
    uint8_t f = flags ? b[flags + 8 + i * 2] : 8, material = flags ? b[flags + 9 + i * 2] : 0;
    if ((f & 8) || ((f & 32) && !(f & 4)) || material == 255)
      count++;
  }
  if (count > TRIANGLE_LIMIT - triangle_count)
    return 2;
  Triangle *tri = count ? malloc(count * sizeof(*tri)) : NULL;
  if (count && !tri)
    return 5;
  Mesh record = {.key = key, .triangles = tri};
  empty_bounds(record.bounds);
  for (uint32_t i = 0; i < ic / 3; i++) {
    uint8_t f = flags ? b[flags + 8 + i * 2] : 8, material = flags ? b[flags + 9 + i * 2] : 0;
    if (!((f & 8) || ((f & 32) && !(f & 4)) || material == 255))
      continue;
    Triangle *t = &tri[record.count++];
    empty_bounds(t->bounds);
    for (uint32_t j = 0; j < 3; j++) {
      uint32_t at = io + (i * 3 + j) * 2, index = b[at] | (uint32_t)b[at + 1] << 8;
      if (index >= vc) {
        free(tri);
        return 4;
      }
      for (uint32_t k = 0; k < 3; k++) {
        float v = f32(b, vo + index * 12 + k * 4);
        if (!isfinite(v)) {
          free(tri);
          return 4;
        }
        t->p[j * 3 + k] = v;
      }
      extend(t->bounds, t->p + j * 3);
      extend(record.bounds, t->p + j * 3);
    }
  }
  triangle_count += record.count;
  meshes[mesh_count++] = record;
  return 0;
}
static void transform(const float *m, const float *p, float *out) {
  for (int k = 0; k < 3; k++)
    out[k] = m[12 + k] + m[k] * p[0] + m[4 + k] * p[1] + m[8 + k] * p[2];
}
static void local_point(const Instance *i, const float *p, float *out) {
  for (int k = 0; k < 3; k++)
    out[k] = i->inverse[k * 4] * (p[0] - i->matrix[12]) +
             i->inverse[k * 4 + 1] * (p[1] - i->matrix[13]) +
             i->inverse[k * 4 + 2] * (p[2] - i->matrix[14]);
}
uint32_t bw_collision_instance(uint32_t id, uint32_t key, const float *m) {
  uint32_t mesh = mesh_index(key);
  if (mesh == UINT32_MAX || !id)
    return 1;
  for (uint32_t i = 0; i < instance_count; i++)
    if (instances[i].id == id && instances[i].mesh == mesh)
      return 0;
  if (!meshes[mesh].count)
    return 0;
  if (instance_count >= INSTANCE_LIMIT)
    return 2;
  Instance out = {.id = id, .mesh = mesh};
  memcpy(out.matrix, m, 64);
  out.scale = INFINITY;
  for (int k = 0; k < 16; k++)
    if (!isfinite(m[k]))
      return 3;
  /* ADT/doodad transforms have orthogonal axes and a positive uniform scale. */
  for (int axis = 0; axis < 3; axis++) {
    float sq = 0;
    for (int k = 0; k < 3; k++)
      sq += m[axis * 4 + k] * m[axis * 4 + k];
    if (sq < 1e-10f)
      return 3;
    out.scale = fminf(out.scale, sqrtf(sq));
    for (int k = 0; k < 3; k++)
      out.inverse[axis * 4 + k] = m[axis * 4 + k] / sq;
    for (int other = 0; other < axis; other++) {
      float dot = 0;
      for (int k = 0; k < 3; k++)
        dot += m[axis * 4 + k] * m[other * 4 + k];
      if (fabsf(dot) > .001f * sqrtf(sq) *
                           sqrtf(m[other * 4] * m[other * 4] + m[other * 4 + 1] * m[other * 4 + 1] +
                                 m[other * 4 + 2] * m[other * 4 + 2]))
        return 3;
    }
  }
  empty_bounds(out.bounds);
  for (int corner = 0; corner < 8; corner++) {
    float p[3], w[3];
    for (int k = 0; k < 3; k++)
      p[k] = meshes[mesh].bounds[k + ((corner >> k & 1) ? 3 : 0)];
    transform(m, p, w);
    extend(out.bounds, w);
  }
  instances[instance_count++] = out;
  return 0;
}
static float dot(const float *a, const float *b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static void sub(const float *a, const float *b, float *o) {
  for (int k = 0; k < 3; k++)
    o[k] = a[k] - b[k];
}
static void cross(const float *a, const float *b, float *o) {
  o[0] = a[1] * b[2] - a[2] * b[1];
  o[1] = a[2] * b[0] - a[0] * b[2];
  o[2] = a[0] * b[1] - a[1] * b[0];
}
static float clamp(float x) {
  return fminf(1, fmaxf(0, x));
}
static float point_triangle(const float *p, const float *v) {
  const float *a = v, *b = v + 3, *c = v + 6;
  float ab[3], ac[3], ap[3], bp[3], cp[3], q[3];
  sub(b, a, ab);
  sub(c, a, ac);
  sub(p, a, ap);
  float d1 = dot(ab, ap), d2 = dot(ac, ap), d3, d4, d5, d6;
  if (d1 <= 0 && d2 <= 0)
    return dot(ap, ap);
  sub(p, b, bp);
  d3 = dot(ab, bp);
  d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3)
    return dot(bp, bp);
  float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    float t = d1 / (d1 - d3);
    for (int k = 0; k < 3; k++)
      q[k] = ap[k] - t * ab[k];
    return dot(q, q);
  }
  sub(p, c, cp);
  d5 = dot(ab, cp);
  d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6)
    return dot(cp, cp);
  float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    float t = d2 / (d2 - d6);
    for (int k = 0; k < 3; k++)
      q[k] = ap[k] - t * ac[k];
    return dot(q, q);
  }
  float va = d3 * d6 - d5 * d4;
  if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
    float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    for (int k = 0; k < 3; k++)
      q[k] = bp[k] - t * (c[k] - b[k]);
    return dot(q, q);
  }
  float sum = va + vb + vc;
  if (fabsf(sum) < 1e-20f)
    return INFINITY;
  float u = vb / sum, w = vc / sum;
  for (int k = 0; k < 3; k++)
    q[k] = ap[k] - u * ab[k] - w * ac[k];
  return dot(q, q);
}
static float segment_segment(const float *p, const float *q, const float *a, const float *b) {
  float d[3], e[3], r[3];
  sub(q, p, d);
  sub(b, a, e);
  sub(p, a, r);
  float aa = dot(d, d), bb = dot(e, e), c = dot(d, r), f = dot(e, r), s = 0, t = 0;
  if (aa < 1e-20f && bb < 1e-20f)
    return dot(r, r);
  if (aa < 1e-20f)
    t = clamp(f / bb);
  else if (bb < 1e-20f)
    s = clamp(-c / aa);
  else {
    float de = dot(d, e), den = aa * bb - de * de;
    if (den > 1e-20f)
      s = clamp((de * f - c * bb) / den);
    t = (de * s + f) / bb;
    if (t < 0) {
      t = 0;
      s = clamp(-c / aa);
    } else if (t > 1) {
      t = 1;
      s = clamp((de - c) / aa);
    }
  }
  for (int k = 0; k < 3; k++)
    r[k] += s * d[k] - t * e[k];
  return dot(r, r);
}
static int segment_triangle(const float *a, const float *b, const Triangle *t, float *fraction) {
  float d[3], e[3], f[3], h[3], s[3], q[3];
  sub(b, a, d);
  sub(t->p + 3, t->p, e);
  sub(t->p + 6, t->p, f);
  cross(d, f, h);
  float det = dot(e, h);
  if (fabsf(det) < 1e-8f)
    return 0;
  float inv = 1 / det;
  sub(a, t->p, s);
  float u = dot(s, h) * inv;
  if (u < 0 || u > 1)
    return 0;
  cross(s, e, q);
  float v = dot(d, q) * inv;
  if (v < 0 || u + v > 1)
    return 0;
  float hit = dot(f, q) * inv;
  if (hit < 0 || hit > 1)
    return 0;
  *fraction = hit;
  return 1;
}
static int overlap(const float *a, const float *b) {
  for (int k = 0; k < 3; k++)
    if (a[k] > b[k + 3] || a[k + 3] < b[k])
      return 0;
  return 1;
}
int bw_collision_blocked(const float *feet, float radius, float height) {
  float a[3] = {feet[0], feet[1], feet[2] + radius},
        b[3] = {feet[0], feet[1], feet[2] + height - radius};
  float box[6] = {feet[0] - radius, feet[1] - radius, feet[2],
                  feet[0] + radius, feet[1] + radius, feet[2] + height};
  for (uint32_t i = 0; i < instance_count; i++) {
    Instance *instance = &instances[i];
    if (!overlap(box, instance->bounds))
      continue;
    float p[3], q[3], bounds[6];
    local_point(instance, a, p);
    local_point(instance, b, q);
    float r = (radius - .002f) / instance->scale;
    for (int k = 0; k < 3; k++) {
      bounds[k] = fminf(p[k], q[k]) - r;
      bounds[k + 3] = fmaxf(p[k], q[k]) + r;
    }
    Mesh *mesh = &meshes[instance->mesh];
    for (uint32_t j = 0; j < mesh->count; j++) {
      Triangle *t = &mesh->triangles[j];
      if (!overlap(bounds, t->bounds))
        continue;
      float hit;
      if (segment_triangle(p, q, t, &hit) || point_triangle(p, t->p) < r * r ||
          point_triangle(q, t->p) < r * r)
        return 1;
      for (int edge = 0; edge < 3; edge++)
        if (segment_segment(p, q, t->p + edge * 3, t->p + ((edge + 1) % 3) * 3) < r * r)
          return 1;
    }
  }
  return 0;
}
float bw_collision_floor(float x, float y, float low, float high) {
  float best = NAN, a[3] = {x, y, high}, b[3] = {x, y, low};
  float box[6] = {x, y, low, x, y, high};
  for (uint32_t i = 0; i < instance_count; i++) {
    Instance *in = &instances[i];
    if (!overlap(box, in->bounds))
      continue;
    float p[3], q[3];
    local_point(in, a, p);
    local_point(in, b, q);
    float query[6];
    for (int k = 0; k < 3; k++) {
      query[k] = fminf(p[k], q[k]);
      query[k + 3] = fmaxf(p[k], q[k]);
    }
    Mesh *mesh = &meshes[in->mesh];
    for (uint32_t j = 0; j < mesh->count; j++) {
      Triangle *t = &mesh->triangles[j];
      if (!overlap(query, t->bounds))
        continue;
      float fraction;
      if (!segment_triangle(p, q, t, &fraction))
        continue;
      float u[3], v[3], n[3], worldn[3];
      sub(t->p + 3, t->p, u);
      sub(t->p + 6, t->p, v);
      cross(u, v, n);
      for (int k = 0; k < 3; k++)
        worldn[k] = in->matrix[k] * n[0] + in->matrix[4 + k] * n[1] + in->matrix[8 + k] * n[2];
      if (worldn[2] * worldn[2] < .42f * dot(worldn, worldn))
        continue;
      float z = high + (low - high) * fraction;
      if (!isfinite(best) || z > best)
        best = z;
    }
  }
  return best;
}
uint32_t bw_collision_triangles(void) {
  return triangle_count;
}
uint32_t bw_collision_instances(void) {
  return instance_count;
}
