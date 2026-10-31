#ifndef BRWOWSER_UI_SCRIPT_CONTRACT_H
#define BRWOWSER_UI_SCRIPT_CONTRACT_H
#include <stdint.h>
/* Portable enums; neither pointers nor original C++ object layouts. */
enum BwUiType {
  BW_UI_FRAME = 1,
  BW_UI_BUTTON,
  BW_UI_CHECKBUTTON,
  BW_UI_EDITBOX,
  BW_UI_SCROLLFRAME,
  BW_UI_SLIDER,
  BW_UI_SIMPLEHTML,
  BW_UI_MODEL,
  BW_UI_MODELFFX,
  BW_UI_MOVIEFRAME,
  BW_UI_STATUSBAR = 14
};
enum BwUiScriptStatus {
  BW_UI_SCRIPT_OK = 0,
  BW_UI_SCRIPT_BAD_INPUT = 1,
  BW_UI_SCRIPT_UNSUPPORTED_TYPE = 2,
  BW_UI_SCRIPT_UNMAPPED_EVENT = 3
};
typedef struct BwUiScriptContract {
  uint32_t event_id;
  uint32_t signature_id;
  /* Comparison metadata only. Never add this offset to a portable frame pointer. */
  uint32_t native_descriptor_offset;
  uint32_t native_wrapper_overridden;
} BwUiScriptContract;
/* Explicit bounded ASCII event bytes, no NUL terminator required. */
uint32_t bw_ui_resolve_script(uint32_t type, const char *name, uint32_t length,
                              BwUiScriptContract *out);
const char *bw_ui_script_parameters(uint32_t signature_id);
#endif
