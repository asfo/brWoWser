#include <stdint.h>
char *bw_minimap_directory(void);
uint32_t bw_minimap_index(const uint8_t *, uint32_t);
const float *bw_minimap_sample(float, float, float);
const char *bw_minimap_paths(void);

void bw_minimap_zoom(uint32_t);
