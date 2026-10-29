/* Original Glue character APIs over imported server records and owner DBC rows.
 * No server success, inventory, appearance range or model result is fabricated. */
#include "characters.h"
#include "lauxlib.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define SECTION_LIMIT 32768
typedef struct {
  uint32_t id, faction, roles[3];
  char name[128], token[32], hair[32], facial[32], facial_female[32], background[32];
} Choice;
typedef struct {
  uint32_t race, gender, type, variation, color, flags;
} Section;
typedef struct {
  uint32_t guid[2], race, class_id, gender, level, flags, customization;
  uint32_t appearance[5];
  char name[49], zone[256];
} Character;
typedef struct {
  uint32_t race, gender;
  char name[49];
} Name;
struct BwCharacters {
  lua_State *L;
  BwCharacters **handle;
  int reference;
  Choice races[10], classes[10];
  uint32_t race_count, class_count, combinations[10][10];
  Section *sections, *facial;
  uint32_t section_capacity, facial_capacity;
  Name *names;
  uint32_t name_count, name_capacity;
  uint32_t section_count, facial_count;
  Character rows[50];
  uint32_t count, connected, selected, race, class_index, gender, appearance[5];
  uint32_t revision, actions, creating, random;
  double facing[2];
  char create_name[49];
  uint8_t create_fields[9];
};
static uint32_t word(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int read_string(const uint8_t **at, const uint8_t *end, char *out, size_t capacity) {
  const uint8_t *nul = memchr(*at, 0, (size_t)(end - *at));
  if (!nul || (size_t)(nul - *at) >= capacity)
    return 1;
  memcpy(out, *at, (size_t)(nul - *at));
  out[nul - *at] = 0;
  *at = nul + 1;
  return 0;
}
static int choice_index(Choice *rows, uint32_t count, uint32_t id) {
  for (uint32_t i = 0; i < count; i++)
    if (rows[i].id == id)
      return (int)i;
  return -1;
}
static int background_race(BwCharacters *s, uint32_t id) {
  /* Native selection/create callbacks share the dwarf and orc scenes. */
  return choice_index(s->races, s->race_count, id == 7 ? 3 : id == 8 ? 2 : id);
}
static int valid(const BwCharacters *s, uint32_t race, uint32_t class_index) {
  return race && race <= s->race_count && class_index && class_index <= s->class_count &&
         s->combinations[race - 1][class_index - 1];
}
static uint32_t random_next(BwCharacters *s) {
  uint32_t x = s->random;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return s->random = x;
}
static void *reserve_rows(void *rows, uint32_t *capacity, uint32_t needed, size_t row_size) {
  if (!capacity || needed > SECTION_LIMIT || !row_size)
    return NULL;
  if (needed <= *capacity)
    return rows;
  uint32_t next = *capacity ? *capacity : 64;
  while (next < needed) {
    if (next > SECTION_LIMIT / 2) {
      next = SECTION_LIMIT;
      break;
    }
    next *= 2;
  }
  if ((size_t)next > SIZE_MAX / row_size)
    return NULL;
  void *grown = realloc(rows, (size_t)next * row_size);
  if (!grown)
    return NULL;
  *capacity = next;
  return grown;
}
static uint32_t option_values(BwCharacters *s, uint32_t field, uint32_t values[256]) {
  uint8_t seen[256] = {0};
  uint32_t count = 0;
  if (!s->race || s->race > s->race_count)
    return 0;
  uint32_t race = s->races[s->race - 1].id;
  Section *rows = field == 4 ? s->facial : s->sections;
  uint32_t length = field == 4 ? s->facial_count : s->section_count;
  for (uint32_t i = 0; i < length; i++) {
    Section *row = &rows[i];
    if (row->race != race || row->gender != s->gender)
      continue;
    if ((row->flags & 4) &&
        (!s->class_index || strcmp(s->classes[s->class_index - 1].token, "DEATHKNIGHT")))
      continue;
    uint32_t value;
    if (field == 0 && row->type == 0)
      value = row->color;
    else if (field == 1 && row->type == 1 && row->color == s->appearance[0])
      value = row->variation;
    else if (field == 2 && row->type == 3)
      value = row->variation;
    else if (field == 3 && row->type == 3 && row->variation == s->appearance[2])
      value = row->color;
    else if (field == 4)
      value = row->variation;
    else
      continue;
    if (value > 255 || seen[value])
      continue;
    seen[value] = 1;
  }
  for (uint32_t i = 0; i < 256; i++)
    if (seen[i])
      values[count++] = i;
  return count;
}
static void fix_appearance(BwCharacters *s) {
  for (uint32_t field = 0; field < 5; field++) {
    uint32_t values[256], count = option_values(s, field, values), found = 0;
    for (uint32_t i = 0; i < count; i++)
      found |= values[i] == s->appearance[field];
    if (!found && count)
      s->appearance[field] = values[0];
  }
  s->revision++;
}
BwCharacters *bw_characters_new(lua_State *L) {
  BwCharacters *s = calloc(1, sizeof(*s));
  if (!s)
    return NULL;
  s->L = L;
  s->handle = lua_newuserdata(L, sizeof(*s->handle));
  *s->handle = s;
  s->reference = luaL_ref(L, LUA_REGISTRYINDEX);
  return s;
}
void bw_characters_free(BwCharacters *s) {
  if (!s)
    return;
  *s->handle = NULL;
  luaL_unref(s->L, LUA_REGISTRYINDEX, s->reference);
  free(s->sections);
  free(s->facial);
  free(s->names);
  free(s);
}
uint32_t bw_characters_import(BwCharacters *s, uint32_t kind, const uint8_t *bytes,
                              uint32_t length) {
  if (!s || !bytes || length > 1024)
    return 1;
  const uint8_t *at, *end = bytes + length;
  if (kind == 1 || kind == 2) {
    if (length < 25 || s->connected)
      return 1;
    Choice c = {0};
    c.id = word(bytes);
    c.faction = word(bytes + 4);
    for (uint32_t i = 0; i < 3; i++)
      c.roles[i] = word(bytes + 8 + i * 4);
    at = bytes + 20;
    if (!c.id || c.id > 255 || c.faction > 1 || read_string(&at, end, c.name, sizeof(c.name)) ||
        read_string(&at, end, c.token, sizeof(c.token)) ||
        read_string(&at, end, c.hair, sizeof(c.hair)) ||
        read_string(&at, end, c.facial, sizeof(c.facial)) ||
        read_string(&at, end, c.facial_female, sizeof(c.facial_female)) || !c.name[0] ||
        !c.token[0])
      return 1;
    if (at == end)
      strcpy(c.background, c.token); /* Older bounded metadata records. */
    else if (read_string(&at, end, c.background, sizeof(c.background)) || at != end ||
             !c.background[0])
      return 1;
    for (const char *p = c.background; *p; p++)
      if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
            *p == '_'))
        return 1;
    Choice *rows = kind == 1 ? s->races : s->classes;
    uint32_t *count = kind == 1 ? &s->race_count : &s->class_count;
    if (*count >= 10 || choice_index(rows, *count, c.id) >= 0)
      return 1;
    rows[(*count)++] = c;
    return 0;
  }
  if (kind == 3) {
    if (length != 8 || s->connected)
      return 1;
    int race = choice_index(s->races, s->race_count, word(bytes));
    int class_index = choice_index(s->classes, s->class_count, word(bytes + 4));
    if (race < 0 || class_index < 0)
      return 1;
    s->combinations[race][class_index] = 1;
    return 0;
  }
  if (kind == 4 || kind == 5) {
    if (length != (kind == 4 ? 24u : 20u) || s->connected)
      return 1;
    Section row = {word(bytes),      word(bytes + 4),  word(bytes + 8),
                   word(bytes + 12), word(bytes + 16), kind == 4 ? word(bytes + 20) : 0};
    if (choice_index(s->races, s->race_count, row.race) < 0 || row.gender > 1 || row.type > 4 ||
        row.variation > 255 || row.color > 255)
      return 1;
    Section **rows = kind == 4 ? &s->sections : &s->facial;
    uint32_t *count = kind == 4 ? &s->section_count : &s->facial_count;
    uint32_t *capacity = kind == 4 ? &s->section_capacity : &s->facial_capacity;
    if (*count >= SECTION_LIMIT)
      return 1;
    Section *grown = reserve_rows(*rows, capacity, *count + 1, sizeof(**rows));
    if (!grown)
      return 1;
    *rows = grown;
    (*rows)[(*count)++] = row;
    return 0;
  }
  if (kind == 6) {
    if (length < 54 || s->count >= 50)
      return 1;
    Character row = {0};
    uint32_t *fields[] = {&row.guid[0],       &row.guid[1],       &row.race,
                          &row.class_id,      &row.gender,        &row.level,
                          &row.flags,         &row.customization, &row.appearance[0],
                          &row.appearance[1], &row.appearance[2], &row.appearance[3],
                          &row.appearance[4]};
    for (uint32_t i = 0; i < 13; i++)
      *fields[i] = word(bytes + i * 4);
    at = bytes + 52;
    if ((!row.guid[0] && !row.guid[1]) || row.gender > 1 || row.level > 255 ||
        choice_index(s->races, s->race_count, row.race) < 0 ||
        choice_index(s->classes, s->class_count, row.class_id) < 0 ||
        read_string(&at, end, row.name, sizeof(row.name)) ||
        read_string(&at, end, row.zone, sizeof(row.zone)) || at != end || !row.name[0])
      return 1;
    for (uint32_t i = 0; i < 5; i++)
      if (row.appearance[i] > 255)
        return 1;
    for (uint32_t i = 0; i < s->count; i++)
      if (!memcmp(s->rows[i].guid, row.guid, sizeof(row.guid)))
        return 1;
    s->rows[s->count++] = row;
    return 0;
  }
  if (kind == 7) {
    if (length != 4 || !word(bytes))
      return 1;
    s->random = word(bytes);
    return 0;
  }
  if (kind == 8) {
    if (length < 10 || s->connected || s->name_count >= SECTION_LIMIT)
      return 1;
    Name row = {word(bytes), word(bytes + 4), {0}};
    at = bytes + 8;
    if (choice_index(s->races, s->race_count, row.race) < 0 || row.gender > 1 ||
        read_string(&at, end, row.name, sizeof(row.name)) || at != end || !row.name[0])
      return 1;
    Name *grown = reserve_rows(s->names, &s->name_capacity, s->name_count + 1, sizeof(*s->names));
    if (!grown)
      return 1;
    s->names = grown;
    s->names[s->name_count++] = row;
    return 0;
  }
  return 1;
}
uint32_t bw_characters_roster_begin(BwCharacters *s) {
  if (!s || !s->race_count || !s->class_count)
    return 1;
  memset(s->rows, 0, sizeof(s->rows));
  s->count = s->selected = 0;
  s->revision++;
  return 0;
}
uint32_t bw_characters_connected(BwCharacters *s, uint32_t connected) {
  if (!s || connected > 1)
    return 1;
  s->connected = connected;
  if (!connected) {
    s->creating = s->actions = 0;
    memset(s->create_name, 0, sizeof(s->create_name));
  }
  return 0;
}
static BwCharacters *current(lua_State *L) {
  BwCharacters **p = lua_touserdata(L, lua_upvalueindex(1));
  if (!p || !*p)
    luaL_error(L, "closed character state");
  return *p;
}
static void flag(lua_State *L, uint32_t v) {
  if (v)
    lua_pushnumber(L, 1);
  else
    lua_pushnil(L);
}
static int call(lua_State *L) {
  BwCharacters *s = current(L);
  uint32_t op = (uint32_t)lua_tointeger(L, lua_upvalueindex(2));
  if (op == 0) {
    uint32_t index = (uint32_t)luaL_checkinteger(L, 1);
    if (!index || index > s->count) {
      lua_pushnil(L);
      return 1;
    }
    Character *row = &s->rows[index - 1];
    int race = choice_index(s->races, s->race_count, row->race);
    int cls = choice_index(s->classes, s->class_count, row->class_id);
    lua_pushstring(L, row->name);
    lua_pushstring(L, s->races[race].name);
    lua_pushstring(L, s->classes[cls].name);
    lua_pushnumber(L, row->level);
    lua_pushstring(L, row->zone);
    lua_pushnumber(L, row->gender + 2);
    flag(L, row->flags & 0x2000);
    flag(L, row->customization & 1);
    flag(L, row->customization & 0x100000);
    flag(L, row->customization & 0x10000);
    return 10;
  }
  if (op == 1) {
    flag(L, s->connected);
    return 1;
  }
  if (op == 2) {
    uint32_t index = (uint32_t)luaL_checkinteger(L, 1);
    if (!s->connected || index > s->count)
      return luaL_error(L, "character selection unavailable");
    s->selected = index;
    s->actions |= 1;
    s->revision++;
    return 0;
  }
  if (op == 3 || op == 20) {
    uint32_t race = s->race ? s->race : 1;
    if (op == 3) {
      uint32_t index = (uint32_t)luaL_checkinteger(L, 1);
      if (index > s->count)
        return luaL_error(L, "invalid character index");
      if (!index) {
        /* Normal nonstreaming native fallback: ChrRaces row 2, not the
         * special-mode CharacterSelect scene (0x4e36fd..0x4e3724). */
        int fallback = background_race(s, 2);
        if (fallback < 0)
          return luaL_error(L, "empty character background metadata unavailable");
        lua_pushstring(L, s->races[fallback].background);
        return 1;
      }
      race = (uint32_t)background_race(s, s->rows[index - 1].race) + 1;
    } else if (race && race <= s->race_count) {
      race = (uint32_t)background_race(s, s->races[race - 1].id) + 1;
    }
    if (!race || race > s->race_count)
      return luaL_error(L, "character metadata unavailable");
    int cls = s->class_index ? (int)s->class_index - 1 : -1;
    if (op == 3) {
      uint32_t index = (uint32_t)lua_tointeger(L, 1);
      if (index)
        cls = choice_index(s->classes, s->class_count, s->rows[index - 1].class_id);
    }
    lua_pushstring(L, cls >= 0 && !strcmp(s->classes[cls].token, "DEATHKNIGHT")
                          ? s->classes[cls].background
                          : s->races[race - 1].background);
    return 1;
  }
  if (op == 4 || op == 5 || op == 6) {
    if (op == 5)
      s->actions |= 2;
    if (op == 6)
      s->actions |= 8;
    return 0;
  }
  if (op == 7) {
    lua_pushnumber(L, 0);
    return 1;
  } /* No browser addons admitted. */
  if (op == 8) {
    s->actions |= 4;
    return 0;
  }
  if (op == 9)
    return luaL_error(L, "Entering the game world is not implemented yet");
  if (op == 10 || op == 11 || op == 12 || op == 13) {
    uint32_t mode = op >= 12;
    if (op & 1) {
      double value = luaL_checknumber(L, 1);
      if (!isfinite(value))
        return luaL_error(L, "invalid character facing");
      s->facing[mode] = fmod(value, 360.0);
      s->revision++;
      return 0;
    }
    lua_pushnumber(L, s->facing[mode]);
    return 1;
  }
  if (op == 14) {
    if (s->creating)
      return 0;
    if (!s->race_count || !s->class_count)
      return luaL_error(L, "character metadata unavailable");
    if (!s->random)
      return luaL_error(L, "character randomness is not configured");
    s->race = random_next(s) % s->race_count + 1;
    uint32_t classes[10], count = 0;
    for (uint32_t i = 1; i <= s->class_count; i++)
      if (valid(s, s->race, i))
        classes[count++] = i;
    if (!count)
      return luaL_error(L, "selected race has no available class");
    s->class_index = classes[random_next(s) % count];
    s->gender = random_next(s) % 2;
    memset(s->appearance, 0, sizeof(s->appearance));
    for (uint32_t field = 0; field < 5; field++) {
      uint32_t values[256], options = option_values(s, field, values);
      if (options)
        s->appearance[field] = values[random_next(s) % options];
    }
    fix_appearance(s);
    return 0;
  }
  if (op == 15 || op == 16) {
    uint32_t count = op == 15 ? s->race_count : s->class_count;
    Choice *rows = op == 15 ? s->races : s->classes;
    if (!lua_checkstack(L, (int)count * 3))
      return luaL_error(L, "character choice stack capacity");
    for (uint32_t i = 0; i < count; i++) {
      lua_pushstring(L, rows[i].name);
      lua_pushstring(L, rows[i].token);
      lua_pushnumber(L, op == 15 || valid(s, s->race, i + 1) ? 1 : 0);
    }
    return (int)count * 3;
  }
  if (op == 17 || op == 18 || op == 19) {
    if (op == 17) {
      lua_pushnumber(L, s->race);
      return 1;
    }
    if (op == 19) {
      lua_pushnumber(L, s->gender + 2);
      return 1;
    }
    if (!s->class_index || s->class_index > s->class_count)
      return luaL_error(L, "class not selected");
    Choice *row = &s->classes[s->class_index - 1];
    lua_pushstring(L, row->name);
    lua_pushstring(L, row->token);
    lua_pushnumber(L, s->class_index);
    for (uint32_t i = 0; i < 3; i++)
      flag(L, row->roles[i]);
    return 6;
  }
  if (op == 21 || op == 22 || op == 23) {
    if (s->creating)
      return 0;
    uint32_t value = (uint32_t)luaL_checkinteger(L, 1);
    if (op == 21) {
      if (!value || value > s->race_count)
        return luaL_error(L, "invalid race index");
      s->race = value;
      if (!valid(s, s->race, s->class_index)) {
        s->class_index = 1;
        while (!valid(s, s->race, s->class_index) && s->class_index < s->class_count)
          s->class_index++;
      }
    } else if (op == 22) {
      if (!valid(s, s->race, value))
        return luaL_error(L, "invalid race/class combination");
      s->class_index = value;
    } else {
      if (value != 2 && value != 3)
        return luaL_error(L, "invalid character sex");
      s->gender = value - 2;
    }
    fix_appearance(s);
    return 0;
  }
  if (op == 24) {
    flag(L, valid(s, (uint32_t)luaL_checkinteger(L, 1), (uint32_t)luaL_checkinteger(L, 2)));
    return 1;
  }
  if (op == 25 || op == 26 || op == 27 || op == 28) {
    uint32_t race = op == 25 ? (uint32_t)luaL_checkinteger(L, 1) : s->race;
    if (!race || race > s->race_count)
      return luaL_error(L, "invalid race index");
    Choice *row = &s->races[race - 1];
    if (op == 25) {
      lua_pushstring(L, row->faction ? "Horde" : "Alliance");
      lua_pushstring(L, row->faction ? "Horde" : "Alliance");
      return 2;
    }
    if (op == 26) {
      lua_pushstring(L, row->name);
      lua_pushstring(L, row->token);
      return 2;
    }
    lua_pushstring(L, op == 27 ? row->hair : s->gender ? row->facial_female : row->facial);
    return 1;
  }
  if (op == 29 || op == 30) {
    if (s->creating)
      return 0;
    if (op == 29) {
      uint32_t field = (uint32_t)luaL_checkinteger(L, 1);
      int direction = (int)luaL_checkinteger(L, 2);
      if (!field || field > 5 || (direction != -1 && direction != 1))
        return luaL_error(L, "invalid customization cycle");
      uint32_t values[256], count = option_values(s, field - 1, values), current = 0;
      if (!count)
        return luaL_error(L, "appearance options unavailable in client data");
      for (uint32_t i = 0; i < count; i++)
        if (values[i] == s->appearance[field - 1])
          current = i;
      s->appearance[field - 1] = values[(current + count + direction) % count];
    } else {
      if (!s->random)
        return luaL_error(L, "character randomness is not configured");
      for (uint32_t field = 0; field < 5; field++) {
        uint32_t values[256], count = option_values(s, field, values);
        if (count)
          s->appearance[field] = values[random_next(s) % count];
      }
    }
    fix_appearance(s);
    return 0;
  }
  if (op == 31 || op == 32)
    return 0; /* Renderer consumes the current snapshot. */
  if (op == 33) {
    size_t length;
    const char *name = luaL_checklstring(L, 1, &length);
    if (!s->connected || s->creating || !valid(s, s->race, s->class_index))
      return luaL_error(L, "character creation unavailable");
    if (length > 48 || memchr(name, 0, length))
      return luaL_error(L, "invalid character name length");
    for (uint32_t field = 0; field < 5; field++) {
      uint32_t values[256], count = option_values(s, field, values), found = 0;
      for (uint32_t i = 0; i < count; i++)
        found |= values[i] == s->appearance[field];
      if (!found && field != 4)
        return luaL_error(L, "appearance selection is not available in client data");
    }
    memcpy(s->create_name, name, length);
    s->create_name[length] = 0;
    s->create_fields[0] = (uint8_t)s->races[s->race - 1].id;
    s->create_fields[1] = (uint8_t)s->classes[s->class_index - 1].id;
    s->create_fields[2] = (uint8_t)s->gender;
    for (uint32_t i = 0; i < 5; i++)
      s->create_fields[3 + i] = (uint8_t)s->appearance[i];
    s->create_fields[8] = 0;
    s->creating = 1;
    s->actions |= 16;
    return 0;
  }
  if (op == 34) {
    if (!s->race || s->race > s->race_count || !s->random)
      return luaL_error(L, "character metadata unavailable");
    uint32_t race = s->races[s->race - 1].id, count = 0;
    for (uint32_t i = 0; i < s->name_count; i++)
      count += s->names[i].race == race && s->names[i].gender == s->gender;
    if (!count)
      return luaL_error(L, "local random names are unavailable for this race");
    uint32_t selected = random_next(s) % count;
    for (uint32_t i = 0; i < s->name_count; i++)
      if (s->names[i].race == race && s->names[i].gender == s->gender) {
        if (!selected--) {
          lua_pushstring(L, s->names[i].name);
          return 1;
        }
      }
  }
  return luaL_error(L, "unsupported character operation");
}
uint32_t bw_characters_register(BwCharacters *s) {
  if (!s)
    return 1;
  const char *names[] = {"GetCharacterInfo",
                         "IsConnectedToServer",
                         "SelectCharacter",
                         "GetSelectBackgroundModel",
                         "ReadyForAccountDataTimes",
                         "GetCharacterListUpdate",
                         "RequestRealmSplitInfo",
                         "GetNumAddOns",
                         "DisconnectFromServer",
                         "EnterWorld",
                         "GetCharacterSelectFacing",
                         "SetCharacterSelectFacing",
                         "GetCharacterCreateFacing",
                         "SetCharacterCreateFacing",
                         "ResetCharCustomize",
                         "GetAvailableRaces",
                         "GetAvailableClasses",
                         "GetSelectedRace",
                         "GetSelectedClass",
                         "GetSelectedSex",
                         "GetCreateBackgroundModel",
                         "SetSelectedRace",
                         "SetSelectedClass",
                         "SetSelectedSex",
                         "IsRaceClassValid",
                         "GetFactionForRace",
                         "GetNameForRace",
                         "GetHairCustomization",
                         "GetFacialHairCustomization",
                         "CycleCharCustomization",
                         "RandomizeCharCustomization",
                         "UpdateSelectionCustomizationScene",
                         "UpdateCustomizationScene",
                         "CreateCharacter",
                         "GetRandomName"};
  for (uint32_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, s->reference);
    lua_pushinteger(s->L, i);
    lua_pushcclosure(s->L, call, 2);
    lua_setglobal(s->L, names[i]);
  }
  return 0;
}
uint32_t bw_characters_metric(const BwCharacters *s, uint32_t field) {
  if (!s)
    return 0;
  if (field == 0)
    return s->connected;
  if (field == 1)
    return s->count;
  if (field == 2)
    return s->selected;
  if (field == 3)
    return s->race;
  if (field == 4)
    return s->class_index;
  if (field == 5)
    return s->gender;
  if (field >= 6 && field <= 10)
    return s->appearance[field - 6];
  if (field == 11)
    return s->revision;
  if (field == 12)
    return s->creating;
  if (field == 13)
    return s->race && s->race <= s->race_count ? s->races[s->race - 1].id : 0;
  if (field == 14)
    return s->class_index && s->class_index <= s->class_count ? s->classes[s->class_index - 1].id
                                                              : 0;
  return 0;
}
uint32_t bw_characters_actions(BwCharacters *s) {
  uint32_t bits = s ? s->actions : 0;
  if (s)
    s->actions = 0;
  return bits;
}
uint32_t bw_characters_request(BwCharacters *s, uint8_t *out, uint32_t capacity) {
  if (!s || !s->creating || !out)
    return 0;
  uint32_t length = (uint32_t)strlen(s->create_name);
  if (capacity < length + 10)
    return 0;
  memcpy(out, s->create_name, length + 1);
  memcpy(out + length + 1, s->create_fields, sizeof(s->create_fields));
  return length + 10;
}
void bw_characters_finish(BwCharacters *s) {
  if (s) {
    s->creating = 0;
    memset(s->create_name, 0, sizeof(s->create_name));
    memset(s->create_fields, 0, sizeof(s->create_fields));
  }
}
void bw_characters_cancel(BwCharacters *s) {
  if (!s || !s->connected)
    return;
  bw_characters_finish(s);
  s->actions |= 4;
}
double bw_characters_facing(const BwCharacters *s, uint32_t creation) {
  return s && creation <= 1 ? s->facing[creation] : 0;
}
