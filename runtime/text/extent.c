#include "extent.h"
#include <math.h>
static int hex(uint8_t c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
/* 0=character, 1=end, 2=unsupported/malformed. */
static uint32_t next(const uint8_t *s, uint32_t n, uint32_t *position, uint32_t *cp) {
  while (*position < n) {
    uint32_t i = *position, c = s[i];
    if (!c)
      return 2;
    if (c == '|') {
      if (i + 1 >= n)
        return 2;
      uint8_t code = s[i + 1];
      if (code == 'n') {
        *position = i + 2;
        *cp = 10;
        return 0;
      }
      if (code == 'r') {
        *position = i + 2;
        continue;
      }
      if (code == 'c') {
        if (n - i < 10)
          return 2;
        for (uint32_t k = i + 2; k < i + 10; k++)
          if (!hex(s[k]))
            return 2;
        *position = i + 10;
        continue;
      }
      if (code == '|') {
        *position = i + 2;
        *cp = '|';
        return 0;
      }
      return 2;
    }
    if (c < 128) {
      *position = i + 1;
      *cp = c;
      return 0;
    }
    uint32_t count, min;
    if (c >= 0xc2 && c <= 0xdf) {
      count = 2;
      min = 0x80;
      c &= 31;
    } else if (c >= 0xe0 && c <= 0xef) {
      count = 3;
      min = 0x800;
      c &= 15;
    } else
      return 2;
    if (n - i < count)
      return 2;
    for (uint32_t k = 1; k < count; k++) {
      uint8_t v = s[i + k];
      if ((v & 0xc0) != 0x80)
        return 2;
      c = (c << 6) | (v & 63);
    }
    if (c < min || (c >= 0xd800 && c <= 0xdfff))
      return 2;
    *position = i + count;
    *cp = c;
    return 0;
  }
  return 1;
}
uint32_t bw_text_extent(const uint8_t *s, uint32_t n, const BwTextProvider *p, float outline,
                        float scale, float *out) {
  if (!s || !p || !p->glyph || !p->pair || !out || n > 1048576 || !isfinite(outline) ||
      outline < 0 || outline > 16 || !isfinite(scale) || scale <= 0 || scale > 128)
    return 1;
  *out = 0;
  if (!n)
    return 0;
  float line = outline, tail = 0, maximum = 0;
  uint32_t at = 0, previous = 0, cp, status;
  while ((status = next(s, n, &at, &cp)) == 0) {
    if (cp == 10) {
      float width = line + tail;
      if (width > maximum)
        maximum = width;
      line = 0;
      tail = 0;
      previous = 0;
      continue;
    }
    BwTextGlyph g;
    uint32_t found = p->glyph(p->context, cp, &g);
    if (found == 2)
      continue;
    if (found || g.width > 512 || !isfinite(g.advance) || !isfinite(g.bearing) ||
        fabsf(g.advance) > 1024 || fabsf(g.bearing) > 1024)
      return 3;
    if (previous) {
      float advance;
      if (p->pair(p->context, previous, cp, &advance) || !isfinite(advance) ||
          fabsf(advance) > 2048)
        return 3;
      line += advance;
    }
    uint32_t look = at, following;
    uint32_t more = next(s, n, &look, &following);
    if (more == 2)
      return 2;
    tail = 0;
    if (more == 0 && following != 10) {
      float advance;
      if (p->pair(p->context, cp, following, &advance))
        return 3;
      if (!isfinite(advance))
        return 3;
      if (advance <= 0)
        tail = ceilf(g.bearing) + g.width;
    } else
      tail = ceilf(g.bearing) + g.width;
    previous = cp;
  }
  if (status == 2)
    return 2;
  float width = line + tail;
  if (width > maximum)
    maximum = width;
  *out = maximum * scale;
  if (!isfinite(*out) || *out < 0)
    return 3;
  return 0;
}
