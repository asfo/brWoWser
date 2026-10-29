#ifndef BRWOWSER_UI_FRAMES_H
#define BRWOWSER_UI_FRAMES_H
#include "lua.h"
#include "document.h"
#include "font.h"
#include "../movie/movie.h"
#include <stdint.h>
typedef struct BwUiRuntime BwUiRuntime;
uint32_t bw_ui_runtime_input_at(BwUiRuntime *, uint32_t, double, double, int32_t);
uint32_t bw_ui_runtime_edit_action(BwUiRuntime *, uint32_t, const char *, uint32_t);
uint32_t bw_ui_runtime_edit_pump(BwUiRuntime *);
uint32_t bw_ui_runtime_edit_focus(BwUiRuntime *);
uint32_t bw_ui_runtime_input_target(BwUiRuntime *);
uint32_t bw_ui_runtime_update_scroll(BwUiRuntime *);
uint32_t bw_ui_runtime_html_asset(BwUiRuntime *, const char *, const uint8_t *, uint32_t);
BwMovie *bw_ui_runtime_movie(BwUiRuntime *);
uint32_t bw_ui_runtime_movie_complete(BwUiRuntime *, uint32_t);
uint32_t bw_ui_runtime_cursor_hidden(BwUiRuntime *);
typedef uint32_t (*BwUiXmlDispatch)(void *context, const BwXmlNode *node);
enum BwUiModelOperation {
  BW_UI_MODEL_FILE = 1,
  BW_UI_MODEL_CAMERA = 2,
  BW_UI_MODEL_SEQUENCE = 3,
  BW_UI_MODEL_SEQUENCE_TIME = 4,
  BW_UI_MODEL_SCALE = 5
};
typedef struct BwUiModelRequest {
  uint32_t object, operation, value, time_ms;
  float scalar;
  char path[512];
} BwUiModelRequest;
/* Admitted asset identity is supplied by the actual resource backend, never
 * inferred from a script filename. Requests are not render/load success proof. */
uint32_t bw_ui_runtime_model_role(BwUiRuntime *runtime, uint32_t role);
double bw_ui_runtime_model_parameter(BwUiRuntime *, uint32_t role, uint32_t field, uint32_t index);
uint32_t bw_ui_runtime_font_asset(BwUiRuntime *runtime, const char *path, const uint8_t *bytes,
                                  uint32_t length);
uint32_t bw_ui_runtime_model_asset(BwUiRuntime *runtime, const char *path);
uint32_t bw_ui_runtime_model_request(BwUiRuntime *runtime, BwUiModelRequest *out);
/* Read-only renderer boundary: object index is stable until runtime destruction.
 * Borrowed strings expire on mutation. geometry_status: 0 resolved, 1 rejected,
 * 2 unpositioned. No synthetic origin or hidden HTML layout is supplied.
 * Coordinates are existing logical bottom-left coordinates; effective_scale is
 * separate metadata (mixed-scale native anchor conversion is not reconstructed). */
typedef struct BwUiDrawItem {
  uint32_t object, parent, type, visible, geometry_status, layer, enabled;
  int32_t stratum, level;
  double own_alpha, left, bottom, width, height, alpha, effective_scale, font_height, shadow_x,
      shadow_y;
  float uv[8];
  uint8_t color[4], backdrop_color[4], border_color[4], shadow_color[4];
  const char *name, *texture, *text, *font, *font_flags, *justify_h, *justify_v;
  const char *backdrop_bg, *backdrop_edge;
  double edge_size, tile_size, inset_left, inset_right, inset_top, inset_bottom;
  uint32_t backdrop, tile, outline, html_loaded, has_clip, additive, pressed, hovered,
      mouse_enabled;
  double clip_left, clip_bottom, clip_width, clip_height;
  char geometry_error[256];
} BwUiDrawItem;
/* Public FreeType baseline policy; native glyph placement/pixels are a separate
 * gate. C owns glyph placement and UTF-8 validation; device code draws quads. */
typedef struct BwUiTextQuad {
  uint32_t codepoint, width, height;
  double left, bottom;
  uint8_t color[4];
} BwUiTextQuad;
uint32_t bw_ui_runtime_text_quad(BwUiRuntime *, uint32_t, uint32_t, BwUiTextQuad *, uint8_t *,
                                 uint32_t);
typedef struct BwUiImageQuad {
  double left, bottom, width, height;
  float uv[8];
  uint8_t color[4];
  const char *asset;
  uint8_t vertex_colors[16];
  uint32_t has_vertex_colors, desaturated;
} BwUiImageQuad;
/* Selected eight-tile atlas policy, not native border-pixel equivalence.
 * 0 success, 2 end, 3 absent part, 1 rejected geometry. */
uint32_t bw_ui_runtime_image_quad(BwUiRuntime *, uint32_t, uint32_t, BwUiImageQuad *);
uint32_t bw_ui_runtime_draw_order(BwUiRuntime *, uint32_t *, uint32_t);
uint32_t bw_ui_runtime_draw_item(BwUiRuntime *runtime, uint32_t index, BwUiDrawItem *out);
uint32_t bw_ui_runtime_draw_glyph(BwUiRuntime *runtime, uint32_t index, uint32_t codepoint,
                                  BwFontGlyph *out, uint8_t *coverage, uint32_t capacity);
/* Own objects and Lua registry references; no native pointers or layouts reused. */
BwUiRuntime *bw_ui_runtime_new(lua_State *L);
uint32_t bw_ui_runtime_protected_failures(const BwUiRuntime *runtime);
const char *bw_ui_runtime_protected_error(const BwUiRuntime *runtime);
/* Host resource backend attests a decoded immutable texture with these dimensions.
 * Pixel upload, draw lifecycle and native path/creation semantics remain separate. */
uint32_t bw_ui_runtime_texture_asset(BwUiRuntime *runtime, const char *path, uint32_t width,
                                     uint32_t height);
void bw_ui_runtime_free(BwUiRuntime *runtime);
uint32_t bw_ui_runtime_register(BwUiRuntime *runtime);
uint32_t bw_ui_runtime_viewport(BwUiRuntime *runtime, double width, double height);
uint32_t bw_ui_runtime_define_event(BwUiRuntime *runtime, const char *event);
/* Consume arguments from Lua stack, delivering (self,event,...args). */
uint32_t bw_ui_runtime_event(BwUiRuntime *runtime, const char *event, uint32_t arguments);
/* Documents are retained by caller until runtime is freed. Selected XML tree
 * composition only: lifecycle is a separate explicit operation. */
uint32_t bw_ui_runtime_document(BwUiRuntime *runtime, const BwXmlNode *root);
void bw_ui_runtime_auto_load(BwUiRuntime *runtime, int enabled);
void bw_ui_runtime_dispatch(BwUiRuntime *runtime, BwUiXmlDispatch dispatch, void *context);
uint32_t bw_ui_runtime_load(BwUiRuntime *runtime);
uint32_t bw_ui_runtime_count(const BwUiRuntime *runtime);
const char *bw_ui_runtime_error(const BwUiRuntime *runtime);
uint32_t bw_ui_runtime_options_readonly(BwUiRuntime *);
#endif
