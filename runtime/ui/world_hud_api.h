#ifndef BRWOWSER_WORLD_HUD_API_H
#define BRWOWSER_WORLD_HUD_API_H
#include "lua.h"
#include <stdint.h>
void bw_world_hud_register(lua_State *);
uint32_t bw_world_hud_state(const uint8_t *, uint32_t);
uint32_t bw_world_hud_record(const uint8_t *, uint32_t);
uint32_t bw_world_hud_seen(uint32_t);
uint32_t bw_world_hud_zoom(void);
void bw_world_hud_time(uint32_t);

#endif
