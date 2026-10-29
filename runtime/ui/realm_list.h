#ifndef BRWOWSER_REALM_LIST_H
#define BRWOWSER_REALM_LIST_H
#include "lua.h"
#include <stdint.h>
#define BW_REALM_LIMIT 256
#define BW_REALM_CATEGORY_LIMIT 64
typedef struct {
  char name[256], address[256];
  uint32_t id, type, flags, locked, characters, timezone, major, minor, revision, build;
  float population;
} BwRealm;
typedef struct {
  uint32_t id, flags;
  char label[128];
} BwRealmCategory;
typedef struct {
  BwRealm rows[BW_REALM_LIMIT];
  BwRealmCategory categories[BW_REALM_CATEGORY_LIMIT];
  uint32_t count, category_count, active[BW_REALM_CATEGORY_LIMIT], active_count;
  uint32_t order[BW_REALM_LIMIT], selected, change, refresh, cancelled;
  uint32_t suggestion;
  uint32_t sort_column, reverse;
  float mean, deviation;
} BwRealmList;
uint32_t bw_realms_category(BwRealmList *, uint32_t id, uint32_t flags, const char *label);
void bw_realms_reset(BwRealmList *);
uint32_t bw_realms_add(BwRealmList *, const BwRealm *);
void bw_realms_finalize(BwRealmList *, const char *preferred);
uint32_t bw_realms_suggest(BwRealmList *, uint32_t category_index, int pvp, int roleplay);
int bw_realms_lua(lua_State *, BwRealmList *, uint32_t operation);
int bw_realms_server(lua_State *, const BwRealmList *);
#endif
