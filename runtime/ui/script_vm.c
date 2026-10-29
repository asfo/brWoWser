/* Selected native 0x48fef0 attachment and callback boundary reconstruction.
 * Source cache, taint state and complete 0x819ea0 invocation semantics remain open. */
#include "script_vm.h"
#include "script_contract.h"
#include "lauxlib.h"
#include <stdlib.h>
#include <string.h>
static char execution_key;
uint32_t bw_ui_execution_context(lua_State *L) {
  lua_pushlightuserdata(L, &execution_key);
  lua_rawget(L, LUA_REGISTRYINDEX);
  uint32_t value = lua_isnumber(L, -1) ? (uint32_t)lua_tointeger(L, -1) : 1;
  lua_pop(L, 1);
  return value ? 1 : 0;
}
uint32_t bw_ui_execution_swap(lua_State *L, uint32_t untrusted) {
  uint32_t old = bw_ui_execution_context(L);
  lua_pushlightuserdata(L, &execution_key);
  lua_pushinteger(L, untrusted ? 1 : 0);
  lua_rawset(L, LUA_REGISTRYINDEX);
  return old;
}
static int is_secure(lua_State *L) {
  if (bw_ui_execution_context(L))
    lua_pushnil(L);
  else
    lua_pushnumber(L, 1);
  return 1;
}
void bw_ui_execution_register(lua_State *L) {
  lua_pushcfunction(L, is_secure);
  lua_setglobal(L, "issecure");
}
static void message(char *error, uint32_t capacity, const char *text) {
  if (!error || !capacity)
    return;
  uint32_t i = 0;
  if (text)
    for (; i + 1 < capacity && text[i]; i++)
      error[i] = text[i];
  error[i] = 0;
}
void bw_ui_clear_script(lua_State *L, BwUiScriptSlot *slot) {
  if (!slot)
    return;
  if (L && slot->reference > 0)
    luaL_unref(L, LUA_REGISTRYINDEX, slot->reference);
  slot->reference = 0;
  slot->context = 0;
}
uint32_t bw_ui_bind_script(lua_State *L, uint32_t type, const char *event, uint32_t event_length,
                           const char *body, uint32_t body_length, const char *attribute,
                           BwUiScriptSlot *slot, char *error, uint32_t capacity) {
  message(error, capacity, "");
  if (!L || !slot || (!body && body_length) || body_length > 1048576)
    return BW_UI_VM_BAD_INPUT;
  BwUiScriptContract contract;
  if (bw_ui_resolve_script(type, event, event_length, &contract))
    return BW_UI_VM_UNKNOWN_EVENT;
  int top = lua_gettop(L), compiled = 0;
  /* Native binding releases the previous reference before compiling/replacing it. */
  bw_ui_clear_script(L, slot);
  if (body_length) {
    const char *parameters = bw_ui_script_parameters(contract.signature_id);
    size_t n = strlen(parameters);
    size_t length = 16 + n + 2 + body_length + 4;
    char *source = (char *)malloc(length + 1);
    if (!source) {
      message(error, capacity, "script wrapper allocation failed");
      return BW_UI_VM_COMPILE_ERROR;
    }
    const char *prefix = "return function(";
    size_t at = 0;
    memcpy(source + at, prefix, 16);
    at += 16;
    memcpy(source + at, parameters, n);
    at += n;
    memcpy(source + at, ") ", 2);
    at += 2;
    memcpy(source + at, body, body_length);
    at += body_length;
    memcpy(source + at, " end", 4);
    at += 4;
    source[at] = 0;
    int status = luaL_loadbuffer(L, source, at, "@brwowser-frame-handler");
    free(source);
    if (!status)
      status = lua_pcall(L, 0, 1, 0);
    if (status) {
      message(error, capacity, lua_tostring(L, -1));
      lua_settop(L, top);
      return BW_UI_VM_COMPILE_ERROR;
    }
    compiled = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  if (attribute && *attribute) {
    lua_getglobal(L, attribute);
    int reference = luaL_ref(L, LUA_REGISTRYINDEX);
    if (reference == LUA_REFNIL) {
      if (compiled > 0)
        luaL_unref(L, LUA_REGISTRYINDEX, compiled);
      message(error, capacity, "unknown function attribute");
      lua_settop(L, top);
      return BW_UI_VM_UNKNOWN_FUNCTION;
    }
    slot->reference = reference;
  } else if (compiled > 0) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, compiled);
    slot->reference = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  if (compiled > 0)
    luaL_unref(L, LUA_REGISTRYINDEX, compiled);
  slot->context = bw_ui_execution_context(L);
  lua_settop(L, top);
  return BW_UI_VM_OK;
}
uint32_t bw_ui_call_script(lua_State *L, const BwUiScriptSlot *slot, int owner_reference,
                           uint32_t arguments, char *error, uint32_t capacity) {
  message(error, capacity, "");
  if (!L || !slot || arguments > 1024)
    return BW_UI_VM_BAD_INPUT;
  int top = lua_gettop(L);
  if (arguments > (uint32_t)top)
    return BW_UI_VM_BAD_INPUT;
  int base = top - (int)arguments;
  if (slot->reference <= 0) {
    lua_settop(L, base);
    return BW_UI_VM_OK;
  }
  if (!lua_checkstack(L, 4)) {
    message(error, capacity, "callback stack capacity");
    lua_settop(L, base);
    return BW_UI_VM_CALL_ERROR;
  }
  /* Native 0x819f78..0x819fcd saves this and assigns the owner;
  * 0x81a241..0x81a265 restores it after protected invocation. */
  lua_getglobal(L, "this");
  int previous_this = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_rawgeti(L, LUA_REGISTRYINDEX, owner_reference);
  lua_setglobal(L, "this");
  lua_rawgeti(L, LUA_REGISTRYINDEX, slot->reference);
  lua_insert(L, base + 1);
  lua_rawgeti(L, LUA_REGISTRYINDEX, owner_reference);
  lua_insert(L, base + 2);
  uint32_t previous_context = bw_ui_execution_context(L);
  /* A callback cannot elevate a foreign caller into trusted execution. */
  bw_ui_execution_swap(L, previous_context || slot->context);
  int status = lua_pcall(L, (int)arguments + 1, 0, 0);
  bw_ui_execution_swap(L, previous_context);
  if (status)
    message(error, capacity, lua_tostring(L, -1));
  if (previous_this == LUA_REFNIL)
    lua_pushnil(L);
  else
    lua_rawgeti(L, LUA_REGISTRYINDEX, previous_this);
  lua_setglobal(L, "this");
  if (previous_this > 0)
    luaL_unref(L, LUA_REGISTRYINDEX, previous_this);
  lua_settop(L, base);
  return status ? BW_UI_VM_CALL_ERROR : BW_UI_VM_OK;
}
