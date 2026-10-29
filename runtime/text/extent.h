#ifndef BRWOWSER_TEXT_EXTENT_H
#define BRWOWSER_TEXT_EXTENT_H
#include <stdint.h>
typedef struct BwTextGlyph {
  uint32_t width;
  float advance, bearing;
} BwTextGlyph;
/* Glyph callback returns 0 for a record, 2 for absent, other values for failure.
 * Pair callback supplies the already rounded native-style pair advance. */
typedef struct BwTextProvider {
  void *context;
  uint32_t (*glyph)(void *, uint32_t, BwTextGlyph *);
  uint32_t (*pair)(void *, uint32_t, uint32_t, float *);
} BwTextProvider;
/* Selected text branch: UTF-8/BMP, escaped bars, color/reset markup and LF.
 * Other markup is rejected, never silently stripped as if implemented.
 * outline and scale are explicit backend parameters, not inferred from examples. */
uint32_t bw_text_extent(const uint8_t *text, uint32_t length, const BwTextProvider *provider,
                        float outline, float scale, float *out);
#endif
