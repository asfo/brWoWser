#include <stdint.h>
#include <string.h>
extern uint8_t *bw_update_input(void);
/* Stable little-endian snapshot: revision, ready mask, action slots, learned
 * spells and account tutorial flags. No fabricated starting loadout. */
static uint32_t data[2 + 144 + 1 + 4096 + 8];
static uint32_t u16(const uint8_t *b, uint32_t at) {
  return b[at] | (uint32_t)b[at + 1] << 8;
}
static uint32_t u32(const uint8_t *b, uint32_t at) {
  return u16(b, at) | u16(b, at + 2) << 16;
}
void bw_world_hud_begin(void) {
  memset(data, 0, sizeof(data));
}
uint32_t bw_world_hud_ptr(void) {
  return (uint32_t)(uintptr_t)data;
}
uint32_t bw_world_hud_packet(uint32_t opcode, uint32_t n) {
  const uint8_t *b = bw_update_input();
  if (opcode == 0x129) {
    if (n != 577 && !(n == 1 && b[0] == 2))
      return 1;
    if (b[0] > 2)
      return 1;
    for (uint32_t i = 0; i < 144; i++)
      data[2 + i] = b[0] == 2 ? 0 : u32(b, 1 + i * 4);
    data[1] |= 1;
  } else if (opcode == 0x12a) {
    if (n < 5)
      return 1;
    uint32_t count = u16(b, 1), at = 3 + count * 6;
    if (count > 4096 || at + 2 > n)
      return 1;
    uint32_t cooldowns = u16(b, at);
    if (at + 2 + cooldowns * 16 != n)
      return 1;
    data[146] = count;
    for (uint32_t i = 0; i < count; i++)
      data[147 + i] = u32(b, 3 + i * 6);
    data[1] |= 2;
  } else if (opcode == 0xfd) {
    if (n != 32)
      return 1;
    for (uint32_t i = 0; i < 8; i++)
      data[4243 + i] = u32(b, i * 4);
    data[1] |= 4;
  } else
    return 2;
  data[0]++;
  return 0;
}
