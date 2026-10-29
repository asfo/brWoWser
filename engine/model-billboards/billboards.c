#include <stdint.h>
static uint8_t model[2097152], prepared[65536 * 48], valid[512], vertex_valid[65536];
static float world[16], matrices[512 * 16], positions[65536 * 3];
static uint32_t nb;
uint32_t model_ptr(void) {
  return (uint32_t)(uintptr_t)model;
}
uint32_t prepared_ptr(void) {
  return (uint32_t)(uintptr_t)prepared;
}
uint32_t world_ptr(void) {
  return (uint32_t)(uintptr_t)world;
}
uint32_t matrices_ptr(void) {
  return (uint32_t)(uintptr_t)matrices;
}
uint32_t bone_valid_ptr(void) {
  return (uint32_t)(uintptr_t)valid;
}
uint32_t positions_ptr(void) {
  return (uint32_t)(uintptr_t)positions;
}
uint32_t vertex_valid_ptr(void) {
  return (uint32_t)(uintptr_t)vertex_valid;
}
uint32_t bone_count(void) {
  return nb;
}
static uint32_t u32(const uint8_t *b, uint32_t o) {
  return b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}
static uint32_t u16(const uint8_t *b, uint32_t o) {
  return b[o] | ((uint32_t)b[o + 1] << 8);
}
static float f32(const uint8_t *b, uint32_t o) {
  union {
    uint32_t u;
    float f;
  } x;
  x.u = u32(b, o);
  return x.f;
}
static int finite(double x) {
  return __builtin_isfinite(x);
}
static int span(uint32_t n, uint32_t o, uint32_t c, uint32_t s) {
  return o <= n && c <= (n - o) / s;
}
static double length(const double *v) {
  return __builtin_sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}
/* Native flag0x40 branch only, root/no tracks; explicit instance-world matrix. */
uint32_t prepare_billboards(uint32_t n, uint32_t count) {
  nb = 0;
  for (uint32_t i = 0; i < 512; i++)
    valid[i] = 0;
  for (uint32_t i = 0; i < 65536; i++)
    vertex_valid[i] = 0;
  if (n < 0x138 || n > sizeof(model) || count > 65536)
    return 1;
  if (u32(model, 0) != 0x3032444d || u32(model, 4) != 264)
    return 2;
  uint32_t bc = u32(model, 44), bo = u32(model, 48);
  if (!bc || bc > 512 || !span(n, bo, bc, 88))
    return 3;
  nb = bc;
  for (uint32_t i = 0; i < 16; i++)
    if (!finite(world[i]))
      return 4;
  if (world[3] != 0 || world[7] != 0 || world[11] != 0 || world[15] != 1)
    return 4;
  double z[3] = {world[8], world[9], world[10]}, scale[3], y[3], x[3];
  for (uint32_t i = 0; i < 3; i++) {
    double row[3] = {world[i * 4], world[i * 4 + 1], world[i * 4 + 2]};
    scale[i] = length(row);
    if (scale[i] < 1e-8)
      return 5;
  }
  for (uint32_t j = 0; j < 3; j++)
    z[j] = (float)(z[j] / scale[2]);
  y[0] = z[1];
  y[1] = -z[0];
  y[2] = 0;
  double yl = length(y);
  if (yl < 1e-8)
    return 6;
  for (uint32_t j = 0; j < 3; j++)
    y[j] = (float)(y[j] / yl);
  x[0] = (float)(z[1] * y[2] - z[2] * y[1]);
  x[1] = (float)(y[0] * z[2] - y[2] * z[0]);
  x[2] = (float)(y[1] * z[0] - y[0] * z[1]);
  double basis[9];
  for (uint32_t j = 0; j < 3; j++) {
    basis[j] = (float)(x[j] * scale[0]);
    basis[3 + j] = (float)(y[j] * scale[1]);
    basis[6 + j] = (float)(z[j] * scale[2]);
  }
  double a = world[0], b = world[1], c = world[2], d = world[4], e = world[5], f = world[6],
         g = world[8], h = world[9], i = world[10],
         det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (!finite(det) || __builtin_fabs(det) < 1e-10)
    return 7;
  double inv[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                   (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                   (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
  for (uint32_t bone = 0; bone < bc; bone++) {
    uint32_t o = bo + bone * 88;
    if (u32(model, o + 4) != 64)
      continue;
    if (u16(model, o + 8) != 65535)
      return 8;
    for (uint32_t tr = 16; tr <= 56; tr += 20)
      if (u32(model, o + tr + 4) || u32(model, o + tr + 12))
        return 9;
    double pivot[3];
    for (uint32_t j = 0; j < 3; j++) {
      pivot[j] = f32(model, o + 76 + j * 4);
      if (!finite(pivot[j]))
        return 10;
    }
    float *mat = matrices + bone * 16;
    for (uint32_t j = 0; j < 16; j++)
      mat[j] = 0;
    mat[15] = 1;
    double original_anchor[3], translation[3];
    for (uint32_t j = 0; j < 3; j++) {
      original_anchor[j] = (float)(pivot[0] * world[j] + pivot[1] * world[4 + j] +
                                   pivot[2] * world[8 + j] + world[12 + j]);
      translation[j] = (float)(original_anchor[j] - pivot[0] * basis[j] - pivot[1] * basis[3 + j] -
                               pivot[2] * basis[6 + j]);
    }
    /* Return model-local delta: reconstructed native-world palette * inverse(instance-world). */
    for (uint32_t r = 0; r < 3; r++)
      for (uint32_t col = 0; col < 3; col++) {
        double s = 0;
        for (uint32_t k = 0; k < 3; k++)
          s += basis[r * 3 + k] * inv[k * 3 + col];
        mat[r * 4 + col] = (float)s;
      }
    for (uint32_t col = 0; col < 3; col++) {
      double s = 0;
      for (uint32_t k = 0; k < 3; k++)
        s += (translation[k] - world[12 + k]) * inv[k * 3 + col];
      mat[12 + col] = (float)s;
    }
    valid[bone] = 1;
  }
  for (uint32_t v = 0; v < count; v++) {
    uint32_t o = v * 48, sum = 0;
    int ok = 1;
    double out[3] = {0, 0, 0};
    for (uint32_t k = 0; k < 4; k++) {
      uint32_t w = prepared[o + 12 + k], bone = prepared[o + 16 + k];
      sum += w;
      if (!w)
        continue;
      if (bone >= bc)
        return 11;
      if (!valid[bone]) {
        ok = 0;
        continue;
      }
      for (uint32_t j = 0; j < 3; j++) {
        double s = (double)f32(prepared, o) * matrices[bone * 16 + j] +
                   (double)f32(prepared, o + 4) * matrices[bone * 16 + 4 + j] +
                   (double)f32(prepared, o + 8) * matrices[bone * 16 + 8 + j] +
                   matrices[bone * 16 + 12 + j];
        out[j] += s * ((double)w / 255);
      }
    }
    if (sum != 255)
      return 12;
    for (uint32_t j = 0; j < 3; j++) {
      if (!finite(out[j]))
        return 13;
      positions[v * 3 + j] = ok ? (float)out[j] : 0;
    }
    vertex_valid[v] = (uint8_t)ok;
  }
  return 0;
}
