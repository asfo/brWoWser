#include "audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  uint8_t *bytes;
  uint32_t length, count, fields, strings;
} Table;
static Table tables[5];
static char paths[3][512];
/* sound ID, volume, revision, silence minimum/maximum (ms), selected delay */
static float info[18];
static uint32_t last_area = UINT32_MAX, last_night = UINT32_MAX, rng = 0x42525753;
static uint32_t u32(const uint8_t *b, uint32_t at) {
  return b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24;
}
static uint32_t value(uint32_t table, uint32_t row, uint32_t field) {
  Table *t = &tables[table];
  return u32(t->bytes, 20 + (row * t->fields + field) * 4);
}
static float scalar(uint32_t table, uint32_t row, uint32_t field) {
  union {
    uint32_t u;
    float f;
  } v = {.u = value(table, row, field)};
  return v.f;
}
static uint32_t row_for(uint32_t table, uint32_t id) {
  Table *t = &tables[table];
  for (uint32_t i = 0; i < t->count; i++)
    if (value(table, i, 0) == id)
      return i;
  return UINT32_MAX;
}
static const char *string(uint32_t table, uint32_t row, uint32_t field) {
  Table *t = &tables[table];
  uint32_t off = value(table, row, field);
  if (off >= t->length - t->strings)
    return NULL;
  const char *s = (const char *)(t->bytes + t->strings + off);
  return memchr(s, 0, t->length - t->strings - off) ? s : NULL;
}
static uint32_t random_next(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
uint32_t bw_world_audio_load(uint32_t kind, const uint8_t *b, uint32_t n) {
  const uint32_t fields[] = {36, 8, 3, 5, 30};
  if (kind >= 5 || n < 20 || u32(b, 0) != 0x43424457 || u32(b, 8) != fields[kind] ||
      u32(b, 12) != fields[kind] * 4)
    return 1;
  uint32_t count = u32(b, 4), record = fields[kind] * 4;
  if (count > (n - 20) / record)
    return 2;
  uint32_t strings = 20 + count * record;
  if (u32(b, 16) != n - strings)
    return 2;
  uint8_t *copy = malloc(n);
  if (!copy)
    return 3;
  memcpy(copy, b, n);
  free(tables[kind].bytes);
  tables[kind] = (Table){copy, n, count, fields[kind], strings};
  last_area = last_night = UINT32_MAX;
  return 0;
}
static void choose(uint32_t channel, uint32_t sound, uint32_t minimum, uint32_t maximum,
                   int delay) {
  float *out = info + channel * 6;
  out[0] = sound;
  out[1] = 0;
  out[2]++;
  out[3] = minimum;
  out[4] = maximum;
  out[5] = delay ? minimum + (maximum > minimum ? random_next() % (maximum - minimum + 1) : 0) : 0;
  paths[channel][0] = 0;
  if (!sound)
    return;
  uint32_t row = row_for(4, sound);
  if (row == UINT32_MAX)
    return;
  uint32_t total = 0, weights[10] = {0};
  for (uint32_t i = 0; i < 10; i++) {
    const char *file = string(4, row, 3 + i);
    if (file && *file) {
      weights[i] = value(4, row, 13 + i);
      if (!weights[i])
        weights[i] = 1;
      if (weights[i] > 1000000)
        return;
      total += weights[i];
    }
  }
  if (!total)
    return;
  uint32_t selected = random_next() % total, index = 0;
  for (; index < 10; index++) {
    if (selected < weights[index])
      break;
    selected -= weights[index];
  }
  if (index >= 10)
    return;
  const char *directory = string(4, row, 23), *file = string(4, row, 3 + index);
  float volume = scalar(4, row, 24);
  if (!directory || !file || !isfinite(volume) || volume < 0 || volume > 1)
    return;
  size_t d = strlen(directory);
  if (d + strlen(file) + 2 > 512)
    return;
  memcpy(paths[channel], directory, d);
  if (d && directory[d - 1] != '\\' && directory[d - 1] != '/')
    paths[channel][d++] = '\\';
  memcpy(paths[channel] + d, file, strlen(file) + 1);
  out[1] = volume;
}
static uint32_t area_field(uint32_t area, uint32_t column) {
  for (uint32_t depth = 0; area && depth < 16; depth++) {
    uint32_t row = row_for(0, area);
    if (row == UINT32_MAX)
      return 0;
    uint32_t v = value(0, row, column);
    if (v)
      return v;
    area = value(0, row, 2);
  }
  return 0;
}
void bw_world_audio_sample(uint32_t area, float minutes) {
  if (!tables[4].bytes || !tables[0].bytes || !tables[1].bytes || !tables[2].bytes ||
      !tables[3].bytes || !isfinite(minutes))
    return;
  float time = fmodf(fmaxf(0, minutes), 1440);
  uint32_t night = time < 360 || time >= 1080;
  if (area == last_area && night == last_night)
    return;
  uint32_t old_area = last_area;
  last_area = area;
  last_night = night;
  uint32_t music = row_for(1, area_field(area, 8)), ambient = row_for(2, area_field(area, 7)),
           intro = row_for(3, area_field(area, 9));
  uint32_t ids[3] = {music == UINT32_MAX ? 0 : value(1, music, 6 + night),
                     ambient == UINT32_MAX ? 0 : value(2, ambient, 1 + night),
                     intro == UINT32_MAX ? 0 : value(3, intro, 2)};
  for (uint32_t i = 0; i < 3; i++)
    if ((uint32_t)info[i * 6] != ids[i] || (i == 2 && area != old_area && ids[i])) {
      uint32_t minimum = i == 0 && music != UINT32_MAX ? value(1, music, 2 + night) : 0,
               maximum = i == 0 && music != UINT32_MAX ? value(1, music, 4 + night) : 0;
      if (minimum > 3600000)
        minimum = 3600000;
      if (maximum < minimum)
        maximum = minimum;
      if (maximum > 3600000)
        maximum = 3600000;
      choose(i, ids[i], minimum, maximum, 0);
    }
}
uint32_t bw_world_audio_next(uint32_t channel) {
  if (channel >= 3 || !tables[4].bytes)
    return 1;
  float *v = info + channel * 6;
  choose(channel, (uint32_t)v[0], (uint32_t)v[3], (uint32_t)v[4], channel == 0);
  return 0;
}
const float *bw_world_audio_info(void) {
  return info;
}
const char *bw_world_audio_paths(void) {
  return &paths[0][0];
}
