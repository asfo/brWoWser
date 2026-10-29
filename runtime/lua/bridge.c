/* Public Lua VM embedding boundary. Not the Blizzard VM implementation. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#define INPUT_CAPACITY 1048576u
#define HEAP_LIMIT (64u * 1024u * 1024u)
static lua_State *state;
static char input[INPUT_CAPACITY], message[4096], result_text[65536];
static uint32_t used, steps, instruction_limit;
static double result_number;
static void *allocate(void *ud, void *ptr, size_t old, size_t count) {
  (void)ud;
  if (!ptr)
    old = 0;
  if (!count) {
    free(ptr);
    used -= (uint32_t)old;
    return NULL;
  }
  if (count > HEAP_LIMIT || old > used || (size_t)used - old + count > HEAP_LIMIT)
    return NULL;
  void *next = realloc(ptr, count);
  if (next)
    used = (uint32_t)((size_t)used - old + count);
  return next;
}
static void copy_text(char *dest, uint32_t capacity, const char *source, size_t size) {
  if (size >= capacity)
    size = capacity - 1;
  if (source && size)
    memcpy(dest, source, size);
  dest[size] = 0;
}
static void instruction_hook(lua_State *L, lua_Debug *debug) {
  (void)debug;
  steps += 1000;
  if (steps >= instruction_limit)
    luaL_error(L, "brwowser instruction budget exceeded");
}
char *bw_lua_input_ptr(void) {
  return input;
}
uint32_t bw_lua_input_capacity(void) {
  return INPUT_CAPACITY;
}
const char *bw_lua_error_ptr(void) {
  return message;
}
const char *bw_lua_text_ptr(void) {
  return result_text;
}
double bw_lua_number(void) {
  return result_number;
}
uint32_t bw_lua_heap_bytes(void) {
  return used;
}
void bw_lua_close(void) {
  if (state)
    lua_close(state);
  state = NULL;
}
uint32_t bw_lua_init(void) {
  bw_lua_close();
  used = 0;
  message[0] = 0;
  state = lua_newstate(allocate, NULL);
  if (!state)
    return 1;
  /* Candidate standard libraries, to be compared with the native environment. */
  luaopen_base(state);
  lua_settop(state, 0);
  luaopen_string(state);
  lua_settop(state, 0);
  luaopen_table(state);
  lua_settop(state, 0);
  luaopen_math(state);
  lua_settop(state, 0);
  return 0;
}
uint32_t bw_lua_run(uint32_t length, uint32_t budget) {
  message[0] = 0;
  result_text[0] = 0;
  result_number = 0;
  if (!state || length > INPUT_CAPACITY || budget < 1000 || budget > 10000000)
    return 1;
  lua_settop(state, 0);
  steps = 0;
  instruction_limit = budget;
  int status = luaL_loadbuffer(state, input, length, "@brwowser-local-chunk");
  if (!status) {
    lua_sethook(state, instruction_hook, LUA_MASKCOUNT, 1000);
    status = lua_pcall(state, 0, LUA_MULTRET, 0);
    lua_sethook(state, NULL, 0, 0);
  }
  if (status) {
    size_t size = 0;
    const char *text = lua_tolstring(state, -1, &size);
    copy_text(message, sizeof(message), text, size);
    lua_settop(state, 0);
    return (uint32_t)status + 1;
  }
  return 0;
}
/* Read a named global after a protected call; bytes must contain its ASCII name. */
uint32_t bw_lua_global(uint32_t length) {
  result_text[0] = 0;
  result_number = 0;
  if (!state || !length || length >= 128)
    return 0;
  for (uint32_t i = 0; i < length; i++)
    if (!((input[i] >= 'A' && input[i] <= 'Z') || (input[i] >= 'a' && input[i] <= 'z') ||
          (input[i] >= '0' && input[i] <= '9') || input[i] == '_'))
      return 0;
  input[length] = 0;
  lua_settop(state, 0);
  lua_getglobal(state, input);
  int type = lua_type(state, -1);
  if (type == LUA_TNUMBER)
    result_number = lua_tonumber(state, -1);
  if (type == LUA_TBOOLEAN)
    result_number = (double)lua_toboolean(state, -1);
  if (type == LUA_TSTRING) {
    size_t size = 0;
    const char *text = lua_tolstring(state, -1, &size);
    copy_text(result_text, sizeof(result_text), text, size);
  }
  lua_settop(state, 0);
  return (uint32_t)type + 1;
}

uint32_t bw_lua_result_count(void) {
  return state ? (uint32_t)lua_gettop(state) : 0;
}
uint32_t bw_lua_result(int index) {
  result_text[0] = 0;
  result_number = 0;
  if (!state)
    return 0;
  int type = lua_type(state, index);
  if (type == LUA_TNUMBER)
    result_number = lua_tonumber(state, index);
  if (type == LUA_TBOOLEAN)
    result_number = (double)lua_toboolean(state, index);
  if (type == LUA_TSTRING) {
    size_t size = 0;
    const char *text = lua_tolstring(state, index, &size);
    copy_text(result_text, sizeof(result_text), text, size);
  }
  return (uint32_t)(type + 1);
}

lua_State *bw_lua_state(void) {
  return state;
}
