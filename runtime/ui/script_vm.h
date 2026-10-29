#ifndef BRWOWSER_UI_SCRIPT_VM_H
#define BRWOWSER_UI_SCRIPT_VM_H
#include <stdint.h>
#include "lua.h"
/* Portable descriptor mirrors the observed ref/context semantics, not an address. */
typedef struct BwUiScriptSlot {
  int32_t reference;
  uint32_t context;
} BwUiScriptSlot;
enum BwUiVmStatus {
  BW_UI_VM_OK = 0,
  BW_UI_VM_UNKNOWN_EVENT = 1,
  BW_UI_VM_BAD_INPUT = 2,
  BW_UI_VM_COMPILE_ERROR = 3,
  BW_UI_VM_UNKNOWN_FUNCTION = 4,
  BW_UI_VM_CALL_ERROR = 5
};
uint32_t bw_ui_bind_script(lua_State *L, uint32_t type, const char *event, uint32_t event_length,
                           const char *body, uint32_t body_length, const char *function_attribute,
                           BwUiScriptSlot *slot, char *error, uint32_t error_capacity);
/* Caller supplies n arguments on top of the current Lua stack; owner is a live registry ref. */
uint32_t bw_ui_call_script(lua_State *L, const BwUiScriptSlot *slot, int owner_reference,
                           uint32_t arguments, char *error, uint32_t error_capacity);
/* Explicit closed-resource execution context only. This does not implement
 * Blizzard value/table taint propagation. Missing context defaults untrusted. */
uint32_t bw_ui_execution_context(lua_State *L);
uint32_t bw_ui_execution_swap(lua_State *L, uint32_t untrusted);
void bw_ui_execution_register(lua_State *L);
void bw_ui_clear_script(lua_State *L, BwUiScriptSlot *slot);
#endif
