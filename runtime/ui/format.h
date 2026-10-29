#ifndef BRWOWSER_UI_FORMAT_H
#define BRWOWSER_UI_FORMAT_H
#include "lua.h"
#include <stdint.h>
/* Selected native-style positional formatter. Errors use Lua's protected path. */
void bw_ui_format(lua_State *L, int index, char *output, uint32_t capacity);
#endif
