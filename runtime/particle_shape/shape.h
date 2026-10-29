#ifndef BRWOWSER_PARTICLE_SHAPE_H
#define BRWOWSER_PARTICLE_SHAPE_H
#include <stdint.h>
/* Explicit native runtime fields. No M2-to-runtime track evaluation implied. */
typedef struct {
  uint32_t type, flags;
  float e234, e238, e23c, e240, e244;
  float e0b0, e0b8, e0bc, e1e0[3];
} BrParticleShape;
typedef struct {
  float age, position[3], velocity[3];
  uint16_t untouched_1c, seed_1e;
} BrParticle32;
/* Selected 9815c0/981950 virtual+8 branches. type1/2 only, flag40000
 * explicitly unsupported. rng words use the shared caller-owned algorithm table.
 * On success returns1 and updates rng and fields native actually writes;
 * untouched_1c remains unchanged. Invalid input returns0 without mutation.
 * Trigonometric/native x87 rounding equivalence remains a separate oracle. */
int br_particle_shape_init32(const BrParticleShape *, float, const float[16], uint32_t rng[2],
                             const uint8_t table[256], BrParticle32 *);
#endif
