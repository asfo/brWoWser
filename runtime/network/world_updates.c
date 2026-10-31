#include <stdint.h>
#include <string.h>
#define CAP 1048576
uint8_t *bw_update_input(void);
uint8_t *bw_update_output(void);
uint32_t bw_update_inflate(uint32_t, uint32_t, uint32_t);
static uint32_t own[2], fields[160], present[160], staged[160], known[160], revision;
static const uint8_t *bytes;
static uint32_t length, cursor, error;
static uint32_t take(uint32_t n) {
  uint32_t p = cursor;
  if (n > length - cursor) {
    error = 1;
    return cursor;
  }
  cursor += n;
  return p;
}
static uint32_t word(uint32_t p) {
  return bytes[p] | (uint32_t)bytes[p + 1] << 8 | (uint32_t)bytes[p + 2] << 16 |
         (uint32_t)bytes[p + 3] << 24;
}
static uint32_t number(uint32_t n) {
  uint32_t p = take(n), v = 0;
  if (error)
    return 0;
  for (uint32_t i = 0; i < n; i++)
    v |= (uint32_t)bytes[p + i] << (8 * i);
  return v;
}
static void guid(uint32_t *out) {
  uint32_t mask = number(1);
  out[0] = out[1] = 0;
  for (uint32_t i = 0; i < 8; i++)
    if (mask & (1 << i))
      out[i / 4] |= number(1) << (8 * (i % 4));
}
static void movement(void) {
  uint32_t flags = number(2);
  if (flags & ~0x3ffu) {
    error = 1;
    return;
  }
  if (flags & 0x20) {
    uint32_t move = number(4), extra = number(2);
    take(20);
    if (move & 0x200) {
      uint32_t g[2];
      guid(g);
      take(21);
      if (extra & 0x400)
        take(4);
    }
    if ((move & 0x02200000) || (extra & 0x20))
      take(4);
    take(4);
    if (move & 0x1000)
      take(16);
    if (move & 0x4000000)
      take(4);
    take(36);
    if (move & 0x8000000) {
      uint32_t f = number(4);
      if (f & 0x20000)
        take(4);
      else if (f & 0x10000)
        take(8);
      else if (f & 0x8000)
        take(12);
      take(28);
      uint32_t n = number(4);
      if (n > 65536) {
        error = 1;
        return;
      }
      take(n * 12);
      take(13);
    }
  } else if (flags & 0x100) {
    uint32_t g[2];
    guid(g);
    take(32);
  } else if (flags & 0x40)
    take(16);
  if (flags & 8)
    take(4);
  if (flags & 16)
    take(4);
  if (flags & 4) {
    uint32_t g[2];
    guid(g);
  }
  if (flags & 2)
    take(4);
  if (flags & 0x80)
    take(8);
  if (flags & 0x200)
    take(8);
}
static void values(int self) {
  uint32_t blocks = number(1), at = take(blocks * 4);
  if (error)
    return;
  for (uint32_t i = 0; i < blocks * 32 && !error; i++)
    if (word(at + (i / 32) * 4) & (1u << (i % 32))) {
      uint32_t value = number(4);
      if (self && i < 160) {
        staged[i] = value;
        known[i] = 1;
      }
    }
}
void bw_world_unit_begin(uint32_t low, uint32_t high) {
  own[0] = low;
  own[1] = high;
  memset(fields, 0, sizeof(fields));
  memset(present, 0, sizeof(present));
  revision = 0;
}
uint32_t bw_world_unit_values(void) {
  return (uint32_t)(uintptr_t)fields;
}
uint32_t bw_world_unit_known(void) {
  return (uint32_t)(uintptr_t)present;
}
uint32_t bw_world_unit_revision(void) {
  return revision;
}
uint32_t bw_world_unit_update(uint32_t n, uint32_t compressed) {
  if (n > CAP || n < 4)
    return 1;
  bytes = bw_update_input();
  length = n;
  cursor = error = 0;
  if (compressed) {
    uint32_t size = word(0);
    if (size > CAP)
      return 2;
    memmove(bw_update_input(), bytes + 4, n - 4);
    if (bw_update_inflate(n - 4, CAP, size))
      return 2;
    bytes = bw_update_output();
    length = size;
  }
  memcpy(staged, fields, sizeof(fields));
  memcpy(known, present, sizeof(present));
  uint32_t count = number(4);
  if (count > 65536)
    return 3;
  for (uint32_t i = 0; i < count && !error; i++) {
    uint32_t type = number(1), g[2];
    if (type == 4 || type == 5) {
      uint32_t count = number(4);
      if (count > 65536)
        return 3;
      for (uint32_t k = 0; k < count && !error; k++)
        guid(g);
      continue;
    }
    if (type > 3)
      return 3;
    guid(g);
    int self = g[0] == own[0] && g[1] == own[1];
    if (type == 2 || type == 3) {
      number(1);
      if (self) {
        memset(staged, 0, sizeof(staged));
        for (uint32_t k = 0; k < 160; k++)
          known[k] = 1;
      }
      movement();
      values(self);
    } else if (type == 1)
      movement();
    else
      values(self);
  }
  if (error || cursor != length)
    return 4;
  if (memcmp(fields, staged, sizeof(fields)) || memcmp(present, known, sizeof(present))) {
    memcpy(fields, staged, sizeof(fields));
    memcpy(present, known, sizeof(present));
    revision++;
  }
  return 0;
}
