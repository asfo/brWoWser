#ifndef BRWOWSER_CHARACTERS_H
#define BRWOWSER_CHARACTERS_H
#include "lua.h"
#include <stdint.h>
typedef struct BwCharacters BwCharacters;
BwCharacters *bw_characters_new(lua_State *L);
void bw_characters_free(BwCharacters *state);
uint32_t bw_characters_register(BwCharacters *state);
/* Bounded host import: race, class, combination, section, facial hair, roster.
 * Numeric fields are little-endian u32; trailing strings are NUL terminated.
 * Race/class records accept a sixth string for the background token. Older
 * records without that string retain their identity token as the fallback. */
uint32_t bw_characters_import(BwCharacters *, uint32_t kind, const uint8_t *, uint32_t length);
uint32_t bw_characters_roster_begin(BwCharacters *);
uint32_t bw_characters_connected(BwCharacters *, uint32_t connected);
uint32_t bw_characters_metric(const BwCharacters *, uint32_t field);
uint32_t bw_characters_actions(BwCharacters *);
uint32_t bw_characters_request(BwCharacters *, uint8_t *output, uint32_t capacity);
void bw_characters_finish(BwCharacters *);
void bw_characters_cancel(BwCharacters *);
double bw_characters_facing(const BwCharacters *, uint32_t creation);
#endif
