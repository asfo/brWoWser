#include "generation.h"
static uint32_t u32le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t rol(uint32_t x, unsigned n) {
  return (x << n) | (x >> (32 - n));
}
static int finite(float x) {
  union {
    float f;
    uint32_t u;
  } v = {x};
  return (v.u & 0x7f800000u) != 0x7f800000u;
}
uint32_t br_particle_rng_step(uint32_t state[2], const uint8_t table[256]) {
  uint32_t s = state[1];
  int a = (int)(s >> 24) - 4, b = (int)((s >> 16) & 255) - 12, c = (int)((s >> 8) & 255) - 24,
      d = (int)(s & 255) - 28;
  if (a < 0)
    a += 188;
  if (b < 0)
    b += 212;
  if (c < 0)
    c += 236;
  if (d < 0)
    d += 244;
  uint32_t delta = rol(u32le(table + a), 1) ^ rol(u32le(table + b), 2) ^ rol(u32le(table + c), 3) ^
                   u32le(table + d);
  state[0] += delta;
  state[1] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
  return state[0];
}
float br_particle_random_unit(uint32_t word) {
  union {
    uint32_t u;
    float f;
  } v = {(word & 0x7fffffu) | 0x3f800000u};
  return v.f - 1.f;
}
float br_particle_random_signed(uint32_t word) {
  union {
    uint32_t u;
    float f;
  } v = {(word & 0x7fffffu) | 0x3f800000u};
  return word & 0x80000000u ? 2.f - v.f : v.f - 2.f;
}
int br_particle_generate(BrParticleGenerator *s, float dt, const float matrix[16],
                         const uint8_t table[256], BrParticleSpawn spawn, void *context,
                         BrParticleGenerationResult *out) {
  if (!s || !matrix || !table || !spawn || !out || !finite(dt) || dt < 0 ||
      !finite(s->accumulator) || s->accumulator < -.5f || !finite(s->rate) ||
      !finite(s->rate_variation) || !finite(s->density) || s->density < 0 || s->density > 1 ||
      !finite(s->quality_metric) || s->free_slots > 65536)
    return 0;
  for (unsigned i = 0; i < 16; i++)
    if (!finite(matrix[i]))
      return 0;
  for (unsigned i = 0; i < 3; i++)
    if (!finite(s->previous_translation[i]))
      return 0;
  uint32_t rng[2] = {s->rng[0], s->rng[1]};
  float quality = 1.f;
  if (!(s->flags & 0x400000u)) {
    double q = 1. - ((double)s->quality_metric - 50.) * (double)0.019999999552965164f;
    quality = q < .25 ? .25f : q > 1. ? 1.f : (float)q;
  }
  uint32_t word = br_particle_rng_step(rng, table);
  float varied = (float)((double)br_particle_random_signed(word) * (double)s->rate_variation +
                         (double)s->rate);
  /* fst97d977 stores f32 but retains the extended register for non-burst flow. */
  double effective_wide = (double)s->density * (double)varied * (double)quality;
  float effective = (float)effective_wide;
  if (!finite(effective) || effective < 0 || effective > 100000000.f)
    return 0;
  double continuous_rate = (s->flags & 0x42u) == 0x42u ? (double)effective : effective_wide;
  double accumulator = continuous_rate * (double)dt + (double)s->accumulator;
  if (accumulator > 100000000.)
    return 0;
  s->rng[0] = rng[0];
  s->rng[1] = rng[1];
  out->burst_spawned = 0;
  out->continuous_spawned = 0;
  out->effective_rate = effective;
  out->quality = quality;
  if ((s->flags & 0x42u) == 0x42u) {
    uint32_t burst = (uint32_t)effective;
    while (burst && s->free_slots) {
      s->free_slots--;
      burst--;
      spawn(s, 0.f, matrix, context);
      out->burst_spawned++;
    }
    s->flags &= ~0x40u;
  }
  if ((s->flags & 3u) != 3u)
    return 1;
  s->accumulator = (float)accumulator;
  uint32_t requested = (uint32_t)(accumulator + .5);
  while (requested && s->free_slots) {
    float local[16];
    const float *transform = matrix;
    if (s->flags & 0x2000u) {
      float u = br_particle_random_unit(br_particle_rng_step(s->rng, table));
      for (unsigned i = 0; i < 16; i++)
        local[i] = matrix[i];
      for (unsigned i = 0; i < 3; i++) {
        float delta = matrix[12 + i] - s->previous_translation[i];
        local[12 + i] = (float)((double)delta * (double)u + (double)s->previous_translation[i]);
      }
      transform = local;
    }
    s->free_slots--;
    requested--;
    spawn(s, dt, transform, context);
    out->continuous_spawned++;
  }
  s->accumulator = (float)((double)s->accumulator - (double)out->continuous_spawned);
  return 1;
}
