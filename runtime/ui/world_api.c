#include "world_api.h"
#include "lauxlib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static struct {
  uint32_t guid[2], race, class_id, gender, level;
  char text[7][256];
  double seconds;
  int supplied;
  char classes[12][3][128];
} player;
uint32_t bw_world_ui_player(uint32_t low, uint32_t high, uint32_t race, uint32_t class_id,
                            uint32_t gender, uint32_t level) {
  if ((!low && !high) || !race || race > 11 || !class_id || class_id > 11 || gender > 1 || !level ||
      level > 255)
    return 1;
  player.guid[0] = low;
  player.guid[1] = high;
  player.race = race;
  player.class_id = class_id;
  player.gender = gender;
  player.level = level;
  player.supplied = 1;
  return 0;
}
uint32_t bw_world_ui_text(uint32_t field, const uint8_t *bytes, uint32_t length) {
  if (field >= 16) {
    uint32_t id = (field - 16) / 3, kind = (field - 16) % 3;
    if (!id || id >= 12 || !bytes || !length || length >= 128 || memchr(bytes, 0, length))
      return 1;
    memcpy(player.classes[id][kind], bytes, length);
    player.classes[id][kind][length] = 0;
    return 0;
  }
  if (field >= 7 || !bytes || !length || length >= sizeof(player.text[0]) ||
      memchr(bytes, 0, length))
    return 1;
  memcpy(player.text[field], bytes, length);
  player.text[field][length] = 0;
  return 0;
}
uint32_t bw_world_ui_clock(double seconds) {
  if (!isfinite(seconds) || seconds < player.seconds)
    return 1;
  player.seconds = seconds;
  return 0;
}
static int call(lua_State *L) {
  int op = lua_tointeger(L, lua_upvalueindex(1));
  if (op == 0) {
    lua_pushnumber(L, player.seconds);
    return 1;
  }
  if (op == 1) {
    lua_pushstring(L, player.text[6]);
    return 1;
  }
  if (op == 2) {
    lua_pushnumber(L, 1);
    return 1;
  } /* Initial local action-bar page. */
  if (op == 3) {
    lua_pushstring(L, "3.3.5");
    lua_pushstring(L, "12340");
    lua_pushstring(L, "Jun 24 2010");
    lua_pushnumber(L, 30300);
    return 4;
  }
  const char *unit = luaL_optstring(L, 1, "");
  int exists = player.supplied && !strcmp(unit, "player");
  if (op == 4) {
    lua_pushboolean(L, exists);
    return 1;
  }
  if (!exists) {
    lua_pushnil(L);
    return 1;
  }
  if (op == 5) {
    lua_pushstring(L, player.text[0]);
    lua_pushnil(L);
    return 2;
  }
  if (op == 6) {
    lua_pushnumber(L, player.level);
    return 1;
  }
  if (op == 7) {
    lua_pushstring(L, player.text[1]);
    lua_pushstring(L, player.text[2]);
    return 2;
  }
  if (op == 8) {
    lua_pushstring(L, player.text[3]);
    lua_pushstring(L, player.text[4]);
    lua_pushnumber(L, player.class_id);
    return 3;
  }
  if (op == 9) {
    lua_pushnumber(L, player.gender + 2);
    return 1;
  }
  if (op == 10) {
    lua_pushstring(L, player.text[5]);
    lua_pushstring(L, player.text[5]);
    return 2;
  }
  if (op == 11) {
    char guid[19];
    snprintf(guid, sizeof(guid), "0x%08X%08X", player.guid[1], player.guid[0]);
    lua_pushstring(L, guid);
    return 1;
  }
  return luaL_error(L, "World UI API unavailable");
}
static int class_list(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int gender = lua_toboolean(L, 2) ? 1 : 0;
  for (uint32_t id = 1; id < 12; id++)
    if (player.classes[id][2][0]) {
      lua_pushstring(L, player.classes[id][gender]);
      lua_setfield(L, 1, player.classes[id][2]);
    }
  return 0;
}
static int bit_operation(lua_State *L) {
  int op = lua_tointeger(L, lua_upvalueindex(1));
  uint32_t value = op == 0 ? UINT32_MAX : 0;
  int count = lua_gettop(L);
  for (int i = 1; i <= count; i++) {
    double number = luaL_checknumber(L, i);
    if (!isfinite(number) || number < -2147483648. || number > 4294967295.)
      return luaL_error(L, "nonfinite bit operand");
    uint32_t n = (uint32_t)(int64_t)number;
    if (op == 0)
      value &= n;
    else if (op == 1)
      value |= n;
    else if (op == 2)
      value ^= n;
    else if (i == 1)
      value = n;
    else if (op == 4)
      value <<= n & 31;
    else if (op == 5)
      value >>= n & 31;
  }
  if (op == 3)
    value = ~value;
  lua_pushnumber(L, (int32_t)value);
  return 1;
}
static int vehicle_exit(lua_State *L) {
  return luaL_error(L, "Vehicle exit is not implemented");
}
static int quality_color(lua_State *L) {
  static const uint32_t colors[] = {0xff9d9d9d, 0xffffffff, 0xff1eff00, 0xff0070dd,
                                    0xffa335ee, 0xffff8000, 0xffe6cc80, 0xffe6cc80};
  int quality = luaL_checkinteger(L, 1);
  uint32_t color = quality >= 0 && quality < 8 ? colors[quality] : 0xffff0000;
  lua_pushnumber(L, ((color >> 16) & 255) / 255.);
  lua_pushnumber(L, ((color >> 8) & 255) / 255.);
  lua_pushnumber(L, (color & 255) / 255.);
  char hex[9];
  snprintf(hex, sizeof(hex), "%08x", color);
  lua_pushstring(L, hex);
  return 4;
}
static int missing_constant(lua_State *L) {
  return luaL_error(L, "World UI static constant is not supplied: %s", luaL_checkstring(L, 2));
}
static int register_constants(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_newtable(L);
  lua_pushcfunction(L, missing_constant);
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, 1);
  return 0;
}
static int table_wipe(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    lua_pop(L, 1);
    lua_pushvalue(L, -1);
    lua_pushnil(L);
    lua_rawset(L, 1);
  }
  lua_settop(L, 1);
  return 1;
}
uint32_t bw_world_ui_register(lua_State *L) {
  memset(&player, 0, sizeof(player));
  lua_pushcfunction(L, vehicle_exit);
  lua_setglobal(L, "VehicleExit");
  lua_pushcfunction(L, quality_color);
  lua_setglobal(L, "GetItemQualityColor");
  lua_pushcfunction(L, register_constants);
  lua_setglobal(L, "RegisterStaticConstants");
  lua_pushcfunction(L, class_list);
  lua_setglobal(L, "FillLocalizedClassList");
  lua_getglobal(L, "table");
  lua_pushcfunction(L, table_wipe);
  lua_setfield(L, -2, "wipe");
  lua_pop(L, 1);
  lua_newtable(L);
  const char *bit_names[] = {"band", "bor", "bxor", "bnot", "lshift", "rshift"};
  for (int i = 0; i < 6; i++) {
    lua_pushinteger(L, i);
    lua_pushcclosure(L, bit_operation, 1);
    lua_setfield(L, -2, bit_names[i]);
  }
  lua_setglobal(L, "bit");
  const char *names[] = {"GetTime",    "GetLocale", "GetActionBarPage", "GetBuildInfo",
                         "UnitExists", "UnitName",  "UnitLevel",        "UnitRace",
                         "UnitClass",  "UnitSex",   "UnitFactionGroup", "UnitGUID"};
  for (uint32_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
    lua_pushinteger(L, i);
    lua_pushcclosure(L, call, 1);
    lua_setglobal(L, names[i]);
  }
  return 0;
}
