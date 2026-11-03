#include "world_hud_api.h"
#include "lauxlib.h"
#include <string.h>
static uint32_t snapshot[4251], seen[8], flagged[8], page, zoom, minutes;
typedef struct {
  uint32_t id, kind, order, form;
  char path[256], name[128];
} Record;
static Record records[512];
static uint32_t record_count;
static uint32_t word(const uint8_t *b) {
  return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
uint32_t bw_world_hud_state(const uint8_t *b, uint32_t n) {
  if (n != sizeof(snapshot) || word(b + 146 * 4) > 4096)
    return 1;
  memcpy(snapshot, b, n);
  for (int i = 0; i < 8; i++)
    snapshot[4243 + i] |= flagged[i];
  return 0;
}
uint32_t bw_world_hud_record(const uint8_t *b, uint32_t n) {
  if (n < 18)
    return 1;
  const char *path = (const char *)b + 16, *end = memchr(path, 0, n - 16);
  if (!end || end - path >= 256)
    return 1;
  const char *name = end + 1, *last = memchr(name, 0, n - (uint32_t)(name - (const char *)b));
  if (!last || last - name >= 128)
    return 1;
  uint32_t id = word(b), kind = word(b + 4), at = 0;
  while (at < record_count && (records[at].id != id || records[at].kind != kind))
    at++;
  if (at >= 512)
    return 1;
  if (at == record_count)
    record_count++;
  Record *r = records + at;
  r->id = id;
  r->kind = kind;
  r->order = word(b + 8);
  r->form = word(b + 12);
  memcpy(r->path, path, end - path + 1);
  memcpy(r->name, name, last - name + 1);
  return 0;
}
uint32_t bw_world_hud_seen(uint32_t i) {
  if (i >= 8)
    return 0;
  uint32_t v = seen[i];
  seen[i] = 0;
  return v;
}
uint32_t bw_world_hud_zoom(void) {
  return zoom;
}
void bw_world_hud_time(uint32_t value) {
  minutes = value % 1440;
}
static Record *record(uint32_t id, uint32_t kind) {
  for (uint32_t i = 0; i < record_count; i++)
    if (records[i].id == id && records[i].kind == kind)
      return records + i;
  return 0;
}
static int action_page(lua_State *L) {
  if (lua_gettop(L)) {
    int value = luaL_checkinteger(L, 1);
    if (value < 1 || value > 6)
      return luaL_error(L, "Action page outside range");
    page = value;
  }
  lua_pushinteger(L, page);
  return 1;
}
static int slot(lua_State *L) {
  int index = luaL_checkinteger(L, 1);
  if (index < 1 || index > 12)
    return luaL_error(L, "Invalid action slot");
  uint32_t packed = snapshot[2 + (page - 1) * 12 + index - 1], id = packed & 0xffffff,
           kind = packed >> 24;
  Record *r = record(id, kind);
  lua_pushinteger(L, id);
  lua_pushinteger(L, kind);
  if (r)
    lua_pushstring(L, r->path);
  else
    lua_pushnil(L);
  lua_pushstring(L, r ? r->name : "");
  return 4;
}
static int learned(uint32_t id) {
  for (uint32_t i = 0; i < snapshot[146] && i < 4096; i++)
    if (snapshot[147 + i] == id)
      return 1;
  return 0;
}
static int forms(lua_State *L) {
  uint32_t indices[512], n = 0;
  for (uint32_t i = 0; i < record_count; i++)
    if (!records[i].kind && records[i].order && learned(records[i].id)) {
      uint32_t at = n;
      while (at && records[indices[at - 1]].order > records[i].order) {
        indices[at] = indices[at - 1];
        at--;
      }
      indices[at] = i;
      n++;
    }
  lua_newtable(L);
  for (uint32_t i = 0; i < n && i < 10; i++) {
    Record *r = records + indices[i];
    lua_newtable(L);
    lua_pushinteger(L, r->id);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, r->form);
    lua_setfield(L, -2, "form");
    lua_pushstring(L, r->path);
    lua_setfield(L, -2, "path");
    lua_pushstring(L, r->name);
    lua_setfield(L, -2, "name");
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}
static int tutorial(lua_State *L) {
  int op = lua_tointeger(L, lua_upvalueindex(1)), id = luaL_checkinteger(L, 1);
  if (id < 1 || id > 256)
    return luaL_error(L, "Tutorial outside range");
  uint32_t bit = id - 1, mask = 1u << (bit % 32);
  if (op == 0) {
    lua_pushboolean(L, (snapshot[4243 + bit / 32] & mask) != 0);
    return 1;
  }
  snapshot[4243 + bit / 32] |= mask;
  flagged[bit / 32] |= mask;
  seen[bit / 32] |= mask;
  return 0;
}
static int previous_next(lua_State *L) {
  int step = lua_tointeger(L, lua_upvalueindex(1)), id = luaL_checkinteger(L, 1) + step;
  for (; id > 0 && id <= 60; id += step)
    if (snapshot[4243 + (id - 1) / 32] & (1u << ((id - 1) % 32))) {
      lua_pushinteger(L, id);
      return 1;
    }
  lua_pushnil(L);
  return 1;
}
static int ready(lua_State *L) {
  lua_pushinteger(L, snapshot[1]);
  return 1;
}
static int set_zoom(lua_State *L) {
  int value = (int)zoom + luaL_checkinteger(L, 1);
  if (value < 0)
    value = 0;
  if (value > 5)
    value = 5;
  zoom = value;
  lua_pushinteger(L, zoom);
  return 1;
}
static int time(lua_State *L) {
  lua_pushinteger(L, minutes / 60);
  lua_pushinteger(L, minutes % 60);
  return 2;
}
static int binding(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  const char *keys[] = {"MOVEFORWARD", "MOVEBACKWARD", "TURNLEFT", "TURNRIGHT", "JUMP"},
             *values[] = {"W", "S", "A", "D", "SPACE"};
  for (int i = 0; i < 5; i++)
    if (!strcmp(name, keys[i])) {
      lua_pushstring(L, values[i]);
      return 1;
    }
  lua_pushstring(L, name);
  return 1;
}
static int chat_index(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  const char *types[] = {"SYSTEM",
                         "SAY",
                         "PARTY",
                         "RAID",
                         "GUILD",
                         "OFFICER",
                         "YELL",
                         "WHISPER",
                         "WHISPER_FOREIGN",
                         "WHISPER_INFORM",
                         "EMOTE",
                         "TEXT_EMOTE",
                         "MONSTER_SAY",
                         "MONSTER_PARTY",
                         "MONSTER_YELL",
                         "MONSTER_WHISPER",
                         "MONSTER_EMOTE",
                         "CHANNEL",
                         "CHANNEL_JOIN",
                         "CHANNEL_LEAVE",
                         "CHANNEL_LIST",
                         "CHANNEL_NOTICE",
                         "CHANNEL_NOTICE_USER",
                         "AFK",
                         "DND",
                         "IGNORED",
                         "SKILL",
                         "LOOT",
                         "MONEY",
                         "OPENING",
                         "TRADESKILLS",
                         "PET_INFO",
                         "COMBAT_MISC_INFO",
                         "COMBAT_XP_GAIN",
                         "COMBAT_HONOR_GAIN",
                         "COMBAT_FACTION_CHANGE",
                         "BG_SYSTEM_NEUTRAL",
                         "BG_SYSTEM_ALLIANCE",
                         "BG_SYSTEM_HORDE",
                         "RAID_LEADER",
                         "RAID_WARNING",
                         "RAID_BOSS_EMOTE",
                         "RAID_BOSS_WHISPER",
                         "FILTERED",
                         "BATTLEGROUND",
                         "BATTLEGROUND_LEADER",
                         "RESTRICTED",
                         "BATTLENET",
                         "ACHIEVEMENT",
                         "GUILD_ACHIEVEMENT",
                         "ARENA_POINTS",
                         "PARTY_LEADER"};
  for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++)
    if (!strcmp(name, types[i])) {
      lua_pushinteger(L, i + 1);
      return 1;
    }
  const char *extra[] = {"TARGETICONS",
                         "REPLY",
                         "CHANNEL1",
                         "CHANNEL2",
                         "CHANNEL3",
                         "CHANNEL4",
                         "CHANNEL5",
                         "CHANNEL6",
                         "CHANNEL7",
                         "CHANNEL8",
                         "CHANNEL9",
                         "CHANNEL10",
                         "BN_WHISPER",
                         "BN_WHISPER_INFORM",
                         "BN_CONVERSATION",
                         "BN_CONVERSATION_NOTICE",
                         "BN_CONVERSATION_LIST",
                         "BN_ALERT",
                         "BN_BROADCAST",
                         "BN_BROADCAST_INFORM",
                         "BN_INLINE_TOAST_ALERT",
                         "BN_INLINE_TOAST_BROADCAST",
                         "BN_INLINE_TOAST_BROADCAST_INFORM",
                         "BN_INLINE_TOAST_CONVERSATION"};
  for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); i++)
    if (!strcmp(name, extra[i])) {
      lua_pushinteger(L, 53 + i);
      return 1;
    }
  return luaL_error(L, "Unsupported chat category: %s", name);
}
void bw_world_hud_register(lua_State *L) {
  memset(snapshot, 0, sizeof(snapshot));
  memset(seen, 0, sizeof(seen));
  memset(flagged, 0, sizeof(flagged));
  lua_pushcfunction(L, chat_index);
  lua_setglobal(L, "GetChatTypeIndex");
  page = 1;
  zoom = minutes = record_count = 0;
  const char *names[] = {"GetActionBarPage", "WorldHUDPage",  "WorldHUDSlot",
                         "WorldHUDForms",    "WorldHUDReady", "WorldHUDZoom",
                         "GetGameTime",      "GetBindingKey", "GetBindingText"};
  lua_CFunction funcs[] = {action_page, action_page, slot,    forms,  ready,
                           set_zoom,    time,        binding, binding};
  for (int i = 0; i < 9; i++) {
    lua_pushcfunction(L, funcs[i]);
    lua_setglobal(L, names[i]);
  }
  for (int i = 0; i < 2; i++) {
    lua_pushinteger(L, i);
    lua_pushcclosure(L, tutorial, 1);
    lua_setglobal(L, i ? "FlagTutorial" : "IsTutorialFlagged");
  }
  for (int i = 0; i < 2; i++) {
    lua_pushinteger(L, i ? 1 : -1);
    lua_pushcclosure(L, previous_next, 1);
    lua_setglobal(L, i ? "GetNextCompleatedTutorial" : "GetPrevCompleatedTutorial");
  }
}
