/* Selected 32-byte particle update: current specimen 0x979bb0..0x979d59.
 * Caller advances age; this kernel neither spawns nor draws particles.
 * Float stores follow the observed boundaries; extended x87 rounding is not claimed.
 */
#include <stdint.h>
static float particle[8], emitter[131], result[8];
static int finite(float x) {
  union {
    float f;
    uint32_t u;
  } v = {x};
  return (v.u & 0x7f800000) != 0x7f800000;
}
static float snap(float x) {
  return x != 0 && x < 1e-8f && x > -1e-8f ? 0 : x;
}
uint32_t particle_ptr(void) {
  return (uint32_t)(uintptr_t)particle;
}
uint32_t emitter_ptr(void) {
  return (uint32_t)(uintptr_t)emitter;
}
uint32_t result_ptr(void) {
  return (uint32_t)(uintptr_t)result;
}
int particle_step(float dt) {
  if (!finite(dt) || dt < 0)
    return -1;
  for (int i = 0; i < 7; i++)
    if (!finite(particle[i]))
      return -1;
  const int fields[] = {0xb4,  0x168, 0x16c, 0x170, 0x174, 0x178,
                        0x1b4, 0x1b8, 0x1bc, 0x200, 0x204, 0x208};
  for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
    if (!finite(emitter[fields[i] / 4]))
      return -1;
  union {
    float f;
    uint32_t u;
  } bits = {emitter[0x134 / 4]};
  uint32_t flags = bits.u;
  float p[8];
  for (int i = 0; i < 8; i++)
    p[i] = particle[i];
  if (emitter[0x178 / 4] > p[0])
    for (int i = 0; i < 3; i++)
      p[4 + i] = snap((float)((double)p[4 + i] + (double)emitter[0x16c / 4 + i] * dt));
  if ((flags & 0x80000) && 2.0 * (double)dt < p[0])
    for (int i = 0; i < 3; i++)
      p[1 + i] = (float)((double)p[1 + i] + emitter[0x200 / 4 + i]);
  float displacement[3];
  for (int i = 0; i < 3; i++)
    displacement[i] = (float)((double)p[4 + i] * dt);
  p[1] = (float)((double)p[1] + (double)p[4] * dt);
  p[2] = (float)((double)p[2] + (double)p[5] * dt);
  p[3] = (float)((double)p[3] + (double)p[6] * dt - (double)emitter[0xb4 / 4] * dt * dt * 0.5);
  p[6] = (float)((double)p[6] - (double)emitter[0xb4 / 4] * dt);
  if (emitter[0x168 / 4] != 0) {
    double amount = (double)dt * emitter[0x168 / 4];
    if (amount > 1)
      amount = 1;
    for (int i = 0; i < 3; i++)
      p[4 + i] = (float)((double)p[4 + i] - (double)p[4 + i] * amount);
  }
  for (int i = 0; i < 3; i++)
    p[4 + i] = snap(p[4 + i]);
  int alive = 1;
  if (flags & 0x1000) {
    double dot = 0;
    for (int i = 0; i < 3; i++)
      dot += ((double)p[1 + i] - ((flags & 0x200) ? 0 : emitter[0x1b4 / 4 + i])) * displacement[i];
    if (dot > 0)
      alive = 0;
  }
  for (int i = 0; i < 7; i++)
    if (!finite(p[i]))
      return -2;
  for (int i = 0; i < 8; i++)
    result[i] = p[i];
  return alive;
}
/* Bounded batch for one emitter. Output publication is atomic on validation failure. */
#define PARTICLE_CAPACITY 4096u
static float batch_input[PARTICLE_CAPACITY * 8], batch_output[PARTICLE_CAPACITY * 8],
    batch_scratch[PARTICLE_CAPACITY * 8];
static int batch_status[PARTICLE_CAPACITY], status_scratch[PARTICLE_CAPACITY];
uint32_t batch_input_ptr(void) {
  return (uint32_t)(uintptr_t)batch_input;
}
uint32_t batch_output_ptr(void) {
  return (uint32_t)(uintptr_t)batch_output;
}
uint32_t batch_status_ptr(void) {
  return (uint32_t)(uintptr_t)batch_status;
}
int particle_step_batch(uint32_t count, float dt) {
  if (count > PARTICLE_CAPACITY || !finite(dt) || dt < 0)
    return -1;
  for (uint32_t i = 0; i < count; i++) {
    for (int j = 0; j < 8; j++)
      particle[j] = batch_input[i * 8 + j];
    int code = particle_step(dt);
    if (code < 0)
      return code;
    status_scratch[i] = code;
    for (int j = 0; j < 8; j++)
      batch_scratch[i * 8 + j] = result[j];
  }
  for (uint32_t i = 0; i < count; i++) {
    batch_status[i] = status_scratch[i];
    for (int j = 0; j < 8; j++)
      batch_output[i * 8 + j] = batch_scratch[i * 8 + j];
  }
  return 0;
}
