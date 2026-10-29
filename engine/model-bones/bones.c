#include <stdint.h>
static uint8_t model[8388608], prepared[65536 * 48], valid[512], state[512], vertex_valid[65536];
static float matrices[512 * 16], positions[65536 * 3], normals[65536 * 3];
uint32_t normals_ptr(void) {
  return (uint32_t)(uintptr_t)normals;
}
static float camera_view[16];
static uint32_t camera_enabled;
uint32_t view_ptr(void) {
  return (uint32_t)(uintptr_t)camera_view;
}
uint32_t set_camera(uint32_t enabled) {
  if (enabled > 1)
    return 1;
  for (uint32_t i = 0; enabled && i < 16; i++)
    if (!__builtin_isfinite(camera_view[i]))
      return 1;
  camera_enabled = enabled;
  return 0;
}
static uint32_t nb, mn, bo, seqms, globalms, ready, norm_mode;
uint32_t set_normalizer(uint32_t mode) {
  if (mode > 1)
    return 1;
  norm_mode = mode;
  return 0;
}
uint32_t model_ptr(void) {
  return (uint32_t)(uintptr_t)model;
}
uint32_t prepared_ptr(void) {
  return (uint32_t)(uintptr_t)prepared;
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
static int span(uint32_t o, uint32_t c, uint32_t s) {
  return o <= mn && c <= (mn - o) / s;
}
static void identity(float *m) {
  for (uint32_t i = 0; i < 16; i++)
    m[i] = i % 5 == 0 ? 1 : 0;
}
/* Stored mode0 or mode1 only; explicit clocks, no sequence transitions. */
static int track(uint32_t tr, int quat, double *out) {
  uint32_t dims = quat ? 4 : 3, stride = quat ? 8 : 12;
  uint32_t interp = u16(model, tr), gs = u16(model, tr + 2), tc = u32(model, tr + 4),
           ta = u32(model, tr + 8), vc = u32(model, tr + 12), va = u32(model, tr + 16);
  if (!tc && !vc)
    return 1;
  if (interp > 1 || tc != 1 || vc != 1 || !span(ta, tc, 8) || !span(va, vc, 8))
    return 0;
  uint32_t count = u32(model, ta), times = u32(model, ta + 4), values = u32(model, va + 4);
  if (!count && !u32(model, va))
    return 1;
  if (!count || count > 65536 || count != u32(model, va) || !span(times, count, 4) ||
      !span(values, count, stride))
    return 0;
  uint32_t t = seqms;
  if (gs != 65535) {
    uint32_t gc = u32(model, 20), go = u32(model, 24);
    if (gs >= gc || !span(go, gc, 4))
      return 0;
    uint32_t duration = u32(model, go + gs * 4);
    if (!duration && count != 1)
      return 0;
    t = duration ? globalms % duration : 0;
  }
  uint32_t lo = 0;
  for (uint32_t i = 1; i < count; i++) {
    if (u32(model, times + i * 4) <= u32(model, times + (i - 1) * 4))
      return 0;
    if (u32(model, times + i * 4) <= t)
      lo = i;
  }
  uint32_t hi = lo + 1 < count ? lo + 1 : lo;
  float alpha = (interp && hi != lo && t >= u32(model, times + lo * 4))
                    ? (float)((double)(t - u32(model, times + lo * 4)) /
                              (double)(u32(model, times + hi * 4) - u32(model, times + lo * 4)))
                    : 0;
  double sum = 0;
  for (uint32_t j = 0; j < dims; j++) {
    double a, b;
    if (quat) {
      a = (float)((double)u16(model, values + lo * stride + j * 2) * 3.051804378628731e-05 - 1);
      b = (float)((double)u16(model, values + hi * stride + j * 2) * 3.051804378628731e-05 - 1);
    } else {
      a = f32(model, values + lo * stride + j * 4);
      b = f32(model, values + hi * stride + j * 4);
    }
    if (!finite(a) || !finite(b))
      return 0;
    out[j] = (float)(a + (b - a) * alpha);
    sum += out[j] * out[j];
  }
  /* Original 0x982570 polynomial inverse-length approximation, f64 portable intermediates. */
  if (quat && interp) {
    if (sum < 1e-12 || !finite(sum))
      return 0;
    double inv = 1.0 / __builtin_sqrt(sum);
    if (norm_mode) {
      inv = 1.021435022354126 - (sum - 0.9590659737586975) * 0.5325160026550293;
      if (sum <= 0.9152119755744934) {
        inv *= 1.021435022354126 - (inv * inv * sum - 0.9590659737586975) * 0.5325160026550293;
        if (sum <= 0.6521196961402893)
          inv *= 1.021435022354126 - (inv * inv * sum - 0.9590659737586975) * 0.5325160026550293;
      }
    }
    for (uint32_t j = 0; j < 4; j++)
      out[j] = (float)(out[j] * inv);
  }
  return 1;
}
static int evaluate(uint32_t i) {
  if (state[i] == 2)
    return 1;
  if (state[i] == 1)
    return 0;
  state[i] = 1;
  uint32_t b = bo + i * 88, flags = u32(model, b + 4), pw = u16(model, b + 8);
  int parent = pw == 65535 ? -1 : (int)pw;
  if (parent >= 0) {
    if ((uint32_t)parent >= nb || !evaluate((uint32_t)parent))
      return 0;
  }
  int billboard = (flags == 8 || flags == 520) && camera_enabled;
  if ((flags != 0 && flags != 512 && !billboard) || (parent >= 0 && !valid[parent])) {
    state[i] = 2;
    return 1;
  }
  float local[16];
  identity(local);
  if (flags & 512) {
    double tr[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, sc[3] = {1, 1, 1};
    if (!track(b + 16, 0, tr) || !track(b + 36, 1, q) || !track(b + 56, 0, sc))
      return 0;
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                   2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                   2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
    double pivot[3];
    for (uint32_t j = 0; j < 3; j++) {
      pivot[j] = f32(model, b + 76 + j * 4);
      if (!finite(pivot[j]))
        return 0;
    }
    for (uint32_t row = 0; row < 3; row++)
      for (uint32_t col = 0; col < 3; col++)
        local[row * 4 + col] = (float)(r[row * 3 + col] * sc[row]);
    for (uint32_t j = 0; j < 3; j++)
      local[12 + j] = (float)(pivot[j] + tr[j] - pivot[0] * local[j] - pivot[1] * local[4 + j] -
                              pivot[2] * local[8 + j]);
  }
  float *out = matrices + i * 16;
  if (parent < 0) {
    for (uint32_t k = 0; k < 16; k++)
      out[k] = local[k];
  } else
    for (uint32_t r = 0; r < 4; r++)
      for (uint32_t c = 0; c < 4; c++) {
        double sum = 0;
        for (uint32_t k = 0; k < 4; k++)
          sum += (double)local[r * 4 + k] * matrices[parent * 16 + k * 4 + c];
        out[r * 4 + c] = (float)sum;
      }
  if (billboard) {
    /* Spherical billboards face the camera in model space, preserving their
     * animated world pivot and inherited scale. Children inherit this basis. */
    float pivot[3], center[3], scale[3];
    for (uint32_t j = 0; j < 3; j++) {
      pivot[j] = f32(model, b + 76 + j * 4);
      if (!finite(pivot[j]))
        return 0;
    }
    for (uint32_t j = 0; j < 3; j++) {
      center[j] = out[12 + j] + pivot[0] * out[j] + pivot[1] * out[4 + j] + pivot[2] * out[8 + j];
      scale[j] = __builtin_sqrtf(out[j * 4] * out[j * 4] + out[j * 4 + 1] * out[j * 4 + 1] +
                                 out[j * 4 + 2] * out[j * 4 + 2]);
    }
    for (uint32_t j = 0; j < 3; j++) {
      out[j] = camera_view[j * 4 + 2] * scale[0];
      out[4 + j] = camera_view[j * 4] * scale[1];
      out[8 + j] = camera_view[j * 4 + 1] * scale[2];
      out[12 + j] = center[j] - pivot[0] * out[j] - pivot[1] * out[4 + j] - pivot[2] * out[8 + j];
    }
  }
  for (uint32_t k = 0; k < 16; k++)
    if (!finite(out[k]))
      return 0;
  valid[i] = 1;
  state[i] = 2;
  return 1;
}
uint32_t pose(uint32_t n, uint32_t sequence_ms, uint32_t global_ms) {
  ready = 0;
  nb = 0;
  for (uint32_t i = 0; i < 512; i++) {
    valid[i] = 0;
    state[i] = 0;
    for (uint32_t k = 0; k < 16; k++)
      matrices[i * 16 + k] = 0;
  }
  mn = n;
  seqms = sequence_ms;
  globalms = global_ms;
  if (n < 0x138 || n > sizeof(model))
    return 1;
  if (u32(model, 0) != 0x3032444d || u32(model, 4) != 264 || u32(model, 28) != 1)
    return 2;
  uint32_t so = u32(model, 32);
  if (!span(so, 1, 64) || sequence_ms > u32(model, so + 4))
    return 3;
  uint32_t lc = u32(model, 120), lp = u32(model, 124);
  if (!span(lp, lc, 2))
    return 6;
  for (uint32_t i = 0; i < lc; i++)
    if (u16(model, lp + 2 * i) > 255)
      return 6;
  uint32_t count = u32(model, 44);
  bo = u32(model, 48);
  if (!count || count > 512 || !span(bo, count, 88))
    return 4;
  nb = count;
  for (uint32_t i = 0; i < nb; i++)
    if (!evaluate(i)) {
      for (uint32_t j = 0; j < nb; j++)
        valid[j] = 0;
      return 5;
    }
  ready = 1;
  return 0;
}
uint32_t skin(uint32_t count) {
  for (uint32_t i = 0; i < 65536; i++)
    vertex_valid[i] = 0;
  if (!ready || count > 65536)
    return 1;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t o = i * 48, sum = 0;
    int ok = 1;
    double out[3] = {0, 0, 0};
    double normal[3] = {0, 0, 0};
    for (uint32_t k = 0; k < 4; k++) {
      uint32_t weight = prepared[o + 12 + k], bone = prepared[o + 16 + k];
      sum += weight;
      if (!weight)
        continue;
      if (bone >= nb)
        return 2;
      if (!valid[bone]) {
        ok = 0;
        continue;
      }
      for (uint32_t j = 0; j < 3; j++) {
        double p = f32(prepared, o) * matrices[bone * 16 + j] +
                   f32(prepared, o + 4) * matrices[bone * 16 + 4 + j] +
                   f32(prepared, o + 8) * matrices[bone * 16 + 8 + j] +
                   matrices[bone * 16 + 12 + j];
        if (!finite(p))
          return 3;
        out[j] += p * ((double)weight / 255);
        double direction = f32(prepared, o + 20) * matrices[bone * 16 + j] +
                           f32(prepared, o + 24) * matrices[bone * 16 + 4 + j] +
                           f32(prepared, o + 28) * matrices[bone * 16 + 8 + j];
        if (!finite(direction))
          return 3;
        normal[j] += direction * ((double)weight / 255);
      }
    }
    if (sum != 255)
      return 4;
    double magnitude =
        __builtin_sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    for (uint32_t j = 0; j < 3; j++) {
      positions[i * 3 + j] = ok ? (float)out[j] : 0;
      normals[i * 3 + j] = ok && magnitude > .000001 ? (float)(normal[j] / magnitude) : 0;
    }
    vertex_valid[i] = (uint8_t)ok;
  }
  return 0;
}
