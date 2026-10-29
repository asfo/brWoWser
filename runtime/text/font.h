#ifndef BRWOWSER_TEXT_FONT_H
#define BRWOWSER_TEXT_FONT_H
#include <stdint.h>
#include "layout.h"
typedef struct BwFont BwFont;
/* Public FreeType backend. No claim that this is the native library version. */
typedef struct BwFontGlyph {
  uint32_t index, width, rows;
  int32_t bearing_x_26_6, advance_26_6, left, top;
} BwFontGlyph;
BwFont *bw_font_open(const uint8_t *bytes, uint32_t length, uint32_t pixels);
void bw_font_close(BwFont *font);
float bw_font_ascent(const BwFont *font);
float bw_font_descent(const BwFont *font);
uint32_t bw_font_glyph(BwFont *font, uint32_t codepoint, BwFontGlyph *out, uint8_t *coverage,
                       uint32_t capacity);
uint32_t bw_font_kerning(BwFont *font, uint32_t left, uint32_t right, int32_t *units);
uint32_t bw_font_units(const BwFont *font);
/* Selected native-style glyph packing and text extent. Explicit outline padding
 * and shadow offset are supplied by the owning font style. scale remains local.
 * Color/reset markup, escaped bars and LF only; not a full WoW text shaper. */
uint32_t bw_font_text_extent(BwFont *font, const uint8_t *text, uint32_t length, uint32_t outline,
                             float shadow_x, float scale, float *out);
uint32_t bw_font_client_glyph(BwFont *font, uint32_t cp, uint32_t outline, uint32_t *width,
                              float *advance, float *bearing);
uint32_t bw_font_text_layout(BwFont *font, const uint8_t *text, uint32_t length, uint32_t outline,
                             float shadow_x, float scale, float line_height, float width,
                             BwTextLine *lines, uint32_t capacity, BwTextLayout *out);
#endif
