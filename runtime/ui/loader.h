#ifndef BRWOWSER_UI_LOADER_H
#define BRWOWSER_UI_LOADER_H
#include "frames.h"
typedef struct BwUiLoader BwUiLoader;
/* Immutable local resource snapshot; replaces neither MPQ selection nor I/O.
 * Browser/provider supplies exact selected bytes before synchronous execution. */
BwUiLoader *bw_ui_loader_new(lua_State *L, BwUiRuntime *runtime);
uint32_t bw_ui_loader_add(BwUiLoader *loader, const char *name, const uint8_t *bytes,
                          uint32_t length);
/* Host attests every registered source has matched its original manifest hash.
 * Seals further registration, permits only this finite immutable resource set.
 * Not a signature verifier, addon loader or universal secure/taint VM. */
uint32_t bw_ui_loader_seal_verified(BwUiLoader *loader);
uint32_t bw_ui_loader_load(BwUiLoader *loader, const char *name);
const char *bw_ui_loader_error(const BwUiLoader *loader);
void bw_ui_loader_free(BwUiLoader *loader);
#endif
