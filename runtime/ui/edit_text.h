#ifndef BW_EDIT_TEXT_H
#define BW_EDIT_TEXT_H
#include <stdint.h>
#define BW_EDIT_CAPACITY 4096
/* Selected bounded byte-indexed edit buffer. Browser transport initially ASCII. */
typedef struct BwEditText {
  char bytes[BW_EDIT_CAPACITY + 1];
  uint32_t length, cursor, start, end, limit;
} BwEditText;
uint32_t bw_edit_set(BwEditText *, const char *, uint32_t);
uint32_t bw_edit_select(BwEditText *, int32_t, int32_t);
uint32_t bw_edit_cursor(BwEditText *, int32_t);
uint32_t bw_edit_insert(BwEditText *, const char *, uint32_t);
uint32_t bw_edit_delete(BwEditText *, uint32_t);
#endif
