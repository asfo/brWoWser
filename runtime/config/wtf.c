#include "wtf.h"
#include <string.h>
static int space(uint8_t c) {
  return c == ' ' || c == '\t';
}
static int namechar(uint8_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}
uint32_t bw_wtf_parse(BwWtfConfig *out, const uint8_t *b, size_t n) {
  if (!out)
    return 1;
  memset(out, 0, sizeof(*out));
  if ((!b && n) || n > 1024 * 1024) {
    out->error_line = 1;
    return 1;
  }
  size_t p = 0;
  uint32_t line = 1;
  if (n >= 3 && b[0] == 0xef && b[1] == 0xbb && b[2] == 0xbf)
    p = 3;
  while (p < n) {
    size_t end = p;
    while (end < n && b[end] != '\n')
      end++;
    size_t stop = end;
    if (stop > p && b[stop - 1] == '\r')
      stop--;
    size_t q = p;
    while (q < stop && space(b[q]))
      q++;
    if (q < stop) {
      BwWtfEntry e;
      memset(&e, 0, sizeof(e));
      e.line = line;
      if (stop - q < 4 || memcmp(b + q, "SET", 3) || !space(b[q + 3]))
        goto bad;
      q += 4;
      while (q < stop && space(b[q]))
        q++;
      size_t start = q;
      while (q < stop && namechar(b[q]))
        q++;
      size_t size = q - start;
      if (!size || size >= sizeof(e.name) || q == stop || !space(b[q]))
        goto bad;
      memcpy(e.name, b + start, size);
      while (q < stop && space(b[q]))
        q++;
      if (q == stop || b[q++] != '"')
        goto bad;
      start = q;
      while (q < stop && b[q] != '"') {
        if (b[q] < 32 || b[q] == 127 || b[q] == '\\')
          goto bad;
        q++;
      }
      size = q - start;
      if (q == stop || size >= sizeof(e.value))
        goto bad;
      memcpy(e.value, b + start, size);
      q++;
      while (q < stop && space(b[q]))
        q++;
      if (q != stop || out->count == BW_WTF_ENTRIES)
        goto bad;
      out->entries[out->count++] = e;
    }
    p = end < n ? end + 1 : end;
    line++;
    continue;
  bad:
    out->count = 0;
    out->error_line = line;
    return 1;
  }
  return 0;
}
