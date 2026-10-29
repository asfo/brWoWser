/* Bounded selected MD20/v264 emitter records and sequence0 scalar controls.
 * Native file fixup stride83afba; shape factory833b58; setter inputs833be5..833cf3.
 */
#include <stdint.h>
static unsigned char input[2097152];
static uint32_t metadata[64 * 10], info[2];
static float controls[64 * 11];
static uint32_t size;
static int span(uint32_t offset, uint32_t count, uint32_t stride) {
  return offset <= size && stride && count <= (size - offset) / stride;
}
static uint32_t u32(uint32_t o) {
  return input[o] | ((uint32_t)input[o + 1] << 8) | ((uint32_t)input[o + 2] << 16) |
         ((uint32_t)input[o + 3] << 24);
}
static uint32_t u16(uint32_t o) {
  return input[o] | ((uint32_t)input[o + 1] << 8);
}
static float f32(uint32_t o) {
  union {
    uint32_t u;
    float f;
  } v = {u32(o)};
  return v.f;
}
static int finite(float f) {
  union {
    float f;
    uint32_t u;
  } v = {f};
  return (v.u & 0x7f800000) != 0x7f800000;
}
uint32_t input_ptr(void) {
  return (uint32_t)(uintptr_t)input;
}
uint32_t metadata_ptr(void) {
  return (uint32_t)(uintptr_t)metadata;
}
uint32_t controls_ptr(void) {
  return (uint32_t)(uintptr_t)controls;
}
uint32_t info_ptr(void) {
  return (uint32_t)(uintptr_t)info;
}
static int track(uint32_t at, uint32_t ms, int byte, float *out) {
  if (!span(at, 1, 20))
    return 1;
  uint32_t interpolation = u16(at), global = u16(at + 2), nt = u32(at + 4), ot = u32(at + 8),
           nv = u32(at + 12), ov = u32(at + 16);
  if (interpolation > 1 || global != 65535)
    return 2;
  if (!nt && !nv) {
    *out = byte ? 1 : 0;
    return 0;
  }
  if (nt != 1 || nv != 1 || !span(ot, 1, 8) || !span(ov, 1, 8))
    return 3;
  uint32_t n = u32(ot), times = u32(ot + 4), values = u32(ov + 4);
  if (!n || n > 4096 || n != u32(ov) || !span(times, n, 4) || !span(values, n, byte ? 1 : 4))
    return 4;
  for (uint32_t i = 0; i < n; i++) {
    if (i && u32(times + i * 4) <= u32(times + (i - 1) * 4))
      return 5;
    if (byte) {
      if (input[values + i] > 1)
        return 6;
    } else if (!finite(f32(values + i * 4)))
      return 6;
  }
  uint32_t lo = 0;
  while (lo + 1 < n && u32(times + (lo + 1) * 4) <= ms)
    lo++;
  float first = byte ? input[values + lo] : f32(values + lo * 4);
  if (!interpolation || lo + 1 == n || ms <= u32(times)) {
    *out = first;
    return 0;
  }
  float second = byte ? input[values + lo + 1] : f32(values + (lo + 1) * 4);
  double t = (double)(ms - u32(times + lo * 4)) / (u32(times + (lo + 1) * 4) - u32(times + lo * 4));
  *out = (float)((double)first + ((double)second - first) * t);
  return finite(*out) ? 0 : 7;
}
int sample(uint32_t bytes, uint32_t ms) {
  info[0] = 0;
  if (bytes > sizeof(input) || bytes < 0x130)
    return 1;
  size = bytes;
  if (u32(0) != 0x3032444d || u32(4) != 264)
    return 2;
  uint32_t count = u32(0x128), offset = u32(0x12c);
  if (count > 64 || !span(offset, count, 0x1dc))
    return 3;
  const uint32_t fields[] = {0x34, 0x48, 0x5c, 0x70, 0x84, 0x98, 0xb0, 0xc8, 0xdc, 0xf0, 0x1c8};
  for (uint32_t i = 0; i < count; i++) {
    uint32_t at = offset + i * 0x1dc, *m = metadata + i * 10;
    m[0] = i;
    m[1] = input[at + 0x29];
    m[2] = u32(at + 4);
    m[3] = u16(at + 0x14);
    m[4] = u16(at + 0x16);
    m[5] = input[at + 0x28];
    m[6] = u16(at + 0x30);
    m[7] = u16(at + 0x32);
    m[8] = at;
    m[9] = u32(at);
    if (m[1] != 1 && m[1] != 2)
      return 4;
    for (uint32_t j = 0; j < 11; j++) {
      int code = track(at + fields[j], ms, j == 10, controls + i * 11 + j);
      if (code)
        return 100 + j * 10 + code;
    }
  }
  info[0] = count;
  info[1] = ms;
  return 0;
}
/* Base lifetime curves used by native979e90. Random atlas/size modifiers and
 * optimized packed appearance branch are separate consumers, not supplied here. */
static float appearance_values[8];
uint32_t appearance_ptr(void) {
  return (uint32_t)(uintptr_t)appearance_values;
}
static double normalized_key(uint32_t offset) {
  return (double)(int16_t)u16(offset) * (double)0.000030518509447574615f;
}
static int curve(uint32_t at, float fraction, uint32_t stride, int type, float *out,
                 uint32_t components) {
  if (!span(at, 1, 16))
    return 1;
  uint32_t n = u32(at), times = u32(at + 4), values = u32(at + 12);
  if (!n) {
    for (uint32_t j = 0; j < components; j++)
      out[j] = 0;
    return 0;
  }
  if (n > 256 || n != u32(at + 8) || !span(times, n, 2) || !span(values, n, stride))
    return 2;
  for (uint32_t i = 0; i < n; i++) {
    if ((int16_t)u16(times + i * 2) < 0 || (i && u16(times + i * 2) < u16(times + (i - 1) * 2)))
      return 3;
  }
  uint32_t lo = 0, hi = 0;
  double t = 0;
  if (n == 2) {
    hi = 1;
    t = fraction;
  } else if (n == 3) {
    double split = normalized_key(times + 2);
    if (!(split > 0 && split < 1))
      return 4;
    if (fraction < split) {
      hi = 1;
      t = (double)fraction / split;
    } else {
      lo = 1;
      hi = 2;
      t = ((double)fraction - split) / (1 - split);
    }
  } else if (n > 3) {
    while (lo + 2 < n && normalized_key(times + (lo + 1) * 2) <= fraction)
      lo++;
    hi = lo + 1;
    double begin = normalized_key(times + lo * 2), end = normalized_key(times + hi * 2);
    if (!(end > begin))
      return 4;
    t = ((double)fraction - begin) / (end - begin);
  }
  for (uint32_t j = 0; j < components; j++) {
    float a = type == 0   ? f32(values + lo * stride + j * 4)
              : type == 1 ? (float)(int16_t)u16(values + lo * stride)
                          : (float)u16(values + lo * stride);
    float b = type == 0   ? f32(values + hi * stride + j * 4)
              : type == 1 ? (float)(int16_t)u16(values + hi * stride)
                          : (float)u16(values + hi * stride);
    if (!finite(a) || !finite(b))
      return 5;
    out[j] = (float)((double)a + ((double)b - a) * t);
  }
  return 0;
}
static float nearest_even(float value) {
  int integer = (int)value;
  double remainder = (double)value - integer;
  if (remainder > .5 || (remainder == .5 && (integer & 1)))
    integer++;
  return (float)integer;
}
int appearance(uint32_t bytes, uint32_t index, float fraction) {
  if (bytes > sizeof(input) || bytes < 0x130 || !finite(fraction) || fraction < 0 || fraction > 1)
    return 1;
  size = bytes;
  if (u32(0) != 0x3032444d || u32(4) != 264)
    return 2;
  uint32_t n = u32(0x128), base = u32(0x12c);
  if (index >= n || n > 64 || !span(base, n, 0x1dc))
    return 2;
  uint32_t at = base + index * 0x1dc;
  float tmp[8];
  if (curve(at + 0x104, fraction, 12, 0, tmp, 3) || curve(at + 0x114, fraction, 2, 1, tmp + 3, 1) ||
      curve(at + 0x124, fraction, 8, 0, tmp + 4, 2) ||
      curve(at + 0x13c, fraction, 2, 2, tmp + 6, 1) ||
      curve(at + 0x14c, fraction, 2, 2, tmp + 7, 1))
    return 3;
  for (int i = 0; i < 3; i++) {
    if (tmp[i] < 0 || tmp[i] > 255)
      return 4;
    tmp[i] = nearest_even(tmp[i]) / 255.f;
  }
  tmp[3] = (float)((double)tmp[3] * (double)0.000030518509447574615f * 255.0);
  if (tmp[3] < 0 || tmp[3] > 255)
    return 4;
  tmp[3] = nearest_even(tmp[3]) / 255.f;
  tmp[6] = nearest_even(tmp[6]);
  tmp[7] = nearest_even(tmp[7]);
  for (int i = 0; i < 8; i++)
    appearance_values[i] = tmp[i];
  return 0;
}
