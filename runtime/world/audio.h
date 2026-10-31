#ifndef BRWOWSER_WORLD_AUDIO_H
#define BRWOWSER_WORLD_AUDIO_H
#include <stdint.h>
uint32_t bw_world_audio_load(uint32_t kind, const uint8_t *bytes, uint32_t length);
void bw_world_audio_sample(uint32_t area, float minutes);
uint32_t bw_world_audio_next(uint32_t channel);
const float *bw_world_audio_info(void);
const char *bw_world_audio_paths(void);
#endif
