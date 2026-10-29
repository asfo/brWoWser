#include "shape.h"
#include "../particle_generation/generation.h"
#include <math.h>
static int valid(float v) {
  return isfinite(v) && fabsf(v) <= 1000000;
}
static float unit(uint32_t rng[2], const uint8_t *t) {
  return br_particle_random_unit(br_particle_rng_step(rng, t));
}
static float signrand(uint32_t rng[2], const uint8_t *t) {
  return br_particle_random_signed(br_particle_rng_step(rng, t));
}
static void rotate(const float m[16], const float v[3], float out[3]) {
  for (int i = 0; i < 3; i++)
    out[i] = (float)((double)m[i] * v[0] + (double)m[4 + i] * v[1] + (double)m[8 + i] * v[2]);
}
int br_particle_shape_init32(const BrParticleShape *s, float age, const float m[16],
                             uint32_t rng[2], const uint8_t table[256], BrParticle32 *out) {
  if (!s || !m || !rng || !table || !out || (s->type != 1 && s->type != 2) ||
      (s->flags & 0x40000) || !valid(age))
    return 0;
  const float *values = &s->e234;
  for (unsigned i = 0; i < 11; i++)
    if (!valid(values[i]))
      return 0;
  for (unsigned i = 0; i < 16; i++)
    if (!valid(m[i]))
      return 0;
  uint32_t state[2] = {rng[0], rng[1]};
  BrParticle32 p = *out;
  p.age = (float)((double)unit(state, table) * age);
  p.seed_1e = (uint16_t)br_particle_rng_step(state, table);
  float direction[3], speed;
  if (s->type == 1) {
    float y = signrand(state, table), x = signrand(state, table);
    p.position[0] = (float)((double)x * s->e234 * 0.5);
    p.position[1] = (float)((double)y * s->e238 * 0.5);
    p.position[2] = 0;
    speed = (float)(((double)signrand(state, table) * s->e0b8 + 1) * s->e0b0);
    if (s->e0bc != 0) {
      double x0 = p.position[0], y0 = p.position[1], z0 = (double)p.position[2] - s->e0bc;
      double length = sqrt(x0 * x0 + y0 * y0 + z0 * z0);
      if (!(length > 0) || !isfinite(length))
        return 0;
      direction[0] = (float)(x0 / length * speed);
      direction[1] = (float)(y0 / length * speed);
      direction[2] = (float)(z0 / length * speed);
    } else {
      float a = (float)((double)signrand(state, table) * s->e23c),
            b = (float)((double)signrand(state, table) * s->e240);
      float ca = (float)cos(a), sa = (float)sin(a), cb = (float)cos(b), sb = (float)sin(b);
      direction[0] = (float)((double)cb * sa * speed);
      direction[1] = (float)((double)sa * sb * speed);
      direction[2] = (float)((double)ca * speed);
    }
  } else {
    float radius = (float)((double)unit(state, table) * s->e23c + s->e234);
    float a = (float)((double)signrand(state, table) * s->e240),
          b = (float)((double)signrand(state, table) * s->e244);
    float ca = (float)cos(a), sa = (float)sin(a), cb = (float)cos(b), sb = (float)sin(b);
    direction[0] = (float)((double)cb * ca);
    direction[1] = (float)((double)sb * ca);
    direction[2] = sa;
    for (unsigned i = 0; i < 3; i++)
      p.position[i] = (float)((double)direction[i] * radius);
    if (s->e0bc != 0) {
      double x = p.position[0], y = p.position[1], z = (double)p.position[2] - s->e0bc;
      double length = sqrt(x * x + y * y + z * z);
      direction[0] = (float)x;
      direction[1] = (float)y;
      direction[2] = (float)z;
      if (x * x + y * y + z * z > 0x1p-22) {
        direction[0] = (float)(x / length);
        direction[1] = (float)(y / length);
        direction[2] = (float)(z / length);
      }
    } else if (s->flags & 0x8000) {
      direction[0] = direction[1] = 0;
      direction[2] = 1;
    }
    speed = (float)(((double)signrand(state, table) * s->e0b8 + 1) * s->e0b0);
    for (unsigned i = 0; i < 3; i++)
      direction[i] = (float)((double)direction[i] * speed);
  }
  if (s->flags & 0x200) {
    for (unsigned i = 0; i < 3; i++)
      p.velocity[i] = direction[i];
  } else {
    float position[3];
    rotate(m, direction, p.velocity);
    for (unsigned i = 0; i < 3; i++)
      position[i] = (float)((double)m[i] * p.position[0] + (double)m[4 + i] * p.position[1] +
                            (double)m[8 + i] * p.position[2] + m[12 + i]);
    for (unsigned i = 0; i < 3; i++)
      p.position[i] = position[i];
  }
  if (s->flags & 0x800) {
    double factor = (double)signrand(state, table) * s->e0b8 + 1;
    for (unsigned i = 0; i < 3; i++)
      p.velocity[i] = (float)((double)p.velocity[i] + factor * s->e1e0[i]);
  }
  for (unsigned i = 0; i < 3; i++)
    if (!isfinite(p.position[i]) || !isfinite(p.velocity[i]))
      return 0;
  if (!isfinite(p.age))
    return 0;
  *out = p;
  rng[0] = state[0];
  rng[1] = state[1];
  return 1;
}
