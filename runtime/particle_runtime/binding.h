#ifndef BR_PARTICLE_BINDING_H
#define BR_PARTICLE_BINDING_H
#include <stdint.h>
/* Selected current 830c3c..830d31. Column-major model-space palette, original
 * file position and explicit instance world. Output is W*bone*T(position)*Rz90.
 * Native 97ac20 optionally removes the caller-supplied base instance transform;
 * the selected model-space browser instance supplies identity world. */
int br_particle_attachment(const float bone[16], const float position[3], const float world[16],
                           float output[16]);
uint32_t br_particle_flags(uint32_t source, uint32_t type);
void br_particle_seed(uint32_t seed, uint32_t output[2]);
#endif
