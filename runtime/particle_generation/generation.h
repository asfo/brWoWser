#ifndef BRWOWSER_PARTICLE_GENERATION_H
#define BRWOWSER_PARTICLE_GENERATION_H
#include <stdint.h>
/* Selected current-native97d8c0 scheduler. The caller supplies owner algorithm
 * table and shape-specific init callback; this is not a complete emitter. */
typedef struct {
  uint32_t flags, free_slots, rng[2];
  float accumulator, rate, rate_variation, density, quality_metric, previous_translation[3];
} BrParticleGenerator;
typedef void (*BrParticleSpawn)(BrParticleGenerator *, float, const float[16], void *);
typedef struct {
  uint32_t burst_spawned, continuous_spawned;
  float effective_rate, quality;
} BrParticleGenerationResult;
uint32_t br_particle_rng_step(uint32_t state[2], const uint8_t table[256]);
float br_particle_random_unit(uint32_t word);
float br_particle_random_signed(uint32_t word);
/* Returns0 for nonfinite/negative rates/clocks, invalid density/accumulator,
 * >65536 free slots or null callback/table. Validation occurs before mutation.
 * Callback owns shape/RNG consumption and must not mutate generator fields
 * other than rng; free_slots is consumed before invoking it. */
int br_particle_generate(BrParticleGenerator *, float, const float[16], const uint8_t[256],
                         BrParticleSpawn, void *, BrParticleGenerationResult *);
#endif
