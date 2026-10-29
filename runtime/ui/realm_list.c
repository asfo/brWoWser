/* Original RealmList.lua presentation backend. Native static references and limits
 * are recorded in docs/realm-selection.md; no world connection is fabricated. */
#include "realm_list.h"
#include "lauxlib.h"
#include <math.h>
#include <string.h>
static int folded_compare(const char *a, const char *b) {
  while (*a && *b) {
    unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (x != y)
      return x < y ? -1 : 1;
  }
  return (unsigned char)*a - (unsigned char)*b;
}
uint32_t bw_realms_category(BwRealmList *r, uint32_t id, uint32_t flags, const char *label) {
  if (!r || !label || strlen(label) >= 128)
    return 1;
  for (uint32_t i = 0; i < r->category_count; i++)
    if (r->categories[i].id == id)
      return 1;
  if (r->category_count >= BW_REALM_CATEGORY_LIMIT)
    return 1;
  BwRealmCategory *c = &r->categories[r->category_count++];
  c->id = id;
  c->flags = flags;
  strcpy(c->label, label);
  return 0;
}
void bw_realms_reset(BwRealmList *r) {
  r->count = r->active_count = r->selected = r->change = r->refresh = r->cancelled = r->suggestion =
      0;
}
uint32_t bw_realms_add(BwRealmList *r, const BwRealm *row) {
  if (!r || !row || r->count >= BW_REALM_LIMIT || !memchr(row->name, 0, 256) ||
      !memchr(row->address, 0, 256) || !row->name[0] || !isfinite(row->population) ||
      row->id > 255 || row->type > 255 || row->flags > 255 || row->timezone > 255 ||
      row->characters > 255 || row->locked > 1)
    return 1;
  for (uint32_t i = 0; i < r->count; i++)
    if (r->rows[i].id == row->id)
      return 1;
  r->rows[r->count++] = *row;
  return 0;
}
static double load(const BwRealmList *r, const BwRealm *row) {
  if (row->flags & 128)
    return 2;
  if (row->flags & 32)
    return -3;
  if (row->flags & 64)
    return -2;
  if (row->population < r->mean - r->deviation)
    return -1;
  if (row->population > r->mean + r->deviation)
    return 1;
  return 0;
}
static int compare(const BwRealmList *r, uint32_t a, uint32_t b) {
  const BwRealm *x = &r->rows[a], *y = &r->rows[b];
  double left = 0, right = 0;
  if (r->sort_column == 1) {
    left = x->type;
    right = y->type;
  } else if (r->sort_column == 2) {
    left = x->characters;
    right = y->characters;
  } else if (r->sort_column == 3) {
    left = load(r, x);
    right = load(r, y);
  }
  int c = left < right ? -1 : left > right ? 1 : folded_compare(x->name, y->name);
  return r->reverse ? -c : c;
}
static void sort(BwRealmList *r) {
  for (uint32_t i = 0; i < r->count; i++)
    r->order[i] = i;
  for (uint32_t i = 1; i < r->count; i++) {
    uint32_t x = r->order[i], j = i;
    while (j && compare(r, x, r->order[j - 1]) < 0) {
      r->order[j] = r->order[j - 1];
      j--;
    }
    r->order[j] = x;
  }
}
void bw_realms_finalize(BwRealmList *r, const char *preferred) {
  r->active_count = 0;
  r->selected = 0;
  r->mean = 0;
  r->deviation = 0;
  for (uint32_t i = 0; i < r->count; i++) {
    r->mean += r->rows[i].population;
    if (preferred && !folded_compare(preferred, r->rows[i].name) && !(r->rows[i].flags & 1))
      r->selected = i + 1;
    int found = 0;
    for (uint32_t c = 0; c < r->category_count; c++)
      if (r->categories[c].id == r->rows[i].timezone)
        found = 1;
    if (!found)
      bw_realms_category(r, r->rows[i].timezone, 0, "UNKNOWN");
  }
  if (r->count > 1) {
    r->mean /= r->count;
    float variance = 0;
    for (uint32_t i = 0; i < r->count; i++) {
      float d = r->rows[i].population - r->mean;
      variance += d * d;
    }
    r->deviation = (float)(sqrt(variance / (r->count - 1)) * .6f);
  } else
    r->mean = 1;
  for (uint32_t c = 0; c < r->category_count; c++)
    for (uint32_t i = 0; i < r->count; i++)
      if (r->rows[i].timezone == r->categories[c].id) {
        r->active[r->active_count++] = c;
        break;
      }
  sort(r);
  /* Fresh native path: one populated category calls SetPreferredInfo(1).
   * A recommended candidate bypasses the default non-PvP/non-RP type filter.
   * Full matching realms are fallback candidates; unavailable flags are skipped. */
  if (!r->selected && (!preferred || !preferred[0]) && r->active_count == 1) {
    uint32_t fallback = 0;
    for (uint32_t i = 0; i < r->count; i++) {
      uint32_t index = r->order[i];
      const BwRealm *row = &r->rows[index];
      if (row->flags & 3)
        continue;
      if (row->flags & 32) {
        r->selected = index + 1;
        break;
      }
      if (row->type == 0) {
        if (row->flags & 128)
          fallback = index + 1;
        else {
          r->selected = index + 1;
          break;
        }
      }
    }
    if (!r->selected)
      r->selected = fallback;
  }
}
uint32_t bw_realms_suggest(BwRealmList *r, uint32_t category_index, int pvp, int roleplay) {
  if (!r || !category_index || category_index > r->active_count)
    return 1;
  uint32_t timezone = r->categories[r->active[category_index - 1]].id,
           wanted_type = pvp ? (roleplay ? 8 : 1) : (roleplay ? 6 : 0);
  uint32_t recommended = 0, matching = 0, full = 0;
  for (uint32_t i = 0; i < r->count; i++) {
    uint32_t index = r->order[i];
    const BwRealm *row = &r->rows[index];
    if (row->timezone != timezone || (row->flags & 3) || row->locked)
      continue;
    if (row->flags & 32) {
      recommended = index + 1;
      break;
    }
    if (row->type != wanted_type)
      continue;
    if (row->flags & 128) {
      if (!full)
        full = index + 1;
    } else {
      matching = index + 1;
      break;
    }
  }
  r->suggestion = 1;
  r->selected = recommended ? recommended : matching ? matching : full;
  if (!r->selected)
    r->suggestion = 2;
  return 0;
}
static uint32_t category(const BwRealmList *r, uint32_t index) {
  return index && index <= r->active_count ? r->categories[r->active[index - 1]].id : UINT32_MAX;
}
static uint32_t row_index(lua_State *L, const BwRealmList *r) {
  uint32_t c = (uint32_t)luaL_checkinteger(L, 1), at = (uint32_t)luaL_checkinteger(L, 2),
           zone = category(r, c);
  for (uint32_t i = 0; i < r->count; i++) {
    uint32_t n = r->order[i];
    if (r->rows[n].timezone == zone && at && !--at)
      return n + 1;
  }
  return 0;
}
static void flag(lua_State *L, int value) {
  if (value)
    lua_pushnumber(L, 1);
  else
    lua_pushnil(L);
}
/* operations: count, info, categories, rate, category, locale, tournament,
 * invalid tournament, cancel query, request, cancelled, change, sort. */
int bw_realms_lua(lua_State *L, BwRealmList *r, uint32_t op) {
  if (op == 0) {
    uint32_t zone = category(r, (uint32_t)luaL_checkinteger(L, 1)), n = 0;
    for (uint32_t i = 0; i < r->count; i++)
      n += r->rows[i].timezone == zone;
    lua_pushnumber(L, n);
    return 1;
  }
  if (op == 1) {
    uint32_t index = row_index(L, r);
    if (!index) {
      lua_pushnil(L);
      return 1;
    }
    const BwRealm *x = &r->rows[index - 1];
    lua_pushstring(L, x->name);
    lua_pushnumber(L, x->characters);
    flag(L, x->flags & 1);
    flag(L, x->flags & 2);
    flag(L, index == r->selected);
    flag(L, x->type == 1 || x->type == 8);
    flag(L, x->type == 6 || x->type == 8);
    lua_pushnumber(L, load(r, x));
    flag(L, x->locked);
    if (x->flags & 4) {
      lua_pushnumber(L, x->major);
      lua_pushnumber(L, x->minor);
      lua_pushnumber(L, x->revision);
      lua_pushnumber(L, x->build);
      lua_pushnumber(L, x->type);
    } else
      for (uint32_t i = 0; i < 5; i++)
        lua_pushnil(L);
    return 14;
  }
  if (op == 2) {
    if (!lua_checkstack(L, (int)r->active_count + 1))
      return luaL_error(L, "realm category stack capacity");
    for (uint32_t c = 0; c < r->active_count; c++)
      lua_pushstring(L, r->categories[r->active[c]].label);
    if (!r->active_count) {
      lua_pushliteral(L, "UNKNOWN");
      return 1;
    }
    return (int)r->active_count;
  }
  if (op == 3) {
    lua_pushnumber(L, 30);
    return 1;
  }
  if (op == 4) {
    uint32_t c = 1;
    if (r->selected)
      for (uint32_t i = 0; i < r->active_count; i++)
        if (category(r, i + 1) == r->rows[r->selected - 1].timezone)
          c = i + 1;
    lua_pushnumber(L, c);
    return 1;
  }
  if (op == 5 || op == 7) {
    lua_pushnil(L);
    return 1;
  }
  if (op == 6) {
    uint32_t c = (uint32_t)luaL_checkinteger(L, 1);
    flag(L, c && c <= r->active_count && (r->categories[r->active[c - 1]].flags & 1));
    return 1;
  }
  if (op == 8) {
    r->refresh = 0;
    return 0;
  }
  if (op == 9) {
    r->refresh = 1;
    return 0;
  }
  if (op == 10) {
    r->cancelled = 1;
    return 0;
  }
  if (op == 11) {
    uint32_t index = row_index(L, r);
    if (!index || (r->rows[index - 1].flags & 3) || r->rows[index - 1].locked)
      return luaL_error(L, "selected realm is unavailable");
    r->selected = index;
    r->change = index;
    return 0;
  }
  if (op == 12) {
    const char *field = luaL_checkstring(L, 1);
    uint32_t col = !strcmp(field, "name")         ? 0
                   : !strcmp(field, "mode")       ? 1
                   : !strcmp(field, "characters") ? 2
                   : !strcmp(field, "load")       ? 3
                                                  : UINT32_MAX;
    if (col == UINT32_MAX)
      return luaL_error(L, "invalid realm sort field");
    r->reverse = col == r->sort_column ? !r->reverse : 0;
    r->sort_column = col;
    sort(r);
    return 0;
  }
  return luaL_error(L, "unknown realm operation");
}
int bw_realms_server(lua_State *L, const BwRealmList *r) {
  if (!r->selected || r->selected > r->count) {
    lua_pushliteral(L, "");
    lua_pushnil(L);
    lua_pushnil(L);
    flag(L, !r->count);
    return 4;
  }
  const BwRealm *x = &r->rows[r->selected - 1];
  lua_pushstring(L, x->name);
  flag(L, x->type == 1 || x->type == 8);
  flag(L, x->type == 6 || x->type == 8);
  flag(L, x->flags & 2);
  return 4;
}
