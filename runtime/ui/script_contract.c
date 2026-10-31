/* Independently reconstructed selector semantics from original 12340 instructions.
 * See validation/ui-script-contract; no client instructions or VM code embedded. */
#include "script_contract.h"
typedef struct Row {
  uint32_t family;
  const char *name;
  uint32_t signature, offset, overridden;
} Row;
static const char *const parameters[] = {
    "self",
    "self,w,h",
    "self,elapsed",
    "self,motion",
    "self,button",
    "self,delta",
    "self,text",
    "self,key",
    "self,name,value",
    "self,event,...",
    "self, userInput",
    "self,x,y,w,h",
    "self,language",
    "self,offset",
    "self,xrange,yrange",
    "self,value",
    "self,min,max",
    "self,button,down",
    "self,link,text",
    "self,link,text,button",
};
static const Row rows[] = {
    {0, "onload", 0, 0x134, 0},
    {0, "onsizechanged", 1, 0x13c, 1},
    {0, "onupdate", 2, 0x144, 1},
    {0, "onshow", 0, 0x14c, 0},
    {0, "onhide", 0, 0x154, 0},
    {0, "onenter", 3, 0x15c, 1},
    {0, "onleave", 3, 0x164, 1},
    {0, "onmousedown", 4, 0x16c, 1},
    {0, "onmouseup", 4, 0x174, 1},
    {0, "onmousewheel", 5, 0x17c, 1},
    {0, "ondragstart", 4, 0x184, 1},
    {0, "ondragstop", 0, 0x18c, 0},
    {0, "onreceivedrag", 0, 0x194, 0},
    {0, "onchar", 6, 0x19c, 1},
    {0, "onkeydown", 7, 0x1a4, 1},
    {0, "onkeyup", 7, 0x1ac, 1},
    {0, "onattributechanged", 8, 0x1b4, 1},
    {0, "onenable", 0, 0x1bc, 0},
    {0, "ondisable", 0, 0x1c4, 0},
    {0, "onevent", 9, 0xc, 1},
    {7, "onmoviefinished", 0, 0x3b4, 0},
    {7, "onmovieshowsubtitle", 6, 0x3bc, 1},
    {7, "onmoviehidesubtitle", 0, 0x3c4, 0},
    {6, "onupdatemodel", 0, 0x358, 0},
    {6, "onanimfinished", 0, 0x360, 0},
    {2, "onenterpressed", 0, 0x380, 0},
    {2, "onescapepressed", 0, 0x388, 0},
    {2, "onspacepressed", 0, 0x390, 0},
    {2, "ontabpressed", 0, 0x398, 0},
    {2, "ontextchanged", 10, 0x3a0, 1},
    {2, "ontextset", 0, 0x3a8, 0},
    {2, "oncursorchanged", 11, 0x3b0, 1},
    {2, "oninputlanguagechanged", 12, 0x3b8, 1},
    {2, "oneditfocusgained", 0, 0x3c0, 0},
    {2, "oneditfocuslost", 0, 0x3c8, 0},
    {2, "oncharcomposition", 6, 0x3d0, 1},
    {3, "onhorizontalscroll", 13, 0x2b4, 1},
    {3, "onverticalscroll", 13, 0x2bc, 1},
    {3, "onscrollrangechanged", 14, 0x2c4, 1},
    {4, "onvaluechanged", 15, 0x2b8, 1},
    {4, "onminmaxchanged", 16, 0x2c0, 1},
    {1, "preclick", 17, 0x2d8, 1},
    {1, "onclick", 17, 0x2e0, 1},
    {1, "postclick", 17, 0x2e8, 1},
    {1, "ondoubleclick", 4, 0x2f0, 1},
    {5, "onhyperlinkenter", 18, 0x2a0, 1},
    {5, "onhyperlinkleave", 18, 0x2a8, 1},
    {5, "onhyperlinkclick", 19, 0x2b0, 1},
};
static uint32_t same_ascii(const char *name, uint32_t length, const char *lower) {
  uint32_t i = 0;
  for (; i < length; i++) {
    unsigned char c = (unsigned char)name[i];
    if (!lower[i])
      return 0;
    if (c >= 'A' && c <= 'Z')
      c = (unsigned char)(c + ('a' - 'A'));
    if (c != (unsigned char)lower[i])
      return 0;
  }
  return lower[i] == 0;
}
uint32_t bw_ui_resolve_script(uint32_t type, const char *name, uint32_t length,
                              BwUiScriptContract *out) {
  if (!out)
    return BW_UI_SCRIPT_BAD_INPUT;
  out->event_id = 0;
  out->signature_id = 0;
  out->native_descriptor_offset = 0;
  out->native_wrapper_overridden = 0;
  if (!name || !length || length > 63)
    return BW_UI_SCRIPT_BAD_INPUT;
  for (uint32_t i = 0; i < length; i++)
    if ((unsigned char)name[i] < 33 || (unsigned char)name[i] > 126)
      return BW_UI_SCRIPT_BAD_INPUT;
  uint32_t family;
  switch (type) {
  case BW_UI_FRAME:
    family = 0;
    break;
  case BW_UI_BUTTON:
  case BW_UI_CHECKBUTTON:
    family = 1;
    break;
  case BW_UI_EDITBOX:
    family = 2;
    break;
  case BW_UI_SCROLLFRAME:
    family = 3;
    break;
  case BW_UI_STATUSBAR:
  case BW_UI_SLIDER:
    family = 4;
    break;
  case BW_UI_SIMPLEHTML:
    family = 5;
    break;
  case BW_UI_MODEL:
  case BW_UI_MODELFFX:
    family = 6;
    break;
  case BW_UI_MOVIEFRAME:
    family = 7;
    break;
  default:
    return BW_UI_SCRIPT_UNSUPPORTED_TYPE;
  }
  /* Native virtual implementations delegate to Frame before their extensions. */
  for (uint32_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    const Row *r = &rows[i];
    if ((r->family == 0 || r->family == family) && same_ascii(name, length, r->name)) {
      out->event_id = i + 1;
      out->signature_id = r->signature;
      out->native_descriptor_offset = r->offset;
      out->native_wrapper_overridden = r->overridden;
      return BW_UI_SCRIPT_OK;
    }
  }
  return BW_UI_SCRIPT_UNMAPPED_EVENT;
}
const char *bw_ui_script_parameters(uint32_t id) {
  return id < sizeof(parameters) / sizeof(parameters[0]) ? parameters[id] : (const char *)0;
}
