#include "binding.h"
static int finite_value(float f) {
  union {
    float f;
    uint32_t u;
  } x = {f};
  return (x.u & 0x7f800000) != 0x7f800000;
}
int br_particle_attachment(const float bone[16], const float position[3], const float world[16],
                           float output[16]) {
  if (!bone || !position || !world || !output)
    return 0;
  for (int i = 0; i < 16; i++)
    if (!finite_value(bone[i]) || !finite_value(world[i]))
      return 0;
  for (int i = 0; i < 3; i++)
    if (!finite_value(position[i]))
      return 0;
  float b[16], w[16], out[16];
  for (int i = 0; i < 16; i++)
    b[i] = bone[i];
  for (int j = 0; j < 3; j++)
    b[12 + j] = (float)((double)bone[8 + j] * position[2] + (double)bone[4 + j] * position[1] +
                        (double)bone[j] * position[0] + bone[12 + j]);
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++)
      w[c * 4 + r] =
          (float)((double)world[8 + r] * b[c * 4 + 2] + (double)world[4 + r] * b[c * 4 + 1] +
                  (double)world[12 + r] * b[c * 4 + 3] + (double)world[r] * b[c * 4]);
  /* Constant initialized explicitly by native830c7d..830cea. */
  for (int r = 0; r < 4; r++) {
    out[r] = w[4 + r];
    out[4 + r] = -w[r];
    out[8 + r] = w[8 + r];
    out[12 + r] = w[12 + r];
  }
  for (int i = 0; i < 16; i++)
    if (!finite_value(out[i]))
      return 0;
  for (int i = 0; i < 16; i++)
    output[i] = out[i];
  return 1;
}
uint32_t br_particle_flags(uint32_t s, uint32_t type) {
  uint32_t f = 2;
  if (s & 0x20000)
    f |= 4;
  if (s & 0x40000)
    f |= 8;
  const uint32_t a[] = {0x10,   0x40,   0x20,    0x800, 0x1000, 0x200,
                        0x2000, 0x4000, 0x80000, 2,     4,      0x400};
  const uint32_t b[] = {0x200,   0x800,   0x400,    0x2000, 0x4000,   0x10000,
                        0x40000, 0x80000, 0x800000, 0x20,   0x200000, 0x20000};
  for (unsigned i = 0; i < sizeof(a) / sizeof(a[0]); i++)
    if (s & a[i])
      f |= b[i];
  if (type == 2) {
    if (s & 0x80)
      f |= 0x1000;
    if (s & 0x100)
      f |= 0x8000;
  }
  return f;
}
/* Native4c1510 reduction constants reconstructed as their exact moduli. */
void br_particle_seed(uint32_t seed, uint32_t output[2]) {
  output[0] = seed;
  output[1] = ((seed % 59) << 10) | ((seed % 61) << 2) | ((seed % 53) << 18) | ((seed % 47) << 26);
}
