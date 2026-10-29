/* Selected offline Glue state. No authentication/realm/character success stubs.
 * Original static callback evidence: glue-host-contracts.md. */
#include "glue_state.h"
#include "realm_list.h"
#include "lauxlib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
typedef struct Cvar {
  char name[64], value[256], default_value[256];
} Cvar;
struct BwGlueState {
  lua_State *L;
  BwGlueAudio *audio;
  BwCharacters *characters;
  struct BwGlueState **handle;
  int reference, profile, uses_token, is_trial;
  int neutral_gamma, realm_configured, movie_profile, options_readonly;
  char realm_name[256];
  BwRealmList realms;
  uint32_t realms_authenticated;
  uint32_t agreements_configured, agreements_accepted, agreements_notices, agreement_profiles;
  int32_t agreement_selectors[5];
  char locale[16];
  uint32_t character_count, expansion;
  uint32_t video[6];
  int video_configured, caps_configured;
  uint32_t caps_flags, buffering, max_anisotropy, processors;
  char audio_output[128];
  char *saved_name, *saved_list;
  Cvar cvars[256];
  uint32_t cvar_count;
  BwGlueScreenBarrier screen_barrier;
  void *screen_context;
  uint32_t screen, screen_busy;
  uint32_t auth_enabled, auth_active, auth_generation, world_active;
  char auth_account[64], auth_password[128];
  uint32_t scan_profile, scan_state, scan_finished, system_profile, system_flags, warning_profile;
};
static void clear_auth_credentials(BwGlueState *s) {
  volatile char *p = s->auth_account;
  for (uint32_t i = 0; i < sizeof(s->auth_account); i++)
    p[i] = 0;
  p = s->auth_password;
  for (uint32_t i = 0; i < sizeof(s->auth_password); i++)
    p[i] = 0;
}
static BwGlueState *current(lua_State *L) {
  BwGlueState **p = lua_touserdata(L, lua_upvalueindex(1));
  if (!p || !*p)
    luaL_error(L, "closed Glue state");
  return *p;
}
uint32_t bw_glue_state_apply_config(BwGlueState *s, const BwWtfConfig *cfg, uint32_t *applied,
                                    uint32_t *unknown) {
  if (applied)
    *applied = 0;
  if (unknown)
    *unknown = 0;
  if (!s || !cfg || cfg->error_line || cfg->count > BW_WTF_ENTRIES || s->screen || s->screen_busy)
    return 1;
  for (uint32_t i = 0; i < cfg->count; i++)
    if (!memchr(cfg->entries[i].name, 0, sizeof(cfg->entries[i].name)) ||
        !cfg->entries[i].name[0] ||
        !memchr(cfg->entries[i].value, 0, sizeof(cfg->entries[i].value)))
      return 1;
  uint32_t a = 0, u = 0;
  for (uint32_t i = 0; i < cfg->count; i++) {
    const BwWtfEntry *e = &cfg->entries[i];
    uint32_t j;
    for (j = 0; j < s->cvar_count; j++)
      if (!strcmp(s->cvars[j].name, e->name)) {
        strcpy(s->cvars[j].value, e->value);
        a++;
        break;
      }
    if (j == s->cvar_count)
      u++;
  }
  if (applied)
    *applied = a;
  if (unknown)
    *unknown = u;
  return 0;
}
/* Temporary browser policy requested by the owner: original panels remain
 * navigable, while graphics/audio settings remain at registered defaults. */
static int option_cvar(const char *n) {
  return strcmp(n, "realmName") && strcmp(n, "showToolsUI") && strcmp(n, "movie") &&
         strcmp(n, "expansionMovie") && strncmp(n, "read", 4);
}
uint32_t bw_glue_state_options_readonly(BwGlueState *s) {
  if (!s)
    return 1;
  s->options_readonly = 1;
  for (uint32_t i = 0; i < s->cvar_count; i++)
    if (option_cvar(s->cvars[i].name))
      strcpy(s->cvars[i].value, s->cvars[i].default_value);
  return 0;
}
static const char *agreement_names[] = {"readTOS", "readEULA", "readTerminationWithoutNotice",
                                        "readScanning", "readContest"};
static int call(lua_State *L) {
  BwGlueState *s = current(L);
  int operation = (int)lua_tointeger(L, lua_upvalueindex(2));
  if (operation == 79) {
    if (!s->realms_authenticated)
      return luaL_error(L, "realm preferences unavailable");
    uint32_t category = (uint32_t)luaL_checkinteger(L, 1);
    if (bw_realms_suggest(&s->realms, category, lua_toboolean(L, 2), lua_toboolean(L, 3)))
      return luaL_error(L, "invalid realm preferences");
    return 0;
  }
  if (operation >= 66 && operation <= 78)
    return bw_realms_lua(L, &s->realms, (uint32_t)operation - 66);
  if (operation == 65) {
    s->uses_token = lua_toboolean(L, 1);
    return 0;
  }
  if (operation == 62)
    return 0; /* Owner policy: external links inert. */
  if (operation == 64) {
    bw_characters_cancel(s->characters);
    clear_auth_credentials(s);
    s->auth_active = 0;
    s->world_active = 0;
    return 0;
  }
  if (operation == 63) {
    if (s->auth_active || s->world_active)
      return 0;
    if (s->auth_enabled) {
      size_t account_length, password_length;
      const char *account = luaL_checklstring(L, 1, &account_length);
      const char *password = luaL_checklstring(L, 2, &password_length);
      if (account_length >= sizeof(s->auth_account) || password_length >= sizeof(s->auth_password))
        return luaL_error(L, "account credential capacity exceeded");
      clear_auth_credentials(s);
      memcpy(s->auth_account, account, account_length);
      memcpy(s->auth_password, password, password_length);
      s->auth_active = 1;
      if (++s->auth_generation == 0)
        ++s->auth_generation;
    }
    lua_getglobal(L, "GlueDialog_Show");
    lua_pushstring(L, s->auth_enabled ? "CANCEL" : "OKAY");
    lua_pushstring(L, s->auth_enabled ? "Connecting to authentication server..."
                                      : "Server connection is not configured yet.");
    lua_call(L, 2, 0);
    return 0;
  }
  if (operation >= 58 && operation <= 61) {
    if (!s->options_readonly)
      return luaL_error(L, "read-only options backend not configured");
    if (operation == 58 || operation == 59) {
      double index = luaL_checknumber(L, 1);
      if (!isfinite(index) || index < 1)
        return luaL_error(L, "read-only options index");
    }
    return 0;
  }
  if (operation == 57)
    return 0; /* Browser Quit is explicitly inert. */
  /* Selected legacy scanner boundary. Browser provider reports unavailable,
  * never a clean scan. Original Lua handles SCANDLL_ERROR and continuation. */
  if (operation == 56) {
    if (!s->warning_profile)
      return luaL_error(L, "changed-option warning profile missing");
    lua_pushnil(L);
    return 1;
  }
  if (operation == 55) {
    if (!s->system_profile)
      return luaL_error(L, "portable system capability profile missing");
    if (s->system_flags & 4)
      lua_pushnumber(L, 1);
    else
      lua_pushnil(L);
    return 1;
  }
  if (operation >= 52 && operation <= 54) {
    if (!s->scan_profile)
      return luaL_error(L, "scanner platform boundary not configured");
    if (operation == 54) {
      if (s->scan_finished)
        lua_pushnumber(L, 1);
      else
        lua_pushnil(L);
      return 1;
    }
    if (operation == 53) {
      if (s->scan_state == 2 || s->scan_state == 3)
        s->scan_finished = 1;
      return 0;
    }
    size_t a, b;
    luaL_checklstring(L, 1, &a);
    luaL_checklstring(L, 2, &b);
    if (a > 2048 || b > 2048)
      return luaL_error(L, "scanner URL capacity");
    if (!s->scan_state)
      s->scan_state = 1;
    return 0;
  }
  if (operation >= 47 && operation < 52) {
    uint32_t index = (uint32_t)operation - 47, bit = 1u << index;
    if (!s->agreements_configured || !(s->agreement_profiles & bit))
      return luaL_error(L, "agreement configuration boundary not configured");
    Cvar *c = NULL;
    for (uint32_t i = 0; i < s->cvar_count; i++)
      if (!strcmp(s->cvars[i].name, agreement_names[index])) {
        c = &s->cvars[i];
        break;
      }
    if (!c)
      return luaL_error(L, "agreement CVar not registered");
    /* Selected native helpers4d7e40..4d7f30 change flags before setter.
   * Host explicitly supplies the native+30 selector; its meaning is unknown. */
    s->agreements_accepted |= bit;
    s->agreements_notices &= ~bit;
    strcpy(c->value, index < 3 && s->agreement_selectors[index] == -2 ? "-1" : "1");
    return 0;
  }
  if (operation == 39) {
    const char *name = luaL_checkstring(L, 1);
    if (!strcmp(name, "realmwizard")) {
      if (s->screen_busy || !s->realms_authenticated)
        return luaL_error(L, "realm wizard requires an authenticated realm list");
      s->screen = 6;
      return 0;
    }
    if (!strcmp(name, "charselect") || !strcmp(name, "charcreate")) {
      if (s->screen_busy || !bw_characters_metric(s->characters, 0))
        return luaL_error(L, "character screen requires an authenticated world session");
      s->screen = !strcmp(name, "charselect") ? 4 : 5;
      return 0;
    }
    if (!strcmp(name, "movie")) {
      if (!s->movie_profile || s->screen == 2 || s->screen_busy)
        return luaL_error(L, "movie screen not configured/already entered");
      s->screen = 2;
      return 0;
    }
    if (!strcmp(name, "credits")) {
      if (s->screen_busy || (s->screen != 1 && s->screen != 3))
        return luaL_error(L, "credits requires an active login screen");
      s->screen = 3;
      return 0;
    }
    if (strcmp(name, "login"))
      return luaL_error(L, "unsupported screen transition");
    if (s->screen == 3 || s->screen == 4 || s->screen == 5 || s->screen == 6) {
      s->screen = 1;
      return 0;
    }
    if (s->screen == 1 || s->screen_busy)
      return luaL_error(L, "initial login transition already entered");
    if (!s->screen_barrier)
      return luaL_error(L, "screen resource barrier not configured");
    s->screen_busy = 1;
    uint32_t failure = s->screen_barrier(s->screen_context);
    s->screen_busy = 0;
    if (failure)
      return luaL_error(L, "screen resource barrier failed");
    s->screen = 1;
    return 0;
  }
  if (operation == 40 || operation == 41) {
    if (!s->neutral_gamma)
      return luaL_error(L, "browser gamma profile not configured");
    if (operation == 40) {
      lua_pushnumber(L, 0);
      return 1;
    }
    if (s->options_readonly) {
      if (!isfinite(luaL_checknumber(L, 1)))
        return luaL_error(L, "invalid gamma");
      return 0;
    }
    if (!isfinite(luaL_checknumber(L, 1)) || fabs(lua_tonumber(L, 1)) > 0.0000001)
      return luaL_error(L, "non-neutral gamma correction not implemented (%f)", lua_tonumber(L, 1));
    return 0;
  }
  /* 0x54eb10 reads 1 - shadowLevel. This renderer advertises no terrain
  * shadows: selected off value is 1; mutations requiring shadows reject. */
  if (operation == 46) {
    Cvar *c = NULL;
    for (uint32_t i = 0; i < s->cvar_count; i++)
      if (!strcmp(s->cvars[i].name, "gxResolution")) {
        c = &s->cvars[i];
        break;
      }
    unsigned width = 0, height = 0;
    char tail;
    if (!c || sscanf(c->value, "%ux%u%c", &width, &height, &tail) != 2 || !width || !height ||
        width > 16384 || height > 16384)
      return luaL_error(L, "unsupported movie resolution profile");
    lua_pushnumber(L, width);
    return 1;
  }
  if (operation == 44 || operation == 45) {
    if (!s->caps_configured || s->caps_flags)
      return luaL_error(L, "minimal terrain profile not configured");
    if (operation == 44) {
      lua_pushnumber(L, 1);
      return 1;
    }
    double value = luaL_checknumber(L, 1);
    if (s->options_readonly && isfinite(value))
      return 0;
    if (!isfinite(value) || fabs(value - 1) > 0.0000001)
      return luaL_error(L, "terrain shadows not implemented");
    return 0;
  }
  if (operation == 38) {
    if (s->realms_authenticated)
      return bw_realms_server(L, &s->realms);
    if (!s->realm_configured)
      return luaL_error(L, "offline realm profile not configured");
    lua_pushstring(L, s->realm_name);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 1);
    return 4;
  }
  if (operation >= 28 && operation < 38) {
    if (!s->agreements_configured)
      return luaL_error(L, "agreement profile not configured");
    uint32_t index = (operation - 28) / 2,
             bits = (operation & 1) ? s->agreements_accepted : s->agreements_notices;
    if (bits & (1u << index))
      lua_pushnumber(L, 1);
    else
      lua_pushnil(L);
    return 1;
  }
  if (operation == 24) {
    if (!s->caps_configured)
      return luaL_error(L, "host graphics capability profile not configured");
    if (s->caps_flags & 32)
      lua_pushnumber(L, 1);
    else
      lua_pushnil(L);
    return 1;
  }
  if (operation == 25) {
    if (!s->processors)
      return luaL_error(L, "host processor profile not configured");
    if (s->processors > 1)
      lua_pushnumber(L, 1);
    else
      lua_pushnil(L);
    return 1;
  }
  if (operation == 26 || operation == 27) {
    if (!s->audio_output[0])
      return luaL_error(L, "host audio output profile not configured");
    if (operation == 26) {
      lua_pushnumber(L, 1);
      return 1;
    }
    if (!isfinite(luaL_checknumber(L, 1)) || fabs(lua_tonumber(L, 1)) > 0.0000001)
      return luaL_error(L, "unsupported audio output index");
    lua_pushstring(L, s->audio_output);
    return 1;
  }
  if (operation == 23) {
    if (!s->caps_configured)
      return luaL_error(L, "host graphics capability profile not configured");
    for (int i = 0; i < 4; i++) {
      if (s->caps_flags & (1u << i))
        lua_pushnumber(L, 1);
      else
        lua_pushnil(L);
    }
    lua_pushnumber(L, s->buffering);
    if (s->max_anisotropy)
      lua_pushnumber(L, s->max_anisotropy);
    else
      lua_pushnil(L);
    if (s->caps_flags & 16)
      lua_pushnumber(L, 1);
    else
      lua_pushnil(L);
    return 7;
  }
  if (operation >= 18 && operation <= 22) {
    if (!s->video_configured)
      return luaL_error(L, "host display profile not configured");
    if (operation == 18) {
      char mode[40];
      snprintf(mode, sizeof(mode), "%ux%u", s->video[0], s->video[1]);
      lua_pushstring(L, mode);
      return 1;
    }
    if (operation == 19 || operation == 22) {
      lua_pushnumber(L, 1);
      return 1;
    }
    if (operation == 20) {
      lua_pushnumber(L, s->video[2]);
      return 1;
    }
    for (int i = 3; i < 6; i++)
      lua_pushnumber(L, s->video[i]);
    return 3;
  }
  if (operation == 0) {
    lua_pushnumber(L, bw_characters_metric(s->characters, 0)
                          ? bw_characters_metric(s->characters, 1)
                          : s->character_count);
    return 1;
  }
  if (operation == 1) {
    lua_pushstring(L, s->locale);
    return 1;
  }
  if (operation == 15 || operation == 17) {
    lua_pushnil(L);
    return 1;
  }
  if (operation == 16) {
    lua_pushnumber(L, 1);
    return 1;
  }
  if (operation == 14) {
    if (!s->expansion)
      return luaL_error(L, "client expansion not configured");
    lua_pushnumber(L, s->expansion);
    return 1;
  }
  if (operation == 2) {
    lua_getglobal(L, "VERSION");
    if (!lua_isstring(L, -1))
      return luaL_error(L, "VERSION resource not loaded");
    lua_getglobal(L, "RELEASE_BUILD");
    if (!lua_isstring(L, -1))
      return luaL_error(L, "RELEASE_BUILD resource not loaded");
    lua_pushliteral(L, "3.3.5");
    lua_pushliteral(L, "12340");
    lua_pushliteral(L, "Jun 24 2010");
    return 5;
  }
  if (!s->profile)
    return luaL_error(L, "Glue profile not configured");
  if (operation == 3 || operation == 4) {
    lua_pushstring(L, operation == 3 ? s->saved_name : s->saved_list);
    return 1;
  }
  if (operation == 5 || operation == 6) {
    size_t length;
    const char *value = luaL_checklstring(L, 1, &length);
    if (length > 4096)
      return luaL_error(L, "saved profile field capacity");
    char *next = malloc(length + 1);
    if (!next)
      return luaL_error(L, "profile allocation");
    memcpy(next, value, length);
    next[length] = 0;
    char **slot = operation == 5 ? &s->saved_name : &s->saved_list;
    free(*slot);
    *slot = next;
    return 0;
  }
  if (operation == 7) {
    lua_pushboolean(L, s->uses_token);
    return 1;
  }
  if (operation == 8) {
    if (s->is_trial)
      lua_pushnumber(L, 1);
    else
      lua_pushnil(L);
    return 1;
  }
  /* Admitted nonstreaming profile only. Native tail callback pushes no result
  * on the selected false branch; not a universal streaming implementation. */
  if (operation == 9)
    return 0;
  /* Glue registrations 0xac40b8/0xac40c0 -> 0x4ddea0/0x4ddf80:
  * only extShadowQuality and farclip have limits. Browser profile advertises
  * no shadow feature, therefore its maximum is zero, not the Wine GPU limit. */
  if (operation == 42 || operation == 43) {
    const char *name = luaL_checkstring(L, 1);
    Cvar *c = NULL;
    for (uint32_t i = 0; i < s->cvar_count; i++)
      if (!strcmp(s->cvars[i].name, name)) {
        c = &s->cvars[i];
        break;
      }
    if (!c)
      return luaL_error(L, "unknown CVar: %s", name);
    if (!strcmp(name, "farclip"))
      lua_pushnumber(L, operation == 42 ? 177 : 1277);
    else if (!strcmp(name, "extShadowQuality")) {
      if (!s->caps_configured)
        return luaL_error(L, "graphics capability profile missing");
      lua_pushnumber(L, 0);
    } else
      lua_pushnil(L);
    return 1;
  }
  if (operation >= 10 && operation <= 13) {
    const char *name = luaL_checkstring(L, 1);
    Cvar *c = NULL;
    for (uint32_t i = 0; i < s->cvar_count; i++)
      if (!strcmp(s->cvars[i].name, name)) {
        c = &s->cvars[i];
        break;
      }
    if (!c)
      return luaL_error(L, "unknown CVar: %s", name);
    if (operation == 10 || operation == 11) {
      lua_pushstring(L, operation == 10 ? c->value : c->default_value);
      return 1;
    }
    if (operation == 12) {
      char *end;
      long n = strtol(c->value, &end, 10);
      if (*end)
        return luaL_error(L, "unsupported CVar boolean profile");
      if (n)
        lua_pushnumber(L, 1);
      else
        lua_pushnil(L);
      return 1;
    }
    const char *value = luaL_checkstring(L, 2);
    if (strlen(value) >= sizeof(c->value))
      return luaL_error(L, "CVar value capacity");
    if (!s->options_readonly || !option_cvar(name))
      strcpy(c->value, value);
    return 0;
  }
  return luaL_error(L, "unsupported Glue state operation");
}
BwGlueState *bw_glue_state_new(lua_State *L, const char *locale) {
  if (!L || !locale || !locale[0] || strlen(locale) >= 16)
    return NULL;
  BwGlueState *s = calloc(1, sizeof(*s));
  if (!s)
    return NULL;
  s->L = L;
  s->audio = bw_glue_audio_new(L);
  if (!s->audio) {
    free(s);
    return NULL;
  }
  s->characters = bw_characters_new(L);
  if (!s->characters) {
    bw_glue_audio_free(s->audio);
    free(s);
    return NULL;
  }
  strcpy(s->locale, locale);
  s->handle = lua_newuserdata(L, sizeof(*s->handle));
  *s->handle = s;
  s->reference = luaL_ref(L, LUA_REGISTRYINDEX);
  return s;
}
uint32_t bw_glue_state_register(BwGlueState *s) {
  if (!s || bw_glue_audio_register(s->audio) || bw_characters_register(s->characters))
    return 1;
  const char *names[] = {"GetNumCharacters",
                         "GetLocale",
                         "GetBuildInfo",
                         "GetSavedAccountName",
                         "GetSavedAccountList",
                         "SetSavedAccountName",
                         "SetSavedAccountList",
                         "GetUsesToken",
                         "IsTrialAccount",
                         "IsStreamingTrial",
                         "GetCVar",
                         "GetCVarDefault",
                         "GetCVarBool",
                         "SetCVar",
                         "GetClientExpansionLevel",
                         "IsMacClient",
                         "IsWindowsClient",
                         "IsLinuxClient",
                         "GetScreenResolutions",
                         "GetCurrentResolution",
                         "GetRefreshRates",
                         "GetMultisampleFormats",
                         "GetCurrentMultisampleFormat",
                         "GetVideoCaps",
                         "IsStereoVideoAvailable",
                         "IsPlayerResolutionAvailable",
                         "Sound_GameSystem_GetNumOutputDrivers",
                         "Sound_GameSystem_GetOutputDriverNameByIndex",
                         "ShowTOSNotice",
                         "TOSAccepted",
                         "ShowEULANotice",
                         "EULAAccepted",
                         "ShowTerminationWithoutNoticeNotice",
                         "TerminationWithoutNoticeAccepted",
                         "ShowScanningNotice",
                         "ScanningAccepted",
                         "ShowContestNotice",
                         "ContestAccepted",
                         "GetServerName",
                         "SetCurrentScreen",
                         "GetGamma",
                         "SetGamma",
                         "GetCVarMin",
                         "GetCVarMax",
                         "GetTerrainMip",
                         "SetTerrainMip",
                         "GetMovieResolution",
                         "AcceptTOS",
                         "AcceptEULA",
                         "AcceptTerminationWithoutNotice",
                         "AcceptScanning",
                         "AcceptContest",
                         "ScanDLLStart",
                         "ScanDLLContinueAnyway",
                         "IsScanDLLFinished",
                         "IsSystemSupported",
                         "ShowChangedOptionWarnings",
                         "QuitGame",
                         "SetScreenResolution",
                         "SetMultisampleFormat",
                         "RestartGx",
                         "Sound_GameSystem_RestartSoundSystem",
                         "LaunchURL",
                         "DefaultServerLogin",
                         "StatusDialogClick",
                         "SetUsesToken",
                         "GetNumRealms",
                         "GetRealmInfo",
                         "GetRealmCategories",
                         "RealmListUpdateRate",
                         "GetSelectedCategory",
                         "IsInvalidLocale",
                         "IsTournamentRealmCategory",
                         "IsInvalidTournamentRealmCategory",
                         "CancelRealmListQuery",
                         "RequestRealmList",
                         "RealmListDialogCancelled",
                         "ChangeRealm",
                         "SortRealms",
                         "SetPreferredInfo"};
  for (int i = 0; i < 80; i++) {
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, s->reference);
    lua_pushinteger(s->L, i);
    lua_pushcclosure(s->L, call, 2);
    lua_setglobal(s->L, names[i]);
  }
  return 0;
}
uint32_t bw_glue_state_define_cvar(BwGlueState *s, const char *name, const char *default_value,
                                   const char *value) {
  if (!s || !name || !*name || !default_value || !value || strlen(name) >= 64 ||
      strlen(default_value) >= 256 || strlen(value) >= 256 || s->cvar_count >= 256)
    return 1;
  for (uint32_t i = 0; i < s->cvar_count; i++)
    if (!strcmp(s->cvars[i].name, name))
      return 1;
  Cvar *c = &s->cvars[s->cvar_count++];
  strcpy(c->name, name);
  strcpy(c->value, value);
  strcpy(c->default_value, default_value);
  return 0;
}
uint32_t bw_glue_state_fresh_profile(BwGlueState *s) {
  if (!s || s->profile)
    return 1;
  s->saved_name = calloc(1, 1);
  s->saved_list = calloc(1, 1);
  if (!s->saved_name || !s->saved_list) {
    free(s->saved_name);
    free(s->saved_list);
    s->saved_name = s->saved_list = NULL;
    return 1;
  }
  /* Native registered default is -1 at 0x401f1e..401f3e. Explicit browser
  * capability policy: no Windows launcher, so selected current value is 0. */
  if (bw_glue_state_define_cvar(s, "showToolsUI", "-1", "0"))
    return 1;
  s->profile = 1;
  return 0;
}
void bw_glue_state_free(BwGlueState *s) {
  if (!s)
    return;
  clear_auth_credentials(s);
  bw_glue_audio_free(s->audio);
  bw_characters_free(s->characters);
  *s->handle = NULL;
  luaL_unref(s->L, LUA_REGISTRYINDEX, s->reference);
  free(s->saved_name);
  free(s->saved_list);
  free(s);
}

uint32_t bw_glue_state_expansion(BwGlueState *s, uint32_t level) {
  if (!s || level < 1 || level > 3)
    return 1;
  s->expansion = level;
  return 0;
}

uint32_t bw_glue_state_video(BwGlueState *s, uint32_t width, uint32_t height, uint32_t refresh,
                             uint32_t color_bits, uint32_t depth_bits, uint32_t samples) {
  if (!s || !width || width > 16384 || !height || height > 16384 || refresh > 1000 ||
      (color_bits != 16 && color_bits != 24 && color_bits != 32) ||
      (depth_bits != 16 && depth_bits != 24 && depth_bits != 32) || !samples || samples > 16 ||
      (samples & (samples - 1)))
    return 1;
  uint32_t v[6] = {width, height, refresh, color_bits, depth_bits, samples};
  memcpy(s->video, v, sizeof(v));
  s->video_configured = 1;
  return 0;
}

uint32_t bw_glue_state_video_caps(BwGlueState *s, uint32_t flags, uint32_t buffering,
                                  uint32_t maximum) {
  if (!s || flags > 63 || !buffering || buffering > 3 || maximum > 16 ||
      ((flags & 1) ? (!maximum || (maximum & (maximum - 1))) : maximum != 0))
    return 1;
  s->caps_flags = flags;
  s->buffering = buffering;
  s->max_anisotropy = maximum;
  s->caps_configured = 1;
  return 0;
}

uint32_t bw_glue_state_processors(BwGlueState *s, uint32_t count) {
  if (!s || !count || count > 65536)
    return 1;
  s->processors = count;
  return 0;
}
uint32_t bw_glue_state_audio_output(BwGlueState *s, const char *name) {
  if (!s || !name || !*name || strlen(name) >= sizeof(s->audio_output))
    return 1;
  strcpy(s->audio_output, name);
  return 0;
}

BwGlueAudio *bw_glue_state_audio(BwGlueState *s) {
  return s ? s->audio : NULL;
}

uint32_t bw_glue_state_agreements(BwGlueState *s, uint32_t accepted, uint32_t notices) {
  if (!s || s->agreements_configured || accepted > 31 || notices > 31 || (accepted & notices))
    return 1;
  s->agreements_accepted = accepted;
  s->agreements_notices = notices;
  s->agreements_configured = 1;
  return 0;
}

uint32_t bw_glue_state_offline_realm(BwGlueState *s, const char *name) {
  if (!s || !name || s->realm_configured || strlen(name) >= sizeof(s->realm_name))
    return 1;
  strcpy(s->realm_name, name);
  s->realm_configured = 1;
  return 0;
}

uint32_t bw_glue_state_screen_backend(BwGlueState *s, BwGlueScreenBarrier barrier, void *context) {
  if (!s || !barrier || s->screen_barrier || s->screen || s->screen_busy)
    return 1;
  s->screen_barrier = barrier;
  s->screen_context = context;
  return 0;
}
uint32_t bw_glue_state_screen(BwGlueState *s) {
  return s ? s->screen : 0;
}

uint32_t bw_glue_state_neutral_gamma(BwGlueState *s) {
  if (!s || s->neutral_gamma)
    return 1;
  s->neutral_gamma = 1;
  return 0;
}

/* Browser-owned unsupported post-effects switches, not captured native defaults.
 * The offline renderer has no glow/death postprocessing; keep controls honest. */
uint32_t bw_glue_state_renderer_profile(BwGlueState *s) {
  if (!s || !s->caps_configured || s->caps_flags)
    return 1;
  if (bw_glue_state_define_cvar(s, "gamma", "1", "1"))
    return 1;
  if (bw_glue_state_define_cvar(s, "ffxGlow", "0", "0"))
    return 1;
  return bw_glue_state_define_cvar(s, "ffxDeath", "0", "0");
}

uint32_t bw_glue_state_movie_profile(BwGlueState *s) {
  if (!s || s->movie_profile || s->screen)
    return 1;
  s->movie_profile = 1;
  return 0;
}

static Cvar *startup_cvar(BwGlueState *s, const char *name) {
  for (uint32_t i = 0; i < s->cvar_count; i++)
    if (!strcmp(s->cvars[i].name, name))
      return &s->cvars[i];
  return NULL;
}
uint32_t bw_glue_state_startup_profile(BwGlueState *s) {
  if (!s || s->screen || s->screen_busy || s->cvar_count > 254 || startup_cvar(s, "movie") ||
      startup_cvar(s, "expansionMovie"))
    return 1;
  if (bw_glue_state_define_cvar(s, "movie", "1", "1"))
    return 1;
  return bw_glue_state_define_cvar(s, "expansionMovie", "1", "1");
}
static uint32_t startup_integer(const char *v, long *out) {
  char *end;
  errno = 0;
  long n = strtol(v, &end, 10);
  if (errno || end == v || *end || n < INT_MIN || n > INT_MAX)
    return 1;
  *out = n;
  return 0;
}
uint32_t bw_glue_state_startup_screen(BwGlueState *s, uint32_t flag, uint32_t *out) {
  if (!s || !out || flag > 255 || s->screen || s->screen_busy)
    return 1;
  Cvar *movie = startup_cvar(s, "movie"), *expansion = startup_cvar(s, "expansionMovie");
  long m, e;
  if (!movie || !expansion || startup_integer(movie->value, &m) ||
      startup_integer(expansion->value, &e))
    return 1;
  if (flag == 1 && e == 1) {
    strcpy(movie->value, "0");
    strcpy(expansion->value, "0");
    *out = 2;
  } else if (m == 1) {
    strcpy(movie->value, "0");
    *out = 2;
  } else
    *out = 1;
  return 0;
}
uint32_t bw_glue_state_write_config(BwGlueState *s, char *out, uint32_t capacity,
                                    uint32_t *length) {
  if (length)
    *length = 0;
  if (!s || !length || (!out && capacity))
    return 1;
  uint32_t size = 0;
  for (uint32_t i = 0; i < s->cvar_count; i++) {
    Cvar *c = &s->cvars[i];
    if (!c->name[0])
      return 1;
    for (const unsigned char *p = (const unsigned char *)c->name; *p; p++)
      if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            *p == '_'))
        return 1;
    for (const unsigned char *p = (const unsigned char *)c->value; *p; p++)
      if (*p < 32 || *p == 127 || *p == '"' || *p == '\\')
        return 1;
    size += (uint32_t)(strlen(c->name) + strlen(c->value) + 8);
  }
  *length = size;
  if (!out)
    return 0;
  if (capacity <= size)
    return 1;
  char *p = out;
  for (uint32_t i = 0; i < s->cvar_count; i++) {
    Cvar *c = &s->cvars[i];
    size_t n = strlen(c->name), v = strlen(c->value);
    memcpy(p, "SET ", 4);
    p += 4;
    memcpy(p, c->name, n);
    p += n;
    memcpy(p, " \"", 2);
    p += 2;
    memcpy(p, c->value, v);
    p += v;
    memcpy(p, "\"\n", 2);
    p += 2;
  }
  *p = 0;
  return 0;
}

uint32_t bw_glue_state_agreement_profile(BwGlueState *s, uint32_t index, int32_t selector) {
  if (!s || index >= 5 || s->screen || s->screen_busy || (s->agreement_profiles & (1u << index)))
    return 1;
  uint32_t i;
  for (i = 0; i < s->cvar_count; i++)
    if (!strcmp(s->cvars[i].name, agreement_names[index]))
      break;
  if (i == s->cvar_count)
    return 1;
  s->agreement_selectors[index] = selector;
  s->agreement_profiles |= 1u << index;
  return 0;
}

uint32_t bw_glue_state_scan_profile(BwGlueState *s) {
  if (!s || s->scan_profile || s->screen || s->screen_busy)
    return 1;
  s->scan_profile = 1;
  return 0;
}
uint32_t bw_glue_state_scan_pending(BwGlueState *s) {
  return s && s->scan_profile && s->scan_state == 1;
}
uint32_t bw_glue_state_scan_unavailable(BwGlueState *s) {
  if (!s || !s->scan_profile || s->scan_state != 1 || s->scan_finished)
    return 1;
  s->scan_state = 2;
  return 0;
}

uint32_t bw_glue_state_system_profile(BwGlueState *s, uint32_t flags) {
  if (!s || s->system_profile || s->screen || s->screen_busy || flags > 255)
    return 1;
  s->system_profile = 1;
  s->system_flags = flags;
  return 0;
}

uint32_t bw_glue_state_warning_profile(BwGlueState *s) {
  if (!s || s->warning_profile || s->screen || s->screen_busy)
    return 1;
  s->warning_profile = 1;
  return 0;
}

uint32_t bw_glue_state_auth_enable(BwGlueState *s) {
  if (!s || s->auth_active)
    return 1;
  if (bw_glue_state_define_cvar(s, "realmName", "", ""))
    return 1;
  s->auth_enabled = 1;
  return 0;
}
uint32_t bw_glue_state_auth_request(BwGlueState *s) {
  return s && s->auth_active ? s->auth_generation : 0;
}
uint32_t bw_glue_state_auth_finish(BwGlueState *s, uint32_t generation) {
  if (!s || !s->auth_active || generation != s->auth_generation)
    return 1;
  s->auth_active = 0;
  clear_auth_credentials(s);
  return 0;
}

uint32_t bw_glue_state_auth_credentials(BwGlueState *s, uint8_t *output, uint32_t capacity) {
  if (!s || !s->auth_active || !output)
    return 0;
  uint32_t account = (uint32_t)strlen(s->auth_account),
           password = (uint32_t)strlen(s->auth_password);
  uint32_t length = 2 + account + password;
  if (capacity < length)
    return 0;
  output[0] = (uint8_t)account;
  output[1] = (uint8_t)password;
  memcpy(output + 2, s->auth_account, account);
  memcpy(output + 2 + account, s->auth_password, password);
  clear_auth_credentials(s);
  return length;
}

uint32_t bw_glue_state_realm_category(BwGlueState *s, uint32_t id, uint32_t flags,
                                      const char *label) {
  return s ? bw_realms_category(&s->realms, id, flags, label) : 1;
}
uint32_t bw_glue_state_realm_begin(BwGlueState *s) {
  if (!s || !s->auth_enabled)
    return 1;
  bw_realms_reset(&s->realms);
  s->realms_authenticated = 1;
  return 0;
}
uint32_t bw_glue_state_realm_add(BwGlueState *s, const BwRealm *row) {
  return s ? bw_realms_add(&s->realms, row) : 1;
}
uint32_t bw_glue_state_realm_finalize(BwGlueState *s) {
  if (!s)
    return 1;
  const char *preferred = "";
  for (uint32_t i = 0; i < s->cvar_count; i++)
    if (!strcmp(s->cvars[i].name, "realmName"))
      preferred = s->cvars[i].value;
  bw_realms_finalize(&s->realms, preferred);
  return 0;
}
uint32_t bw_glue_state_realm_action(BwGlueState *s) {
  if (!s)
    return 0;
  if (s->realms.suggestion) {
    uint32_t suggestion = s->realms.suggestion;
    s->realms.suggestion = 0;
    if (suggestion == 2)
      return 5;
    if (s->realms.selected && s->realms.selected <= s->realms.count) {
      const char *name = s->realms.rows[s->realms.selected - 1].name;
      for (uint32_t i = 0; i < s->cvar_count; i++)
        if (!strcmp(s->cvars[i].name, "realmName"))
          strcpy(s->cvars[i].value, name);
      return 4;
    }
  }
  if (s->realms.cancelled) {
    s->realms.cancelled = 0;
    return 2;
  }
  if (s->realms.change) {
    s->realms.change = 0;
    const char *name = s->realms.rows[s->realms.selected - 1].name;
    for (uint32_t i = 0; i < s->cvar_count; i++)
      if (!strcmp(s->cvars[i].name, "realmName"))
        strcpy(s->cvars[i].value, name);
    return 1;
  }
  if (s->realms.refresh) {
    s->realms.refresh = 0;
    return 3;
  }
  return 0;
}

uint32_t bw_glue_state_world_begin(BwGlueState *s) {
  if (!s || !s->auth_enabled || s->auth_active || s->world_active)
    return 1;
  s->world_active = 1;
  return 0;
}
uint32_t bw_glue_state_world_busy(BwGlueState *s) {
  return s ? s->world_active : 0;
}
void bw_glue_state_world_finish(BwGlueState *s) {
  if (s)
    s->world_active = 0;
}

uint32_t bw_glue_state_characters_received(BwGlueState *s, uint32_t count) {
  if (!s || !s->world_active || count > 50)
    return 1;
  s->character_count = count;
  return 0;
}
BwCharacters *bw_glue_state_characters(BwGlueState *s) {
  return s ? s->characters : NULL;
}
uint32_t bw_glue_state_character_connected(BwGlueState *s, uint32_t connected) {
  if (!s || bw_characters_connected(s->characters, connected))
    return 1;
  s->character_count = connected ? bw_characters_metric(s->characters, 1) : 0;
  return 0;
}
