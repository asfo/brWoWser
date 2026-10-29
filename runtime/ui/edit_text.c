#include "edit_text.h"
#include <string.h>
static uint32_t valid(const char *s, uint32_t n) {
  if (!s || n > BW_EDIT_CAPACITY)
    return 0;
  for (uint32_t i = 0; i < n; i++)
    if ((unsigned char)s[i] < 32)
      return 0;
  return 1;
}
uint32_t bw_edit_set(BwEditText *s, const char *text, uint32_t n) {
  if (!s || !valid(text, n))
    return 1;
  if (s->limit && n > s->limit)
    n = s->limit;
  if (n > BW_EDIT_CAPACITY)
    return 1;
  memmove(s->bytes, text, n);
  s->bytes[n] = 0;
  s->length = s->cursor = n;
  s->start = s->end = n;
  return 0;
}
uint32_t bw_edit_select(BwEditText *s, int32_t start, int32_t end) {
  if (!s)
    return 1;
  uint32_t a = start <= 0 ? 0 : (uint32_t)start;
  if (a > s->length)
    a = s->length;
  int32_t b = end <= -1 ? -1 : end;
  if (b >= (int32_t)s->length || b < (int32_t)a)
    b = (int32_t)s->length;
  s->start = a;
  s->end = (uint32_t)b;
  return 0;
}
uint32_t bw_edit_cursor(BwEditText *s, int32_t cursor) {
  if (!s)
    return 1;
  s->cursor = cursor < 0 ? 0 : (uint32_t)cursor;
  if (s->cursor > s->length)
    s->cursor = s->length;
  s->start = s->end = s->cursor;
  return 0;
}
uint32_t bw_edit_insert(BwEditText *s, const char *text, uint32_t n) {
  if (!s || !valid(text, n))
    return 1;
  uint32_t a = s->start, b = s->end;
  if (a == b)
    a = b = s->cursor;
  if (a > b || b > s->length)
    return 1;
  uint32_t retained = s->length - (b - a), maximum = s->limit ? s->limit : BW_EDIT_CAPACITY;
  if (maximum > BW_EDIT_CAPACITY)
    maximum = BW_EDIT_CAPACITY;
  if (retained > maximum)
    return 1;
  if (n > maximum - retained)
    n = maximum - retained;
  memmove(s->bytes + a + n, s->bytes + b, s->length - b + 1);
  memcpy(s->bytes + a, text, n);
  s->length = retained + n;
  s->cursor = a + n;
  s->start = s->end = s->cursor;
  return 0;
}
uint32_t bw_edit_delete(BwEditText *s, uint32_t forward) {
  if (!s || forward > 1)
    return 1;
  if (s->start == s->end) {
    if (forward) {
      if (s->cursor >= s->length)
        return 0;
      s->start = s->cursor;
      s->end = s->cursor + 1;
    } else {
      if (!s->cursor)
        return 0;
      s->start = s->cursor - 1;
      s->end = s->cursor;
    }
  }
  return bw_edit_insert(s, "", 0);
}
