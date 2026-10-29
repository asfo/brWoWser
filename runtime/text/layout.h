#ifndef BRWOWSER_TEXT_LAYOUT_H
#define BRWOWSER_TEXT_LAYOUT_H
#include "extent.h"
/* Selected native normal fitter (0x6c1d40), default ASCII/Latin letter profile.
 * Byte spans refer to original UTF-8, not copied/rewritten strings. next skips
 * continuation whitespace; length includes a consumed explicit LF if present.
 * width is native fitter extent, not an independently remeasured substring. */
typedef struct BwTextLine {
  uint32_t start, length, next;
  float width;
} BwTextLine;
typedef struct BwTextLayout {
  uint32_t lines;
  float height, intrinsic_width;
} BwTextLayout;
/* Logical dimensions already use the explicitly selected raster scale.
 * Zero width means unconstrained. Positive height clipping/ellipsis, special
 * indentation, nonspace-wrap overrides and inline images are not implemented.
 * 0 success, 1 invalid input, 2 unsupported text, 3 provider, 4 capacity,
 * 5 a glyph cannot fit (native line fitter makes no progress). */
uint32_t bw_text_layout(const uint8_t *text, uint32_t length, const BwTextProvider *provider,
                        float leading, float scale, float line_height, float width,
                        BwTextLine *lines, uint32_t capacity, BwTextLayout *out);
#endif
