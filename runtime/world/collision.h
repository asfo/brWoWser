#ifndef BRWOWSER_WORLD_COLLISION_H
#define BRWOWSER_WORLD_COLLISION_H
#include <stdint.h>
uint32_t bw_collision_mesh(uint32_t key, uint32_t kind, const uint8_t *bytes, uint32_t length);
uint32_t bw_collision_instance(uint32_t id, uint32_t key, const float *matrix);
int bw_collision_blocked(const float *feet, float radius, float height);
float bw_collision_floor(float x, float y, float low, float high);
uint32_t bw_collision_triangles(void);
uint32_t bw_collision_instances(void);
#endif
