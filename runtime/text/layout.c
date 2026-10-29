#include "layout.h"
#include <math.h>
static int hex(uint8_t c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static uint32_t token(const uint8_t *s, uint32_t n, uint32_t *pos, uint32_t *cp) {
  while (*pos < n) {
    uint32_t i = *pos, c = s[i];
    if (!c)
      return 2;
    if (c == '|') {
      if (i + 1 >= n)
        return 2;
      uint8_t k = s[i + 1];
      if (k == 'n') {
        *pos = i + 2;
        *cp = 10;
        return 0;
      }
      if (k == 'r') {
        *pos = i + 2;
        continue;
      }
      if (k == 'c') {
        if (n - i < 10)
          return 2;
        for (uint32_t j = i + 2; j < i + 10; j++)
          if (!hex(s[j]))
            return 2;
        *pos = i + 10;
        continue;
      }
      if (k == '|') {
        *pos = i + 2;
        *cp = '|';
        return 0;
      }
      return 2;
    }
    if (c < 128) {
      *pos = i + 1;
      *cp = c;
      return 0;
    }
    uint32_t count, min;
    if (c >= 0xc2 && c <= 0xdf) {
      count = 2;
      min = 128;
      c &= 31;
    } else if (c >= 0xe0 && c <= 0xef) {
      count = 3;
      min = 2048;
      c &= 15;
    } else
      return 2;
    if (n - i < count)
      return 2;
    for (uint32_t j = 1; j < count; j++) {
      uint8_t x = s[i + j];
      if ((x & 0xc0) != 0x80)
        return 2;
      c = (c << 6) | (x & 63);
    }
    if (c < min || (c >= 0xd800 && c <= 0xdfff))
      return 2;
    *pos = i + count;
    *cp = c;
    return 0;
  }
  return 1;
}
static int space(uint32_t c) {
  return c == 32 || (c >= 9 && c <= 13);
}
/* Native 0x6c02f0 ASCII branch, extracted branch classes, not a generic
 * word-wrapping heuristic. Selected curly quotes/apostrophe are admitted for
 * current enUS legal/credits text; unsupported script-specific breaking remains excluded.
 * U+2019 uses the ordinary in-word punctuation compatibility policy; native
 * line-break equivalence for that addition is not established. */
static int boundary(uint32_t previous, uint32_t current) {
  if (!previous)
    return 0;
  if (previous == '-' || previous == ';' || previous == '/' || previous == 0x2013 ||
      previous == 0x2014 || current == '|')
    return 1;
  if (previous == 0x201c || current == 0x201d)
    return 0;
  if (space(previous))
    return 0;
  if (space(current))
    return 1;
  if (previous == '$' || previous == '(' || previous == '[' || previous == '\\' || previous == '{')
    return 0;
  if (current == '!' || current == '%' || current == ')' || current == ',' || current == '.' ||
      current == ':' || current == ';' || current == '?' || current == ']' || current == '}')
    return 0;
  return 0;
}
static uint32_t glyph(const BwTextProvider *p, uint32_t cp, BwTextGlyph *g) {
  uint32_t s = p->glyph(p->context, cp, g);
  if (s == 2)
    return 2;
  if (s || g->width > 512 || !isfinite(g->advance) || !isfinite(g->bearing) ||
      fabsf(g->advance) > 1024 || fabsf(g->bearing) > 1024)
    return 3;
  return 0;
}
static uint32_t pair(const BwTextProvider *p, uint32_t a, uint32_t b, float *v) {
  if (p->pair(p->context, a, b, v) || !isfinite(*v) || fabsf(*v) > 2048)
    return 3;
  return 0;
}
static uint32_t fit(const uint8_t *s, uint32_t n, uint32_t start, const BwTextProvider *p,
                    float leading, float width, BwTextLine *out) {
  uint32_t at = start, previous = 0, break_at = start;
  float advance = leading, tail = 0, break_width = 0;
  int overflow = 0, newline = 0;
  while (at < n) {
    uint32_t after = at, cp;
    uint32_t st = token(s, n, &after, &cp);
    if (st == 2)
      return 2;
    if (st == 1) {
      at = after;
      break;
    }
    if (cp == 10) {
      at = after;
      newline = 1;
      break;
    }
    /* Explicit punctuation found in the owner's credits documents. Keep
     * script-specific line breaking gated rather than accepting arbitrary CJK. */
    if (cp >= 0x300 && cp != 0x2013 && cp != 0x2014 && cp != 0x2018 && cp != 0x2019 &&
        cp != 0x201c && cp != 0x201d && cp != 0x2026)
      return 2;
    BwTextGlyph g;
    st = glyph(p, cp, &g);
    if (st == 2) {
      at = after;
      continue;
    }
    if (st)
      return st;
    float step = 0;
    if (previous && pair(p, previous, cp, &step))
      return 3;
    if (boundary(previous, cp)) {
      break_at = at;
      break_width = advance + tail;
    }
    uint32_t look = after, next_cp;
    uint32_t more = token(s, n, &look, &next_cp);
    if (more == 2)
      return 2;
    float next_tail = 0;
    if (more == 0 && next_cp != 10) {
      if (pair(p, cp, next_cp, &next_tail))
        return 3;
    }
    if (more == 1 || next_cp == 10 || next_tail <= 0)
      next_tail = ceilf(g.bearing) + g.width;
    if (advance + step + next_tail > width) {
      overflow = 1;
      break;
    }
    advance += step;
    tail = next_tail;
    previous = cp;
    at = after;
  }
  if (overflow && break_at > start) {
    at = break_at;
    advance = break_width;
    tail = 0;
  }
  if (at == start && overflow)
    return 5;
  out->start = start;
  out->length = at - start;
  out->width = advance + tail;
  out->next = at;
  if (overflow && !newline)
    while (out->next < n) {
      uint32_t after = out->next, cp, st = token(s, n, &after, &cp);
      if (st == 2)
        return 2;
      if (st || !space(cp))
        break;
      out->next = after;
    }
  return 0;
}
uint32_t bw_text_layout(const uint8_t *s, uint32_t n, const BwTextProvider *p, float leading,
                        float scale, float height, float width, BwTextLine *lines,
                        uint32_t capacity, BwTextLayout *out) {
  if (!s || !p || !p->glyph || !p->pair || !out || (!lines && capacity) || n > 1048576 ||
      !isfinite(leading) || leading < 0 || leading > 256 || !isfinite(scale) || scale <= 0 ||
      scale > 128 || !isfinite(height) || height <= 0 || height > 512 || !isfinite(width) ||
      width < 0)
    return 1;
  *out = (BwTextLayout){0, 0, 0};
  uint32_t st = bw_text_extent(s, n, p, leading, scale, &out->intrinsic_width);
  if (st)
    return st;
  if (!n)
    return 0;
  float bound = width > 0 ? width / scale : INFINITY;
  uint32_t at = 0;
  while (at < n) {
    if (out->lines >= capacity)
      return 4;
    BwTextLine line;
    st = fit(s, n, at, p, leading, bound, &line);
    if (st)
      return st;
    if (line.next <= at)
      return 5;
    line.width *= scale;
    lines[out->lines++] = line;
    at = line.next;
  }
  if (n && s[n - 1] == 10) {
    if (out->lines >= capacity)
      return 4;
    lines[out->lines++] = (BwTextLine){n, 0, n, 0};
  }
  out->height = out->lines * height;
  return isfinite(out->height) ? 0 : 1;
}
