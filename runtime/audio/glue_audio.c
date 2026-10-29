/* Selected native callback argument/return boundary. Reconstructed provider
 * transport; not FMOD mixer, playlist, gain or native scheduling equivalence. */
#include "glue_audio.h"
#include "lauxlib.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
static uint32_t last_token; /* Never reused across provider reset in one VM. */
typedef struct Asset {
  uint32_t channel, id;
  char kit[128];
} Asset;
struct BwGlueAudio {
  lua_State *L;
  BwGlueAudio **handle;
  int reference;
  Asset assets[128];
  uint32_t asset_count;
  BwGlueAudioRequest queue[128];
  uint32_t head, count, issued, completed, failures;
};
static BwGlueAudio *current(lua_State *L) {
  BwGlueAudio **p = lua_touserdata(L, lua_upvalueindex(1));
  if (!p || !*p)
    luaL_error(L, "closed Glue audio provider");
  return *p;
}
static int call(lua_State *L) {
  BwGlueAudio *a = current(L);
  uint32_t operation = (uint32_t)lua_tointeger(L, lua_upvalueindex(2)), channel = operation & 1,
           play = operation < 2;
  uint32_t id = 0;
  float fade = channel ? 1 : -1;
  if (play) {
    const char *kit = luaL_checkstring(L, 1);
    for (uint32_t i = 0; i < a->asset_count; i++)
      if (a->assets[i].channel == channel && !strcmp(a->assets[i].kit, kit)) {
        id = a->assets[i].id;
        break;
      }
    if (!id)
      return luaL_error(L, "Glue audio kit not admitted: %s", kit);
    fade = channel ? (lua_isnumber(L, 2) ? (float)lua_tonumber(L, 2) : -1) : 0;
  }
  if (!isfinite(fade) || (fade < 0 && fade != -1) || fade > 60)
    return luaL_error(L, "unsupported Glue audio fade");
  if (a->count >= 128 || last_token == 0xffffffffu)
    return luaL_error(L, "Glue audio request capacity");
  BwGlueAudioRequest r = {++last_token, play ? BW_GLUE_AUDIO_PLAY : BW_GLUE_AUDIO_STOP, channel, id,
                          fade};
  a->queue[(a->head + a->count) % 128] = r;
  a->count++;
  return 0;
}
static int sound(lua_State *L) {
  BwGlueAudio *a = current(L);
  uint32_t id = 0;
  if (lua_isnumber(L, 1)) {
    lua_Number n = lua_tonumber(L, 1);
    if (!isfinite(n) || n < 1 || n > 4294967295.0 || floor(n) != n)
      return luaL_error(L, "unsupported sound kit id");
    uint32_t wanted = (uint32_t)n;
    for (uint32_t i = 0; i < a->asset_count; i++)
      if (a->assets[i].channel == 2 && a->assets[i].id == wanted) {
        id = wanted;
        break;
      }
  } else if (lua_isstring(L, 1)) {
    const char *kit = lua_tostring(L, 1);
    for (uint32_t i = 0; i < a->asset_count; i++)
      if (a->assets[i].channel == 2 && !strcmp(a->assets[i].kit, kit)) {
        id = a->assets[i].id;
        break;
      }
  } else
    return luaL_error(L, "sound kit must be name or id");
  if (!id)
    return luaL_error(L, "sound kit not admitted");
  if (a->count >= 128 || last_token == 0xffffffffu)
    return luaL_error(L, "Glue audio request capacity");
  BwGlueAudioRequest r = {++last_token, BW_GLUE_AUDIO_PLAY, 2, id, 0};
  a->queue[(a->head + a->count) % 128] = r;
  a->count++;
  return 0;
}
/* Original AccountLogin_OnHide requests a bounded fade for all SFX. Browser
 * provider completion is distinct from native FMOD scheduling/output. */
static int stop_sounds(lua_State *L) {
  BwGlueAudio *a = current(L);
  double fade = luaL_optnumber(L, 1, 0);
  if (!isfinite(fade) || fade < 0 || fade > 60)
    return luaL_error(L, "unsupported SFX stop fade");
  if (a->count >= 128 || last_token == 0xffffffffu)
    return luaL_error(L, "Glue audio request capacity");
  BwGlueAudioRequest r = {++last_token, BW_GLUE_AUDIO_STOP, 2, 0, (float)fade};
  a->queue[(a->head + a->count) % 128] = r;
  a->count++;
  return 0;
}
BwGlueAudio *bw_glue_audio_new(lua_State *L) {
  if (!L)
    return NULL;
  BwGlueAudio *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  a->L = L;
  a->handle = lua_newuserdata(L, sizeof(*a->handle));
  *a->handle = a;
  a->reference = luaL_ref(L, LUA_REGISTRYINDEX);
  return a;
}
void bw_glue_audio_free(BwGlueAudio *a) {
  if (!a)
    return;
  *a->handle = NULL;
  luaL_unref(a->L, LUA_REGISTRYINDEX, a->reference);
  free(a);
}
uint32_t bw_glue_audio_register(BwGlueAudio *a) {
  if (!a)
    return 1;
  const char *names[] = {"PlayGlueMusic", "PlayGlueAmbience", "StopGlueMusic", "StopGlueAmbience"};
  for (int i = 0; i < 4; i++) {
    lua_rawgeti(a->L, LUA_REGISTRYINDEX, a->reference);
    lua_pushinteger(a->L, i);
    lua_pushcclosure(a->L, call, 2);
    lua_setglobal(a->L, names[i]);
  }
  lua_rawgeti(a->L, LUA_REGISTRYINDEX, a->reference);
  lua_pushinteger(a->L, 0);
  lua_pushcclosure(a->L, call, 2);
  lua_setglobal(a->L, "PlayCreditsMusic");
  lua_rawgeti(a->L, LUA_REGISTRYINDEX, a->reference);
  lua_pushcclosure(a->L, sound, 1);
  lua_setglobal(a->L, "PlaySound");
  lua_rawgeti(a->L, LUA_REGISTRYINDEX, a->reference);
  lua_pushcclosure(a->L, stop_sounds, 1);
  lua_setglobal(a->L, "StopAllSFX");
  return 0;
}
uint32_t bw_glue_audio_asset(BwGlueAudio *a, uint32_t channel, const char *kit, uint32_t id) {
  if (!a || channel > 2 || !kit || !*kit || strlen(kit) >= 128 || !id || a->asset_count >= 128)
    return 1;
  for (uint32_t i = 0; i < a->asset_count; i++)
    if (a->assets[i].channel == channel &&
        (!strcmp(a->assets[i].kit, kit) || a->assets[i].id == id))
      return 1;
  Asset *s = &a->assets[a->asset_count++];
  s->channel = channel;
  s->id = id;
  strcpy(s->kit, kit);
  return 0;
}
uint32_t bw_glue_audio_next(BwGlueAudio *a, BwGlueAudioRequest *r) {
  if (!a || !r || a->issued >= a->count)
    return 1;
  *r = a->queue[(a->head + a->issued) % 128];
  a->issued++;
  return 0;
}
uint32_t bw_glue_audio_ack(BwGlueAudio *a, uint32_t token, uint32_t failure) {
  if (!a || !a->issued || failure > 1 || a->queue[a->head].token != token)
    return 1;
  a->head = (a->head + 1) % 128;
  a->count--;
  a->issued--;
  a->completed++;
  if (failure)
    a->failures++;
  return 0;
}
uint32_t bw_glue_audio_pending(const BwGlueAudio *a) {
  return a ? a->count : 0;
}
uint32_t bw_glue_audio_failures(const BwGlueAudio *a) {
  return a ? a->failures : 0;
}
uint32_t bw_glue_audio_completed(const BwGlueAudio *a) {
  return a ? a->completed : 0;
}
