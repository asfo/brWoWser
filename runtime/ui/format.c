/* Reconstructed from original 0x818070 argument walk and 0x48d800 buffer use.
 * Selected finite numbers / bounded standard specifiers; CRT edge equivalence
 * and exceptional/out-of-range conversion are deliberately not claimed. */
#include "format.h"
#include "lauxlib.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
void bw_ui_format(lua_State *L, int index, char *out, uint32_t capacity) {
  if (!out || !capacity)
    luaL_error(L, "format output capacity");
  const char *source = luaL_checkstring(L, index);
  uint32_t used = 0;
  int argument = index + 1;
  while (*source && used + 1 < capacity) {
    if (*source != '%') {
      out[used++] = *source++;
      continue;
    }
    source++;
    if (*source == '%') {
      out[used++] = '%';
      source++;
      continue;
    }
    if (source[0] >= '1' && source[0] <= '9' && source[1] == '$') {
      argument = index + source[0] - '0';
      source += 2;
    }
    int selected = argument++;
    char spec[128];
    uint32_t length = 1;
    spec[0] = '%';
    while (*source && strchr("-+ #0", *source)) {
      if (length >= 120)
        luaL_error(L, "format specification capacity");
      spec[length++] = *source++;
    }
    uint32_t width = 0;
    while (*source >= '0' && *source <= '9') {
      width = width * 10 + (uint32_t)(*source - '0');
      if (width > 4096 || length >= 120)
        luaL_error(L, "format width capacity");
      spec[length++] = *source++;
    }
    if (*source == '.') {
      spec[length++] = *source++;
      uint32_t precision = 0;
      while (*source >= '0' && *source <= '9') {
        precision = precision * 10 + (uint32_t)(*source - '0');
        if (precision > 4096 || length >= 120)
          luaL_error(L, "format precision capacity");
        spec[length++] = *source++;
      }
    }
    char conversion = *source;
    if (!conversion || !strchr("scdiuoxXfFeEgG", conversion))
      luaL_error(L, "unsupported format conversion");
    source++;
    spec[length++] = conversion == 'F' ? 'f' : conversion;
    spec[length] = 0;
    int count;
    if (conversion == 's') {
      const char *text = luaL_checkstring(L, selected);
      count = snprintf(out + used, capacity - used, spec, text);
    } else {
      double value = luaL_checknumber(L, selected);
      if (!isfinite(value))
        luaL_error(L, "nonfinite formatted number");
      if (strchr("cdiuoxX", conversion)) {
        if (value < (-2147483647.0 - 1) || value > 2147483647.0)
          luaL_error(L, "formatted integer range");
        int32_t integer = (int32_t)value;
        if (conversion == 'c') {
          if (length != 2)
            luaL_error(L, "unsupported character format flags");
          out[used++] = (char)integer;
          continue;
        }
        if (strchr("uoxX", conversion))
          count = snprintf(out + used, capacity - used, spec, (uint32_t)integer);
        else
          count = snprintf(out + used, capacity - used, spec, integer);
      } else
        count = snprintf(out + used, capacity - used, spec, value);
    }
    if (count < 0)
      luaL_error(L, "format conversion failed");
    if ((uint32_t)count >= capacity - used) {
      used = capacity - 1;
      break;
    }
    used += (uint32_t)count;
  }
  out[used] = 0;
}
