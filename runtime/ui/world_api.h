#ifndef BRWOWSER_WORLD_UI_API_H
#define BRWOWSER_WORLD_UI_API_H
#include "lua.h"
#include <stdint.h>
/* A separate world-UI VM owns this snapshot. Unknown unit fields stay unknown. */
uint32_t bw_world_ui_register(lua_State *L);
uint32_t bw_world_ui_player(uint32_t low, uint32_t high, uint32_t race, uint32_t class_id,
                            uint32_t gender, uint32_t level);
uint32_t bw_world_ui_text(uint32_t field, const uint8_t *bytes, uint32_t length);
uint32_t bw_world_ui_clock(double seconds);
#endif
