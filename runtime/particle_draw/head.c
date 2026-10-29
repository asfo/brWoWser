/* Current native97c282 zero-orientation head quad. Inputs expose evaluated
 * globals explicitly; no guessed camera basis or corner order. */
#include <stdint.h>

static float globals[41], particle_position[3], size_xy[2], vertices[20];
static int valid_float(float f) {
  union {
    float f;
    uint32_t u;
  } v = {f};
  return (v.u & 0x7f800000) != 0x7f800000;
}
uint32_t globals_ptr(void) {
  return (uint32_t)(uintptr_t)globals;
}
uint32_t position_ptr(void) {
  return (uint32_t)(uintptr_t)particle_position;
}
uint32_t size_ptr(void) {
  return (uint32_t)(uintptr_t)size_xy;
}
uint32_t vertices_ptr(void) {
  return (uint32_t)(uintptr_t)vertices;
}
/* Native6f7a60 uses a cubic quadrant approximation, not libm sin/cos. */
static float wave(float value) {
  int quadrant = (int)value;
  if (value <= 0)
    quadrant--;
  float t = (float)((double)value - quadrant);
  double v = 1.0 - (6.0 - 4.0 * (double)t) * (double)t * (double)t;
  return (float)((quadrant & 1) ? -v : v);
}
static void native_sincos(float angle, float *sine, float *cosine) {
  double phase = (double)angle * (double)0.31830987334251404f;
  *sine = wave((float)(phase - 0.5));
  *cosine = wave((float)phase);
}
static int build_quad(uint32_t flags, uint32_t rows, uint32_t columns, uint32_t cell, float angle) {
  if (!(flags & 4) || (flags & 0x200000) || !valid_float(angle) || angle > 1000000 ||
      angle < -1000000 || (angle != 0 && (flags & 0x4000)) || !rows || !columns || rows > 1024 ||
      columns > 1024 || (rows & (rows - 1)) || (columns & (columns - 1)))
    return 1;
  for (int i = 0; i < 41; i++)
    if (!valid_float(globals[i]))
      return 2;
  for (int i = 0; i < 3; i++)
    if (!valid_float(particle_position[i]))
      return 2;
  for (int i = 0; i < 2; i++)
    if (!valid_float(size_xy[i]))
      return 2;
  float center[3], tmp[20];
  for (int j = 0; j < 3; j++)
    center[j] = (float)((double)globals[8 + j] * particle_position[2] +
                        (double)globals[4 + j] * particle_position[1] +
                        (double)globals[j] * particle_position[0] + globals[12 + j]);
  float u = (float)(cell & (columns - 1)) / columns, v = (float)(cell / columns) / rows,
        us = 1.f / columns, vs = 1.f / rows;
  float cosine, sine;
  native_sincos(angle, &sine, &cosine);
  for (int i = 0; i < 4; i++) {
    double x = (double)globals[25 + i * 2] * size_xy[0],
           y = (double)globals[26 + i * 2] * size_xy[1];
    if (angle != 0) {
      double old_x = x;
      x = x * cosine - y * sine;
      y = old_x * sine + y * cosine;
    }
    for (int j = 0; j < 3; j++)
      tmp[i * 5 + j] =
          (float)((double)center[j] +
                  ((flags & 0x4000) ? (double)globals[16 + j] * x + (double)globals[19 + j] * y
                   : j == 0         ? x
                   : j == 1         ? y
                                    : 0));
    tmp[i * 5 + 3] = (float)((double)globals[33 + i * 2] * us + u);
    tmp[i * 5 + 4] = (float)((double)globals[34 + i * 2] * vs + v);
  }
  for (int i = 0; i < 20; i++)
    if (!valid_float(tmp[i]))
      return 3;
  for (int i = 0; i < 20; i++)
    vertices[i] = tmp[i];
  return 0;
}

int head_quad(uint32_t flags, uint32_t rows, uint32_t columns, uint32_t cell, float orientation0,
              float orientation1) {
  if (orientation0 != 0 || orientation1 != 0)
    return 1;
  return build_quad(flags, rows, columns, cell, 0);
}
int head_quad_angle(uint32_t flags, uint32_t rows, uint32_t columns, uint32_t cell, float angle) {
  return build_quad(flags, rows, columns, cell, angle);
}

/* Selected non-degenerate native97cc10 tail branch. Explicit evaluated inputs;
 * no approximation of a missing camera, attachment or particle clock. */
static float particle_velocity[3];
uint32_t velocity_ptr(void) {
  return (uint32_t)(uintptr_t)particle_velocity;
}
int tail_quad(uint32_t flags, uint32_t rows, uint32_t columns, uint32_t cell, float age,
              float tail_time) {
  if (!(flags & 8) || (flags & 4) || !rows || !columns || rows > 1024 || columns > 1024 ||
      (rows & (rows - 1)) || (columns & (columns - 1)) || !valid_float(age) ||
      !valid_float(tail_time) || age < 0 || tail_time < 0)
    return 1;
  for (int i = 0; i < 41; i++)
    if (!valid_float(globals[i]))
      return 2;
  for (int i = 0; i < 3; i++)
    if (!valid_float(particle_position[i]) || !valid_float(particle_velocity[i]))
      return 2;
  for (int i = 0; i < 2; i++)
    if (!valid_float(size_xy[i]))
      return 2;
  if ((flags & 0x20000) && tail_time > age)
    tail_time = age;
  float center[3], delta[3], end[3], tmp[20];
  for (int j = 0; j < 3; j++) {
    center[j] = (float)((double)globals[8 + j] * particle_position[2] +
                        (double)globals[4 + j] * particle_position[1] +
                        (double)globals[j] * particle_position[0] + globals[12 + j]);
    delta[j] = (float)(-((double)globals[4 + j] * particle_velocity[1] +
                         (double)globals[8 + j] * particle_velocity[2] +
                         (double)globals[j] * particle_velocity[0]) *
                       tail_time);
    end[j] = (float)((double)center[j] + delta[j]);
  }
  double length2 = (double)delta[0] * delta[0] + (double)delta[1] * delta[1];
  /* Native97d202 falls back to an unrotated view-plane quad when the tail
  * projects below the threshold, including stationary particles. */
  if (length2 < 0.0007716049440205097) {
    float u = (float)(cell & (columns - 1)) / columns, v = (float)(cell / columns) / rows;
    for (int i = 0; i < 4; i++) {
      tmp[i * 5] = (float)((double)center[0] + (double)globals[25 + i * 2] * size_xy[0]);
      tmp[i * 5 + 1] = (float)((double)center[1] + (double)globals[26 + i * 2] * size_xy[1]);
      tmp[i * 5 + 2] = center[2];
      tmp[i * 5 + 3] = (float)((double)globals[33 + i * 2] / columns + u);
      tmp[i * 5 + 4] = (float)((double)globals[34 + i * 2] / rows + v);
    }
    for (int i = 0; i < 20; i++)
      if (!valid_float(tmp[i]))
        return 3;
    for (int i = 0; i < 20; i++)
      vertices[i] = tmp[i];
    return 0;
  }
  double inverse = 1.0 / __builtin_sqrt(length2);
  float x = (float)((double)delta[1] * size_xy[0] * inverse),
        y = (float)((double)delta[0] * size_xy[1] * inverse);
  float u = (float)(cell & (columns - 1)) / columns, v = (float)(cell / columns) / rows;
  for (int i = 0; i < 4; i++) {
    const float *base = i < 2 ? center : end;
    int positive = !(i & 1);
    tmp[i * 5] = (float)((double)base[0] + (positive ? -x : x));
    tmp[i * 5 + 1] = (float)((double)base[1] + (positive ? y : -y));
    tmp[i * 5 + 2] = base[2];
    tmp[i * 5 + 3] = (float)((double)globals[33 + i * 2] / columns + u);
    tmp[i * 5 + 4] = (float)((double)globals[34 + i * 2] / rows + v);
  }
  for (int i = 0; i < 20; i++)
    if (!valid_float(tmp[i]))
      return 3;
  for (int i = 0; i < 20; i++)
    vertices[i] = tmp[i];
  return 0;
}
