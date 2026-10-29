/* Selected original-file CPU particle instance. Scheduling/shape/motion/quad
 * kernels are shared with their independent validation suites. No capture is
 * used as animation data. Pool capacity and deterministic fresh storage are
 * explicit browser policies, not native allocator/entropy equivalence. */
#include <stdint.h>
#include <math.h>
#include "binding.h"
#include "../particle_generation/generation.h"
#include "../particle_shape/shape.h"
#define EMITTERS 64
#define CAPACITY 256
#define MAX_BONES 1024
extern uint32_t input_ptr(void), metadata_ptr(void), controls_ptr(void), info_ptr(void),
    appearance_ptr(void);
extern int sample(uint32_t, uint32_t), appearance(uint32_t, uint32_t, float);
extern uint32_t particle_ptr(void), emitter_ptr(void), result_ptr(void);
extern int particle_step(float);
extern uint32_t globals_ptr(void), position_ptr(void), velocity_ptr(void), size_ptr(void),
    vertices_ptr(void);
extern int head_quad_angle(uint32_t, uint32_t, uint32_t, uint32_t, float),
    tail_quad(uint32_t, uint32_t, uint32_t, uint32_t, float, float);
typedef struct {
  BrParticleGenerator generator;
  BrParticle32 particles[CAPACITY];
  uint32_t count, burst_on, active[CAPACITY], free[CAPACITY], free_count;
  float matrix[16], inherited_velocity[3], inherited_time;
} Emitter;
static Emitter emitters[EMITTERS];
static float bones[MAX_BONES * 16], view[16];
static uint8_t valid[MAX_BONES], table[256];
static float output[EMITTERS * CAPACITY * 6 * 9];
static uint32_t batches[EMITTERS * 5], stats[8];
static uint32_t model_size, bone_count, emitter_count, initialized;
static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
uint32_t runtime_bones_ptr(void) {
  return (uint32_t)(uintptr_t)bones;
}
uint32_t runtime_bone_valid_ptr(void) {
  return (uint32_t)(uintptr_t)valid;
}
uint32_t runtime_view_ptr(void) {
  return (uint32_t)(uintptr_t)view;
}
uint32_t runtime_table_ptr(void) {
  return (uint32_t)(uintptr_t)table;
}
uint32_t runtime_vertices_ptr(void) {
  return (uint32_t)(uintptr_t)output;
}
uint32_t runtime_batches_ptr(void) {
  return (uint32_t)(uintptr_t)batches;
}
uint32_t runtime_stats_ptr(void) {
  return (uint32_t)(uintptr_t)stats;
}
static uint32_t word(const uint8_t *b, uint32_t o) {
  return b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}
static float scalar(const uint8_t *b, uint32_t o) {
  union {
    uint32_t u;
    float f;
  } v = {word(b, o)};
  return v.f;
}
static float rand_signed(uint32_t *r) {
  return br_particle_random_signed(br_particle_rng_step(r, table));
}
int runtime_reset(uint32_t bytes, uint32_t seed, uint32_t palette_count) {
  initialized = 0;
  if (!palette_count || palette_count > MAX_BONES || sample(bytes, 0))
    return 1;
  uint32_t *m = (uint32_t *)(uintptr_t)metadata_ptr(),
           count = ((uint32_t *)(uintptr_t)info_ptr())[0];
  if (count > EMITTERS)
    return 2;
  for (uint32_t i = 0; i < count; i++) {
    if (m[i * 10 + 3] >= palette_count)
      return 3;
    Emitter *e = emitters + i;
    e->count = e->burst_on = 0;
    e->inherited_time = 0;
    for (int j = 0; j < 3; j++)
      e->inherited_velocity[j] = 0;
    e->free_count = CAPACITY;
    for (uint32_t q = 0; q < CAPACITY; q++)
      e->free[q] = CAPACITY - 1 - q;
    e->generator.flags = br_particle_flags(m[i * 10 + 2], m[i * 10 + 1]);
    e->generator.free_slots = CAPACITY;
    e->generator.accumulator = 0;
    e->generator.density = 1;
    e->generator.quality_metric = 0;
    br_particle_seed(seed + i, e->generator.rng);
    for (int j = 0; j < 3; j++)
      e->generator.previous_translation[j] = 0;
    for (uint32_t p = 0; p < CAPACITY; p++)
      e->particles[p] = (BrParticle32){0};
  }
  model_size = bytes;
  bone_count = palette_count;
  emitter_count = count;
  initialized = 1;
  for (int i = 0; i < 8; i++)
    stats[i] = 0;
  return 0;
}
typedef struct {
  Emitter *emitter;
  BrParticleShape shape;
  int failure;
} Spawn;
static void spawn(BrParticleGenerator *g, float dt, const float matrix[16], void *context) {
  Spawn *s = context;
  if (s->failure || s->emitter->count >= CAPACITY) {
    s->failure = 1;
    return;
  }
  uint32_t slot = s->emitter->free[s->emitter->free_count - 1];
  BrParticle32 *p = s->emitter->particles + slot;
  if (!br_particle_shape_init32(&s->shape, dt, matrix, g->rng, table, p)) {
    s->failure = 1;
    return;
  }
  s->emitter->free_count--;
  s->emitter->active[s->emitter->count++] = slot;
}
/* Native row-matrix multiplication, transposed for the browser column layout. */
static void product(const float *a, const float *b, float *out) {
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++)
      out[c * 4 + r] = (float)((double)a[8 + r] * b[c * 4 + 2] + (double)a[4 + r] * b[c * 4 + 1] +
                               (double)a[12 + r] * b[c * 4 + 3] + (double)a[r] * b[c * 4]);
}
static int update_one(uint32_t index, float dt) {
  uint32_t *m = (uint32_t *)(uintptr_t)metadata_ptr() + index * 10;
  float *c = (float *)(uintptr_t)controls_ptr() + index * 11;
  uint8_t *b = (uint8_t *)(uintptr_t)input_ptr();
  uint32_t at = m[8];
  Emitter *e = emitters + index;
  BrParticleGenerator *g = &e->generator;
  if (m[3] >= bone_count || !valid[m[3]])
    return 2;
  float position[3] = {scalar(b, at + 8), scalar(b, at + 12), scalar(b, at + 16)};
  if (!br_particle_attachment(bones + m[3] * 16, position, identity, e->matrix))
    return 3;
  uint32_t base_flags = br_particle_flags(m[2], m[1]);
  if ((m[2] & 0x10000) && m[6] * m[7] > 1)
    base_flags |= 0x100000;
  g->flags = (g->flags & 0x40) | base_flags;
  g->rate = c[6];
  g->rate_variation = scalar(b, at + 0xc4);
  g->free_slots = CAPACITY - e->count;
  if (m[2] & 0x8000) {
    uint32_t on = c[10] > 0 && c[6] > 0;
    if (on && !e->burst_on)
      g->flags |= 0x40;
    e->burst_on = on;
  } else if (c[10] > 0)
    g->flags |= 1;
  float displacement[3];
  double length2 = 0;
  for (int j = 0; j < 3; j++) {
    displacement[j] = (float)((double)e->matrix[12 + j] - g->previous_translation[j]);
    length2 += (double)displacement[j] * displacement[j];
  }
  float follow = 0;
  if ((g->flags & 0x80000) && dt > 0) {
    float x0 = scalar(b, at + 0x1b0), y0 = scalar(b, at + 0x1b4), x1 = scalar(b, at + 0x1b8),
          y1 = scalar(b, at + 0x1bc);
    float slope = 0, intercept = 0;
    if (fabs((double)x1 - x0) > 0.0000002384185791015625) {
      slope = (float)(((double)y1 - y0) / ((double)x1 - x0));
      intercept = (float)((double)y0 - (double)slope * x0);
    }
    follow = fminf(1, fmaxf(0, (float)(sqrt(length2) / dt * slope + intercept)));
  }
  if (g->flags & 0x800) {
    e->inherited_time = (float)((double)e->inherited_time + dt);
    if (e->inherited_time >= 0.03333333507180214f) {
      for (int j = 0; j < 3; j++)
        e->inherited_velocity[j] = e->count
                                       ? (float)((double)displacement[j] * (1 - (double)follow) /
                                                 ((double)e->inherited_time * 29.999998092651367f))
                                       : 0;
      e->inherited_time = 0;
    }
  }
  Spawn s = {e,
             {m[1],
              g->flags,
              0,
              0,
              0,
              0,
              0,
              c[0],
              c[1],
              c[9],
              {e->inherited_velocity[0], e->inherited_velocity[1], e->inherited_velocity[2]}},
             0};
  s.shape.e234 = c[7];
  s.shape.e238 = c[8];
  if (m[1] == 1) {
    s.shape.e23c = c[2];
    s.shape.e240 = c[3];
  } else {
    s.shape.e23c = c[8] - c[7];
    s.shape.e240 = c[2];
    s.shape.e244 = c[3];
  }
  BrParticleGenerationResult result;
  if (!br_particle_generate(g, dt, e->matrix, table, spawn, &s, &result) || s.failure)
    return 4;
  float *motion = (float *)(uintptr_t)emitter_ptr();
  for (int i = 0; i < 131; i++)
    motion[i] = 0;
  union {
    float f;
    uint32_t u;
  } flag = {.u = g->flags};
  motion[0x134 / 4] = flag.f;
  motion[0xb4 / 4] = c[4];
  motion[0x168 / 4] = scalar(b, at + 0x174);
  for (int j = 0; j < 3; j++) {
    motion[0x16c / 4 + j] = scalar(b, at + 0x1a0 + j * 4);
    motion[0x1b4 / 4 + j] = e->matrix[12 + j];
    motion[0x200 / 4 + j] = (float)((double)displacement[j] * follow);
  }
  motion[0x178 / 4] = scalar(b, at + 0x1ac);
  float life = c[5];
  if (life < .001f)
    life = .001f;
  for (uint32_t p = 0; p < e->count;) {
    uint32_t slot = e->active[p];
    BrParticle32 *part = e->particles + slot;
    part->age = (float)((double)part->age + dt);
    if (part->age >= life) {
      e->free[e->free_count++] = slot;
      e->active[p] = e->active[--e->count];
      continue;
    }
    uint32_t *source = (uint32_t *)part, *destination = (uint32_t *)(uintptr_t)particle_ptr();
    for (int j = 0; j < 8; j++)
      destination[j] = source[j];
    int alive = particle_step(dt);
    if (alive < 0)
      return 5;
    if (!alive) {
      e->free[e->free_count++] = slot;
      e->active[p] = e->active[--e->count];
      continue;
    }
    uint32_t *updated = (uint32_t *)(uintptr_t)result_ptr();
    for (int j = 0; j < 8; j++)
      source[j] = updated[j];
    p++;
  }
  for (int j = 0; j < 3; j++)
    g->previous_translation[j] = e->matrix[12 + j];
  return 0;
}
int runtime_step(uint32_t sequence_ms, float dt) {
  if (!initialized || !isfinite(dt) || dt < 0 || dt > .100001f)
    return 1;
  if (sample(model_size, sequence_ms))
    return 2;
  for (uint32_t i = 0; i < emitter_count; i++) {
    int code = update_one(i, dt);
    if (code) {
      initialized = 0;
      return 10 + code;
    }
  }
  return 0;
}
int runtime_draw(void) {
  stats[0] = stats[1] = stats[2] = stats[3] = stats[4] = 0;
  if (!initialized)
    return 1;
  for (int i = 0; i < 16; i++)
    if (!isfinite(view[i]))
      return 2;
  uint32_t *m = (uint32_t *)(uintptr_t)metadata_ptr();
  float *c = (float *)(uintptr_t)controls_ptr();
  uint8_t *b = (uint8_t *)(uintptr_t)input_ptr();
  float *globals = (float *)(uintptr_t)globals_ptr();
  for (int i = 0; i < 41; i++)
    globals[i] = 0;
  const float corners[8] = {-1, 1, -1, -1, 1, 1, 1, -1}, uv[8] = {0, 0, 0, 1, 1, 0, 1, 1};
  for (int i = 0; i < 8; i++) {
    globals[25 + i] = corners[i];
    globals[33 + i] = uv[i];
  }
  for (uint32_t i = 0; i < emitter_count; i++) {
    Emitter *e = emitters + i;
    uint32_t *meta = m + i * 10, at = meta[8], flags = e->generator.flags;
    float matrix[16];
    if (flags & 0x200)
      product(view, e->matrix, matrix);
    else
      for (int k = 0; k < 16; k++)
        matrix[k] = view[k];
    for (int k = 0; k < 16; k++)
      globals[k] = matrix[k];
    uint32_t start = stats[0];
    stats[2] += e->count;
    for (uint32_t j = 0; j < e->count; j++) {
      BrParticle32 *p = e->particles + e->active[j];
      float life = c[i * 11 + 5];
      if (life < .001f)
        life = .001f;
      float fraction = (float)((double)p->age / life);
      if (fraction < 0 || fraction > 1 || appearance(model_size, i, fraction)) {
        stats[3]++;
        continue;
      }
      float *a = (float *)(uintptr_t)appearance_ptr();
      uint32_t rng[2];
      br_particle_seed(p->seed_1e, rng);
      uint32_t head_cell = (uint32_t)a[6];
      if (!word(b, at + 0x13c) && (flags & 0x100000))
        head_cell =
            (uint32_t)(((uint64_t)br_particle_rng_step(rng, table) * meta[6] * meta[7]) >> 32);
      float factor = fmaxf(.01f, (float)(1 + (double)rand_signed(rng) * scalar(b, at + 0x134)));
      float *sizes = (float *)(uintptr_t)size_ptr();
      sizes[0] = (float)((double)a[4] * factor);
      sizes[1] = (float)((double)a[5] * factor);
      if (flags & 0x400) {
        double scale =
            sqrt((double)e->matrix[0] * e->matrix[0] + (double)e->matrix[1] * e->matrix[1] +
                 (double)e->matrix[2] * e->matrix[2]);
        sizes[0] = (float)(sizes[0] * scale);
        sizes[1] = (float)(sizes[1] * scale);
      }
      float *pos = (float *)(uintptr_t)position_ptr(), *vel = (float *)(uintptr_t)velocity_ptr();
      for (int k = 0; k < 3; k++) {
        pos[k] = p->position[k];
        vel[k] = p->velocity[k];
      }
      br_particle_seed(p->seed_1e, rng);
      float bias = scalar(b, at + 0x178), speed = scalar(b, at + 0x180),
            biasvar = scalar(b, at + 0x17c), speedvar = scalar(b, at + 0x184);
      if (biasvar)
        bias = (float)((double)rand_signed(rng) * biasvar + bias);
      if (speedvar)
        speed = (float)((double)rand_signed(rng) * speedvar + speed);
      float angle = (float)((double)p->age * speed + bias);
      if ((flags & 0x10000) && ((uintptr_t)p & 0x20))
        angle = -angle;
      int code = (flags & 4) ? head_quad_angle(flags, meta[6], meta[7], head_cell, angle)
                             : tail_quad(flags, meta[6], meta[7], (uint32_t)a[7], p->age,
                                         scalar(b, at + 0x15c));
      if (code) {
        stats[3]++;
        continue;
      }
      float *v = (float *)(uintptr_t)vertices_ptr();
      const int order[6] = {0, 1, 2, 2, 1, 3};
      for (int k = 0; k < 6; k++) {
        float *out = output + stats[0] * 9;
        for (int n = 0; n < 5; n++)
          out[n] = v[order[k] * 5 + n];
        for (int n = 0; n < 4; n++)
          out[5 + n] = a[n];
        stats[0]++;
      }
    }
    if (stats[0] > start) {
      uint32_t *batch = batches + stats[1]++ * 5;
      batch[0] = i;
      batch[1] = meta[4];
      batch[2] = meta[5];
      batch[3] = start;
      batch[4] = stats[0] - start;
    }
  }
  stats[4] = emitter_count;
  return 0;
}
