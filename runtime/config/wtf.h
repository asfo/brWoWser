#ifndef BRWOWSER_CONFIG_WTF_H
#define BRWOWSER_CONFIG_WTF_H
#include <stddef.h>
#include <stdint.h>
#define BW_WTF_ENTRIES 256
typedef struct {
  char name[64], value[256];
  uint32_t line;
} BwWtfEntry;
typedef struct {
  BwWtfEntry entries[BW_WTF_ENTRIES];
  uint32_t count, error_line;
} BwWtfConfig;
/* Selected portable interchange grammar: blank lines, SET identifier "literal".
 * Horizontal ASCII whitespace, CRLF/LF, optional UTF-8 BOM. Quotes/backslashes
 * inside values are rejected rather than guessing native escape semantics.
 * Unknown names and duplicate assignments remain in source order. No I/O. */
uint32_t bw_wtf_parse(BwWtfConfig *out, const uint8_t *bytes, size_t length);
#endif
