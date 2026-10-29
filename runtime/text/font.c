/* Candidate portable rasterizer; client-specific sizing/packing is separate. */
#include "font.h"
#include "extent.h"
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>
typedef struct MetricCache {
  uint32_t codepoint, valid;
  BwFontGlyph glyph;
} MetricCache;
/* Immutable face/pixel size: metadata reuse cannot change rasterization or
 * kerning. Bounded direct-mapped collisions recompute rather than grow. */
struct BwFont {
  FT_Library library;
  FT_Face face;
  uint8_t *bytes;
  uint32_t pixels;
  MetricCache metrics[256];
};
void bw_font_close(BwFont *f) {
  if (!f)
    return;
  if (f->face)
    FT_Done_Face(f->face);
  if (f->library)
    FT_Done_FreeType(f->library);
  free(f->bytes);
  free(f);
}
BwFont *bw_font_open(const uint8_t *bytes, uint32_t length, uint32_t pixels) {
  if (!bytes || length < 12 || length > 1048576 || pixels < 1 || pixels > 128)
    return NULL;
  BwFont *f = calloc(1, sizeof(*f));
  if (!f)
    return NULL;
  f->bytes = malloc(length);
  if (!f->bytes)
    goto bad;
  memcpy(f->bytes, bytes, length);
  if (FT_Init_FreeType(&f->library) ||
      FT_New_Memory_Face(f->library, f->bytes, length, 0, &f->face))
    goto bad;
  if (!FT_IS_SFNT(f->face) || f->face->num_glyphs <= 0 || f->face->num_glyphs > 65535 ||
      !f->face->units_per_EM)
    goto bad;
  if (FT_Select_Charmap(f->face, FT_ENCODING_UNICODE) || FT_Set_Pixel_Sizes(f->face, pixels, 0))
    goto bad;
  f->pixels = pixels;
  return f;
bad:
  bw_font_close(f);
  return NULL;
}
uint32_t bw_font_glyph(BwFont *f, uint32_t cp, BwFontGlyph *out, uint8_t *coverage,
                       uint32_t capacity) {
  if (!f || !out || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
    return 1;
  MetricCache *cached = &f->metrics[cp % 256];
  if (!coverage && cached->valid && cached->codepoint == cp) {
    *out = cached->glyph;
    return 0;
  }
  memset(out, 0, sizeof(*out));
  FT_UInt index = FT_Get_Char_Index(f->face, cp);
  if (!index)
    return 2;
  /* Observed selected native load flags at 0x6c8c2a, render mode at 0x6c8c49.
  * Public backend version differs; exact native output is a comparison gate. */
  if (FT_Load_Glyph(f->face, index, 0x208a) ||
      FT_Render_Glyph(f->face->glyph, FT_RENDER_MODE_NORMAL))
    return 3;
  FT_GlyphSlot g = f->face->glyph;
  FT_Bitmap *b = &g->bitmap;
  if (b->width > 512 || b->rows > 512 ||
      (b->width && b->rows && b->pixel_mode != FT_PIXEL_MODE_GRAY))
    return 4;
  if (coverage && b->width * b->rows > capacity)
    return 5;
  *out = (BwFontGlyph){
      index,          b->width,     b->rows, g->metrics.horiBearingX, g->metrics.horiAdvance,
      g->bitmap_left, g->bitmap_top};
  cached->valid = 1;
  cached->codepoint = cp;
  cached->glyph = *out;
  if (coverage)
    for (uint32_t y = 0; y < b->rows; y++) {
      const uint8_t *row = b->buffer + (b->pitch >= 0 ? y : b->rows - 1 - y) *
                                           (b->pitch >= 0 ? b->pitch : -b->pitch);
      memcpy(coverage + y * b->width, row, b->width);
    }
  return 0;
}
uint32_t bw_font_kerning(BwFont *f, uint32_t left, uint32_t right, int32_t *units) {
  if (!f || !units || left > 0x10ffff || right > 0x10ffff || (left >= 0xd800 && left <= 0xdfff) ||
      (right >= 0xd800 && right <= 0xdfff))
    return 1;
  FT_UInt a = FT_Get_Char_Index(f->face, left), b = FT_Get_Char_Index(f->face, right);
  if (!a || !b)
    return 2;
  FT_Vector value;
  if (FT_Get_Kerning(f->face, a, b, FT_KERNING_UNSCALED, &value))
    return 3;
  *units = value.x;
  return 0;
}
uint32_t bw_font_units(const BwFont *f) {
  return f ? f->face->units_per_EM : 0;
}

uint32_t bw_font_client_glyph(BwFont *f, uint32_t cp, uint32_t outline, uint32_t *width,
                              float *advance, float *bearing) {
  if (!f || !width || !advance || !bearing || (outline != 0 && outline != 2 && outline != 4))
    return 1;
  BwFontGlyph g;
  uint32_t status = bw_font_glyph(f, cp, &g, NULL, 0);
  if (status)
    return status;
  /* Native 0x6c8d6a blank-glyph branch, 0x6c8ddc outline storage,
  * 0x6c8dfc truncation/+1 and 0x6c8e1d bearing conversion. */
  uint32_t raw_width = g.width;
  if (!g.width || !g.rows) {
    raw_width = (f->pixels + 3) / 4;
    if (!raw_width)
      raw_width = f->pixels;
  }
  *width = raw_width + outline;
  *advance = (float)(g.advance_26_6 / 64 + 1);
  *bearing = g.bearing_x_26_6 / 64.0f;
  return 0;
}
typedef struct ClientContext {
  BwFont *font;
  uint32_t outline;
} ClientContext;
static uint32_t client_glyph(void *ctx, uint32_t cp, BwTextGlyph *out) {
  ClientContext *c = ctx;
  return bw_font_client_glyph(c->font, cp, c->outline, &out->width, &out->advance, &out->bearing);
}
static uint32_t client_pair(void *ctx, uint32_t left, uint32_t right, float *out) {
  ClientContext *c = ctx;
  BwTextGlyph g;
  uint32_t status = client_glyph(ctx, left, &g);
  if (status)
    return status;
  int32_t units;
  status = bw_font_kerning(c->font, left, right, &units);
  if (status)
    return status;
  if (units > 0)
    units = 0;
  float factor = (float)c->font->pixels / bw_font_units(c->font);
  float sum = g.advance + units * factor;
  *out = ceilf(sum);
  return 0;
}
uint32_t bw_font_text_extent(BwFont *f, const uint8_t *s, uint32_t n, uint32_t outline,
                             float shadow_x, float scale, float *out) {
  if (!f || (outline != 0 && outline != 2 && outline != 4) || !isfinite(shadow_x) ||
      fabsf(shadow_x) > 128)
    return 1;
  ClientContext ctx = {f, outline};
  BwTextProvider provider = {&ctx, client_glyph, client_pair};
  float leading = outline + (shadow_x > 0 ? ceilf(shadow_x) : 0);
  return bw_text_extent(s, n, &provider, leading, scale, out);
}

float bw_font_ascent(const BwFont *f) {
  return f ? (float)f->face->size->metrics.ascender / 64.0f : 0;
}
float bw_font_descent(const BwFont *f) {
  return f ? (float)f->face->size->metrics.descender / 64.0f : 0;
}

uint32_t bw_font_text_layout(BwFont *f, const uint8_t *s, uint32_t n, uint32_t outline,
                             float shadow_x, float scale, float height, float width,
                             BwTextLine *lines, uint32_t capacity, BwTextLayout *out) {
  if (!f || (outline != 0 && outline != 2 && outline != 4) || !isfinite(shadow_x) ||
      fabsf(shadow_x) > 128)
    return 1;
  ClientContext ctx = {f, outline};
  BwTextProvider provider = {&ctx, client_glyph, client_pair};
  float leading = outline + (shadow_x > 0 ? ceilf(shadow_x) : 0);
  return bw_text_layout(s, n, &provider, leading, scale, height, width, lines, capacity, out);
}
