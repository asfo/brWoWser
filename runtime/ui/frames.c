/* Portable object identity, selected XML composition and Lua frame boundary.
 * Native observations: tables contain a slot-0 object token; IsShown returns
 * numeric 1/nil. Own allocations, method checks and bounded error policies. */
#include "frames.h"
#include "script_vm.h"
#include "script_contract.h"
#include "anchors.h"
#include "format.h"
#include "simplehtml.h"
#include "edit_text.h"
#include "font.h"
#include "lauxlib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#define MAX_OBJECTS 8192
#define MAX_DEFINITIONS 1024
#define MAX_NAME 256
#define MAX_HANDLERS 64
typedef struct Object Object;
typedef struct Point {
  uint32_t point, relative_point;
  Object *relative;
  char relative_name[MAX_NAME];
  double x, y;
} Point;
typedef struct Subscription {
  Object *object;
  char event[128];
} Subscription;
typedef struct TextureAsset {
  char path[512];
  uint32_t width, height;
} TextureAsset;
typedef struct Definition {
  char name[MAX_NAME];
  const BwXmlNode *node;
} Definition;
struct Object {
  struct BwUiRuntime *runtime;
  uint32_t id, type;
  char name[MAX_NAME], tag[32];
  Object *parent, *all_points_target;
  int frame_level, frame_stratum;
  uint32_t input_flags;
  float model_scale;
  int reference, shown, loaded, enabled, user_id, highlight_locked, checked, auto_focus,
      max_letters;
  double width, height, alpha, scale;
  BwXmlNode *composed;
  BwUiScriptSlot scripts[MAX_HANDLERS];
  Point points[32];
  uint32_t point_count;
  int all_points, generated_points, width_known, height_known, solving, has_backdrop;
  uint8_t backdrop_color[4], border_color[4], vertex_color[4];
  uint32_t draw_layer;
  int has_color, pressed, hovered, desaturated;
  double press_x, press_y;
  float uv[8];
  Object *scroll_child;
  double scroll_x, scroll_y, scroll_range_x, scroll_range_y;
  float minimum, range, value, step;
  int range_initialized, value_initialized, slider_vertical;
  Object *thumb, *bar_texture;
  Object *html_fonts[4];
  Object *font_object, *normal_font, *highlight_font, *disabled_font, *button_text;
  Object *button_textures[6];
  char *text, *font_file, *texture_file;
  double font_height, font_spacing, shadow_x, shadow_y;
  int has_shadow;
  char font_flags[48], justify_h[8], justify_v[8];
  BwEditText *edit;
  Object *edit_display;
  int edit_password, edit_dirty, edit_user;
  uint64_t click_mask;
  int click_registered, click_busy, html_loaded;
  uint32_t text_version, layout_version, layout_outline;
  int layout_ellipsis;
  BwFont *layout_font;
  BwTextLine *layout_lines;
  BwTextLayout layout_result;
  float layout_width, layout_height, layout_shadow;
  double layout_nominal_height;
  int layout_height_known;
  uint32_t camera, sequence;
  float model_fog[5], model_glow, model_lights[6][4][13];
  uint32_t model_fog_enabled, model_light_count[6], model_advance_requested;
  char model_path[512];
  int model_pending;
};
typedef struct HtmlAsset {
  char path[64];
  char *text;
} HtmlAsset;
typedef struct FontAsset {
  char path[256];
  uint8_t *bytes;
  uint32_t length;
} FontAsset;
typedef struct FontFace {
  FontAsset *asset;
  uint32_t pixels;
  BwFont *font;
} FontFace;
struct BwUiRuntime {
  HtmlAsset html_assets[8];
  uint32_t html_asset_count;
  BwMovie *movie;
  int cursor_hidden;
  TextureAsset texture_assets[2048];
  uint32_t texture_asset_count;
  FontAsset font_assets[8];
  uint32_t font_asset_count, font_bytes;
  FontFace font_faces[128];
  uint32_t font_face_count;
  lua_State *L;
  Object *objects[MAX_OBJECTS];
  uint32_t count, definitions;
  Definition definition[MAX_DEFINITIONS];
  char error[1024];
  uint32_t last_input_target;
  uint32_t load_depth;
  int auto_load, deferred_lifecycle, template_only;
  uint32_t protected_failures;
  char protected_error[1024];
  BwUiRuntime **handle;
  int handle_reference, error_handler, metatables[15];
  BwUiXmlDispatch dispatch;
  void *dispatch_context;
  int options_readonly;
  double viewport_width, viewport_height;
  double cursor_x, cursor_y;
  Object *focus, *drag_slider, *pressed_button, *hover_button, *mouse_capture, *model_roles[2];
  double drag_offset;
  Subscription subscriptions[8192];
  uint32_t subscription_count, event_count, event_depth;
  char events[1024][128];
  char model_assets[64][512];
  uint32_t model_asset_count, model_head, model_count;
  BwUiModelRequest model_requests[512];
};
static int fail(BwUiRuntime *r, const char *s) {
  snprintf(r->error, sizeof(r->error), "%s", s);
  return 1;
}
static const char *local(const char *s) {
  const char *p = strrchr(s, ':');
  return p ? p + 1 : s;
}
static int tag(const BwXmlNode *n, const char *s) {
  return n && !strcmp(local(n->name), s);
}
static const char *attr(const BwXmlNode *n, const char *s) {
  for (BwXmlAttribute *a = n ? n->attributes : NULL; a; a = a->next)
    if (!strcmp(local(a->name), s))
      return a->value;
  return NULL;
}
static char *copy(const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}
static void release(BwXmlNode *n) {
  while (n) {
    BwXmlNode *next = n->next;
    BwXmlAttribute *a = n->attributes;
    while (a) {
      BwXmlAttribute *b = a->next;
      free(a->name);
      free(a->value);
      free(a);
      a = b;
    }
    release(n->child);
    free(n->name);
    free(n->text);
    free(n);
    n = next;
  }
}
static BwXmlNode *clone(const BwXmlNode *n) {
  if (!n)
    return NULL;
  BwXmlNode *out = calloc(1, sizeof(*out));
  if (!out)
    return NULL;
  out->name = copy(n->name);
  out->text = copy(n->text);
  if (!out->name || (n->text && !out->text))
    goto bad;
  BwXmlAttribute **a = &out->attributes;
  for (const BwXmlAttribute *s = n->attributes; s; s = s->next) {
    *a = calloc(1, sizeof(**a));
    if (!*a)
      goto bad;
    (*a)->name = copy(s->name);
    (*a)->value = copy(s->value);
    if (!(*a)->name || !(*a)->value)
      goto bad;
    a = &(*a)->next;
  }
  BwXmlNode **c = &out->child;
  for (const BwXmlNode *s = n->child; s; s = s->next) {
    *c = clone(s);
    if (!*c)
      goto bad;
    c = &(*c)->next;
  }
  return out;
bad:
  release(out);
  return NULL;
}
static int set_attribute(BwXmlNode *n, const char *name, const char *value) {
  BwXmlAttribute **p = &n->attributes;
  while (*p && strcmp((*p)->name, name))
    p = &(*p)->next;
  if (!*p) {
    *p = calloc(1, sizeof(**p));
    if (!*p)
      return 1;
    (*p)->name = copy(name);
    if (!(*p)->name)
      return 1;
  }
  char *v = copy(value);
  if (!v)
    return 1;
  free((*p)->value);
  (*p)->value = v;
  return 0;
}
static int merge(BwXmlNode *base, const BwXmlNode *over) {
  for (BwXmlAttribute *a = over->attributes; a; a = a->next)
    if (strcmp(a->name, "name") && strcmp(a->name, "virtual") && strcmp(a->name, "inherits"))
      if (set_attribute(base, a->name, a->value))
        return 1;
  for (const BwXmlNode *c = over->child; c; c = c->next) {
    BwXmlNode **p = &base->child;
    while (*p && strcmp(local((*p)->name), local(c->name)))
      p = &(*p)->next;
    if (*p && (tag(c, "Frames") || tag(c, "Layers") || tag(c, "Scripts"))) {
      BwXmlNode **tail = &(*p)->child;
      while (*tail)
        tail = &(*tail)->next;
      for (const BwXmlNode *k = c->child; k; k = k->next) {
        *tail = clone(k);
        if (!*tail)
          return 1;
        tail = &(*tail)->next;
      }
    } else {
      BwXmlNode *n = clone(c);
      if (!n)
        return 1;
      if (*p) {
        n->next = (*p)->next;
        (*p)->next = NULL;
        release(*p);
      }
      *p = n;
    }
  }
  return 0;
}
static Definition *definition(BwUiRuntime *r, const char *s) {
  for (uint32_t i = 0; i < r->definitions; i++)
    if (!strcmp(r->definition[i].name, s))
      return &r->definition[i];
  return NULL;
}
static int inherit(BwUiRuntime *r, BwXmlNode *out, const BwXmlNode *n, const BwXmlNode **stack,
                   uint32_t depth) {
  if (depth >= 64)
    return fail(r, "template depth/cycle");
  for (uint32_t i = 0; i < depth; i++)
    if (stack[i] == n)
      return fail(r, "template cycle");
  stack[depth] = n;
  const char *s = attr(n, "inherits");
  while (s && *s) {
    while (*s == ' ' || *s == '\t' || *s == ',')
      s++;
    const char *end = s;
    while (*end && *end != ',')
      end++;
    const char *trim = end;
    while (trim > s && (trim[-1] == ' ' || trim[-1] == '\t'))
      trim--;
    size_t size = (size_t)(trim - s);
    if (!size || size >= MAX_NAME)
      return fail(r, "invalid template name");
    char name[MAX_NAME];
    memcpy(name, s, size);
    name[size] = 0;
    Definition *d = definition(r, name);
    if (!d)
      return fail(r, "unknown template");
    if (inherit(r, out, d->node, stack, depth + 1))
      return 1;
    s = *end ? end + 1 : end;
  }
  return merge(out, n);
}
static Object *find(BwUiRuntime *r, const char *s) {
  if (!s)
    return NULL;
  for (uint32_t i = 0; i < r->count; i++)
    if (!strcmp(r->objects[i]->name, s))
      return r->objects[i];
  return NULL;
}
static const char *point_names[] = {"BOTTOMLEFT", "BOTTOM",  "BOTTOMRIGHT", "LEFT",    "CENTER",
                                    "RIGHT",      "TOPLEFT", "TOP",         "TOPRIGHT"};
static int point_id(const char *s) {
  if (!s)
    return -1;
  for (int i = 0; i < 9; i++)
    if (!strcmp(s, point_names[i]))
      return i;
  return -1;
}
static int expand_name(const char *, const char *, char *);
static int intrinsic(Object *, double *);
static int visible(Object *);
static int natural_height(Object *, double *);
/* 0: resolved rectangle; 1: unsupported/conflicting input; 2: no position.
 * Native 0x489230 distinguishes a valid rectangle from a sized but unanchored
 * region. Do not synthesize an origin just because it has width/height. */
/* Source FontString/options XML combines center hints with edge anchors.
 * Resolve the edge constraints independently on each axis. */
static int region_anchor_rect(const BwAnchor *a, uint32_t n, int wk, double w, int hk, double h,
                              BwAnchorRect *out) {
  if (!n)
    return 1;
  BwAnchorRect result = {0};
  for (int axis = 0; axis < 2; axis++) {
    uint32_t first = 0, second = UINT32_MAX;
    for (uint32_t i = 0; i < n; i++) {
      uint32_t f = axis ? a[i].point / 3 : a[i].point % 3;
      if (f != 1) {
        first = i;
        break;
      }
    }
    uint32_t f0 = axis ? a[first].point / 3 : a[first].point % 3;
    for (uint32_t i = 0; i < n; i++) {
      uint32_t f = axis ? a[i].point / 3 : a[i].point % 3;
      if (f != f0 && f != 1) {
        second = i;
        if (f != 1)
          break;
      }
    }
    if (second == UINT32_MAX && !(axis ? hk : wk))
      for (uint32_t i = 0; i < n; i++) {
        uint32_t f = axis ? a[i].point / 3 : a[i].point % 3;
        if (f != f0) {
          second = i;
          break;
        }
      }
    BwAnchor chosen[2];
    uint32_t ids[2] = {first, second}, count = second == UINT32_MAX ? 1 : 2;
    for (uint32_t i = 0; i < count; i++) {
      chosen[i] = a[ids[i]];
      if (axis) {
        chosen[i].point = chosen[i].point / 3 * 3;
        chosen[i].relative_point = chosen[i].relative_point / 3 * 3;
        chosen[i].relative.left = chosen[i].relative.width = chosen[i].x = 0;
      } else {
        chosen[i].point %= 3;
        chosen[i].relative_point %= 3;
        chosen[i].relative.bottom = chosen[i].relative.height = chosen[i].y = 0;
      }
    }
    BwAnchorRect solved;
    int status = bw_anchor_solve(chosen, count, axis ? 1 : wk, axis ? 0 : w, axis ? hk : 1,
                                 axis ? h : 0, .002, &solved);
    if (status)
      return status;
    if (axis) {
      result.bottom = solved.bottom;
      result.height = solved.height;
    } else {
      result.left = solved.left;
      result.width = solved.width;
    }
  }
  *out = result;
  return 0;
}
static int rectangle(Object *o, BwAnchorRect *out, uint32_t depth) {
  BwUiRuntime *r = o->runtime;
  if (depth >= 128 || o->solving)
    return fail(r, "anchor dependency cycle/depth");
  o->solving = 1;
  int status = 0;
  BwAnchor local_anchors[2], *anchors = NULL;
  BwAnchorRect viewport = {0, 0, r->viewport_width, r->viewport_height};
  if (o->generated_points && !visible(o)) {
    status = 2;
    goto done;
  }
  if (o->all_points) {
    Object *target = o->all_points_target;
    if (target)
      status = rectangle(target, out, depth + 1);
    else
      *out = viewport;
    goto done;
  }
  /* Selected deferred Slider update 0x96c210..0x96c31b. */
  if (!o->point_count && o->parent && o->parent->type == BW_UI_SLIDER && o->parent->thumb == o) {
    Object *p = o->parent;
    BwAnchorRect parent_rect;
    if (!p->range_initialized || !p->value_initialized) {
      status = 2;
      goto done;
    }
    if (p->scale != 1 || !o->width_known || !o->height_known) {
      status = fail(r, "unsupported slider thumb geometry profile");
      goto done;
    }
    status = rectangle(p, &parent_rect, depth + 1);
    if (status)
      goto done;
    double t = p->range ? ((double)p->value - p->minimum) / p->range : 0;
    BwAnchor anchor = {p->slider_vertical ? BW_ANCHOR_TOP : BW_ANCHOR_LEFT,
                       p->slider_vertical ? BW_ANCHOR_TOP : BW_ANCHOR_LEFT, parent_rect,
                       p->slider_vertical ? 0 : (parent_rect.width - o->width) * t,
                       p->slider_vertical ? -(parent_rect.height - o->height) * t : 0};
    status = bw_anchor_solve(&anchor, 1, 1, o->width, 1, o->height, .002, out);
    goto done;
  }
  if (!o->point_count) {
    status = 2;
    goto done;
  }
  anchors = o->point_count <= 2 ? local_anchors : malloc(o->point_count * sizeof(*anchors));
  if (!anchors) {
    status = fail(r, "anchor allocation");
    goto done;
  }
  for (uint32_t i = 0; i < o->point_count; i++) {
    Point *p = &o->points[i];
    Object *target = p->relative ? p->relative : o->parent;
    if (p->relative_name[0]) {
      target = find(r, p->relative_name);
      if (!target) {
        status = fail(r, "unresolved anchor reference");
        goto done;
      }
    }
    BwAnchorRect relative = viewport;
    if (target) {
      status = rectangle(target, &relative, depth + 1);
      if (status)
        goto done;
    }
    anchors[i] = (BwAnchor){p->point, p->relative_point, relative, p->x, p->y};
  }
  int wk = o->width_known, hk = o->height_known;
  double w = o->width, h = o->height;
  if (o->type == 12) {
    /* FontString:SetWidth(0) releases the explicit width. Glue's tab
     * templates use it after measuring text to restore natural sizing. */
    if (wk && w == 0)
      wk = 0;
    if (!wk && o->point_count < 2) {
      if (intrinsic(o, &w)) {
        status = 1;
        goto done;
      }
      wk = 1;
    }
    if (!hk || h == 0) {
      if (natural_height(o, &h)) {
        status = 1;
        goto done;
      }
      hk = 1;
    }
  }
  status = bw_anchor_solve(anchors, o->point_count, wk, w, hk, h, 0.002, out);
  if (o->type == 12)
    status = region_anchor_rect(anchors, o->point_count, wk, w, hk, h, out);
  if (r->options_readonly) {
    int options = 0;
    for (Object *p = o; p; p = p->parent)
      if (!strcmp(p->name, "VideoOptionsFrame") || !strcmp(p->name, "AudioOptionsFrame"))
        options = 1;
    if (options)
      status = region_anchor_rect(anchors, o->point_count, wk, w, hk, h, out);
  }
  if (status)
    status = fail(r, "underdetermined/conflicting anchor constraints");
done:
  if (!status && o->parent && o->parent->button_text == o && o->parent->pressed) {
    out->left += o->parent->press_x;
    out->bottom += o->parent->press_y;
  }
  if (anchors && anchors != local_anchors)
    free(anchors);
  o->solving = 0;
  return status;
}
static BwUiRuntime *current(lua_State *L) {
  BwUiRuntime **handle = lua_touserdata(L, lua_upvalueindex(1));
  if (!handle || !*handle)
    luaL_error(L, "closed frame runtime");
  return *handle;
}
static Object *owner(lua_State *L, int index) {
  luaL_checktype(L, index, LUA_TTABLE);
  lua_rawgeti(L, index, 0);
  Object *o = lua_touserdata(L, -1);
  lua_pop(L, 1);
  BwUiRuntime *r = current(L);
  int valid = 0;
  for (uint32_t i = 0; i < r->count; i++)
    if (r->objects[i] == o) {
      valid = 1;
      break;
    }
  if (!valid)
    luaL_error(L, "invalid frame owner");
  lua_rawgeti(L, LUA_REGISTRYINDEX, o->reference);
  valid = lua_rawequal(L, index, -1);
  lua_pop(L, 1);
  if (!valid)
    luaL_error(L, "copied frame token");
  return o;
}
static int frame_type(uint32_t type) {
  return type <= 10 || type == BW_UI_STATUSBAR;
}
static const char *type_names[] = {
    "Frame", "Button",   "CheckButton", "EditBox", "ScrollFrame", "Slider", "SimpleHTML",
    "Model", "ModelFFX", "MovieFrame",  "Texture", "FontString",  "Font",   "StatusBar"};
static uint32_t type_id(const char *s) {
  /* WorldFrame has frame semantics here; the outdoor WASM renderer supplies
   * its scene behind the UI instead of creating another browser surface. */
  if (!strcmp(s, "WorldFrame"))
    return BW_UI_FRAME;
  for (uint32_t i = 0; i < 14; i++) {
    const char *a = s, *b = type_names[i];
    while (*a && *b) {
      char x = *a++, y = *b++;
      if (x >= 'a' && x <= 'z')
        x -= 32;
      if (y >= 'a' && y <= 'z')
        y -= 32;
      if (x != y)
        break;
      if (!*a && !*b)
        return i + 1;
    }
  }
  return 0;
}
static int numeric_flag(lua_State *L, int value) {
  if (value)
    lua_pushnumber(L, 1);
  else
    lua_pushnil(L);
  return 1;
}
static int visible(Object *o) {
  for (uint32_t i = 0; o && i < MAX_OBJECTS; i++, o = o->parent)
    if (!o->shown)
      return 0;
  return o == NULL;
}
static uint32_t event_id(Object *o, const char *event) {
  BwUiScriptContract c;
  if (bw_ui_resolve_script(o->type, event, (uint32_t)strlen(event), &c) ||
      c.event_id >= MAX_HANDLERS)
    return 0;
  return c.event_id;
}
static int invoke(Object *o, const char *event) {
  if (o->runtime->deferred_lifecycle && !o->loaded)
    return 0;
  uint32_t id = event_id(o, event);
  if (!id)
    return 0;
  return bw_ui_call_script(o->runtime->L, &o->scripts[id], o->reference, 0, o->runtime->error,
                           sizeof(o->runtime->error));
}
static int change_visibility(Object *o, int shown) {
  BwUiRuntime *r = o->runtime;
  uint32_t count = r->count;
  uint8_t before[MAX_OBJECTS];
  for (uint32_t i = 0; i < count; i++)
    before[i] = (uint8_t)visible(r->objects[i]);
  o->shown = shown;
  for (uint32_t i = 0; i < count; i++) {
    Object *c = r->objects[i], *p = c;
    while (p && p != o)
      p = p->parent;
    if (!p)
      continue;
    int now = visible(c);
    if (before[i] != now && invoke(c, now ? "OnShow" : "OnHide"))
      return 1;
  }
  return 0;
}
static int known_event(BwUiRuntime *r, const char *event) {
  for (uint32_t i = 0; i < r->event_count; i++)
    if (!strcmp(r->events[i], event))
      return 1;
  return 0;
}
static int color(lua_State *L, int first, uint8_t *out, int preserve_alpha) {
  for (int i = 0; i < 4; i++) {
    double value = (i == 3 && !lua_isnumber(L, first + i)) ? (preserve_alpha ? out[3] / 255.0 : 1)
                                                           : lua_tonumber(L, first + i);
    if (!isfinite(value))
      return luaL_error(L, "nonfinite appearance color");
    if (value < 0)
      value = 0;
    if (value > 1)
      value = 1;
    out[i] = (uint8_t)(value * 255 + 0.5);
  }
  return 0;
}
static const char *strata_names[] = {
    "BACKGROUND", "LOW", "MEDIUM", "HIGH", "DIALOG", "FULLSCREEN", "FULLSCREEN_DIALOG", "TOOLTIP"};
static int stratum(const char *s) {
  for (int i = 0; i < 8; i++)
    if (!strcmp(s, strata_names[i]))
      return i + 1;
  return 0;
}
static int set_stratum(Object *o, int value, uint32_t depth) {
  if (depth >= 128)
    return fail(o->runtime, "frame strata depth");
  if (o->frame_stratum == value)
    return 0;
  o->frame_stratum = value;
  for (uint32_t i = 0; i < o->runtime->count; i++) {
    Object *c = o->runtime->objects[i];
    if (c->parent == o && frame_type(c->type) && set_stratum(c, value, depth + 1))
      return 1;
  }
  return 0;
}
static int set_level(Object *o, int value, uint32_t depth) {
  if (depth >= 128)
    return fail(o->runtime, "frame level depth");
  if (value < 0)
    value = 0;
  int64_t difference = (int64_t)value - o->frame_level;
  if (difference > 128)
    difference = 128;
  if (!difference)
    return 0;
  o->frame_level += (int)difference;
  for (uint32_t i = 0; i < o->runtime->count; i++) {
    Object *c = o->runtime->objects[i];
    if (c->parent == o && frame_type(c->type) && c->frame_stratum == o->frame_stratum) {
      int64_t next = (int64_t)c->frame_level + difference;
      if (next > 2147483647)
        return fail(o->runtime, "frame level capacity");
      if (set_level(c, next < 0 ? 0 : (int)next, depth + 1))
        return 1;
    }
  }
  return 0;
}
static void all_points(Object *o, Object *target) {
  o->all_points = 1;
  o->generated_points = 0;
  o->all_points_target = target;
  o->point_count = 2;
  memset(o->points, 0, sizeof(o->points));
  o->points[0].point = o->points[0].relative_point = 6;
  o->points[1].point = o->points[1].relative_point = 2;
  o->points[0].relative = o->points[1].relative = target;
}
static Object *create(BwUiRuntime *r, const char *type, const char *name, Object *parent);
static Object *font_argument(lua_State *L, int index) {
  BwUiRuntime *r = current(L);
  if (lua_isnil(L, index))
    return NULL;
  Object *f = NULL;
  if (lua_type(L, index) == LUA_TSTRING)
    f = find(r, lua_tostring(L, index));
  else if (lua_istable(L, index))
    f = owner(L, index);
  else
    luaL_error(L, "expected font object, name or nil");
  if (!f || f->type != 13)
    luaL_error(L, "unknown/wrong font object");
  return f;
}
static Object *font_source(Object *o) {
  for (uint32_t i = 0; o && i < 128; i++, o = o->font_object)
    if (o->font_file)
      return o;
  return NULL;
}
static int same_path(const char *a, const char *b) {
  while (*a && *b) {
    char x = *a++, y = *b++;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (x == '\\')
      x = '/';
    if (y == '\\')
      y = '/';
    if (x != y)
      return 0;
  }
  return !*a && !*b;
}
static int object_font(Object *o, BwFont **out) {
  BwUiRuntime *r = o->runtime;
  Object *f = font_source(o);
  if (!f || !isfinite(f->font_height) || f->font_height < 1 || f->font_height > 128)
    return fail(r, "unavailable font descriptor/size");
  uint32_t pixels = (uint32_t)f->font_height;
  if (f->font_height != pixels)
    return fail(r, "fractional font sizing not reconstructed");
  FontAsset *asset = NULL;
  for (uint32_t i = 0; i < r->font_asset_count; i++)
    if (same_path(f->font_file, r->font_assets[i].path)) {
      asset = &r->font_assets[i];
      break;
    }
  if (!asset)
    return fail(r, "font asset not registered");
  BwFont *font = NULL;
  for (uint32_t i = 0; i < r->font_face_count; i++)
    if (r->font_faces[i].asset == asset && r->font_faces[i].pixels == pixels) {
      font = r->font_faces[i].font;
      break;
    }
  if (!font) {
    if (r->font_face_count >= 128)
      return fail(r, "font face capacity");
    font = bw_font_open(asset->bytes, asset->length, pixels);
    if (!font)
      return fail(r, "font backend rejected asset");
    r->font_faces[r->font_face_count++] = (FontFace){asset, pixels, font};
  }
  *out = font;
  return 0;
}
static int intrinsic(Object *o, double *out) {
  *out = 0;
  if (!o || !o->text || !o->text[0])
    return 0;
  BwUiRuntime *r = o->runtime;
  Object *f = font_source(o);
  BwFont *font;
  if (object_font(o, &font))
    return 1;
  uint32_t outline = strstr(f->font_flags, "THICK") ? 4 : strstr(f->font_flags, "OUTLINE") ? 2 : 0;
  Object *shadow = o->has_shadow ? o : f;
  float width;
  if (bw_font_text_extent(font, (const uint8_t *)o->text, (uint32_t)strlen(o->text), outline,
                          (float)shadow->shadow_x, 1, &width))
    return fail(r, "unsupported font text extent contract");
  *out = width;
  return 0;
}
/* Native-backed selected normal fitter. Positive-height ellipsis is a
 * separate branch and is rejected until its output contract is reconstructed. */
/* Resolve horizontal anchor equations before measuring wrapped text. Asking
 * for the complete rectangle here would recurse into natural text height. */
static float text_layout_width(Object *o) {
  if (o->parent && o->parent->edit_display == o)
    return 0;
  double first_fraction = 0, first_position = 0;
  int found = 0;
  for (uint32_t i = 0; i < o->point_count; i++) {
    Point *p = &o->points[i];
    Object *target = p->relative ? p->relative : o->parent;
    if (p->relative_name[0])
      target = find(o->runtime, p->relative_name);
    if (!target || target == o || target->solving || p->point > 8 || p->relative_point > 8)
      continue;
    BwAnchorRect relative;
    if (rectangle(target, &relative, 0))
      continue;
    double fraction = (p->point % 3) * 0.5;
    double position = relative.left + (p->relative_point % 3) * 0.5 * relative.width + p->x;
    if (!found) {
      first_fraction = fraction;
      first_position = position;
      found = 1;
    } else if (fraction != first_fraction) {
      double width = (position - first_position) / (fraction - first_fraction);
      if (isfinite(width) && width > 0)
        return (float)width;
    }
  }
  return o->width_known ? (float)o->width : 0;
}
static int object_layout(Object *o, BwTextLine *lines, uint32_t capacity, BwTextLayout *out) {
  Object *f = font_source(o);
  memset(out, 0, sizeof(*out));
  if (!f || !o->text || !o->text[0])
    return 0;
  BwFont *font;
  if (object_font(o, &font))
    return 1;
  uint32_t outline = strstr(f->font_flags, "THICK") ? 4 : strstr(f->font_flags, "OUTLINE") ? 2 : 0;
  Object *shadow = o->has_shadow ? o : f;
  float width = text_layout_width(o), height = (float)(f->font_height + o->font_spacing),
        shadow_x = (float)shadow->shadow_x;
  if (o->layout_lines && o->layout_version == o->text_version && o->layout_font == font &&
      o->layout_outline == outline && o->layout_width == width && o->layout_height == height &&
      o->layout_shadow == shadow_x && o->layout_nominal_height == o->height &&
      o->layout_height_known == o->height_known) {
    if (capacity < o->layout_result.lines)
      return fail(o->runtime, "text layout capacity");
    memcpy(lines, o->layout_lines, o->layout_result.lines * sizeof(*lines));
    *out = o->layout_result;
    return 0;
  }
  uint32_t status = bw_font_text_layout(font, (const uint8_t *)o->text, (uint32_t)strlen(o->text),
                                        outline, shadow_x, 1, height, width, lines, capacity, out);
  if (status) {
    char message[256];
    snprintf(message, sizeof(message),
             "text line layout rejected (status=%u, object=%.96s, width=%.2f, bytes=%zu)", status,
             o->name, (double)width, strlen(o->text));
    return fail(o->runtime, message);
  }
  if (out->lines)
    out->height -= (float)o->font_spacing;
  o->layout_ellipsis = 0;
  if (o->height_known && o->height > 0 && o->height + .002 < out->height && out->lines > 1) {
    int options = 0;
    if (o->runtime->options_readonly)
      for (Object *p = o; p; p = p->parent)
        if (!strcmp(p->name, "VideoOptionsFrame") || !strcmp(p->name, "AudioOptionsFrame"))
          options = 1;
    if (!options)
      return fail(o->runtime, "positive-height text ellipsis not reconstructed");
    /* Read-only options labels: fit a final ellipsis instead of wrapping outside
   * the source's fixed-height dropdown. Native ellipsis pixels remain separate. */
    uint32_t visible = (uint32_t)(o->height / height);
    if (!visible)
      visible = 1;
    if (visible > out->lines)
      visible = out->lines;
    BwTextLine *last = &lines[visible - 1];
    uint32_t end = last->start + last->length;
    char *shortened = malloc((size_t)end - last->start + 4);
    if (!shortened)
      return fail(o->runtime, "ellipsis allocation");
    int fitted = 0;
    for (;;) {
      uint32_t n = end - last->start;
      memcpy(shortened, o->text + last->start, n);
      memcpy(shortened + n, "...", 4);
      float extent;
      if (!bw_font_text_extent(font, (const uint8_t *)shortened, n + 3, outline, shadow_x, 1,
                               &extent) &&
          extent <= width) {
        last->length = n;
        last->width = extent;
        fitted = 1;
        break;
      }
      if (end == last->start)
        break;
      end--;
      while (end > last->start && (((uint8_t)o->text[end] & 0xc0) == 0x80))
        end--;
    }
    free(shortened);
    if (!fitted)
      return fail(o->runtime, "ellipsis width capacity");
    out->lines = visible;
    out->height = visible * height;
    o->layout_ellipsis = 1;
  }
  BwTextLine *cached = malloc((out->lines ? out->lines : 1) * sizeof(*cached));
  if (!cached)
    return fail(o->runtime, "text layout cache allocation");
  memcpy(cached, lines, out->lines * sizeof(*cached));
  free(o->layout_lines);
  o->layout_lines = cached;
  o->layout_result = *out;
  o->layout_version = o->text_version;
  o->layout_font = font;
  o->layout_outline = outline;
  o->layout_width = width;
  o->layout_height = height;
  o->layout_shadow = shadow_x;
  o->layout_nominal_height = o->height;
  o->layout_height_known = o->height_known;
  return 0;
}
static int natural_height(Object *o, double *out) {
  *out = 0;
  if (!o->text || !o->text[0])
    return 0;
  uint32_t capacity = (uint32_t)strlen(o->text) + 1;
  if (capacity > 4096)
    capacity = 4096;
  BwTextLine *lines = calloc(capacity, sizeof(*lines));
  if (!lines)
    return fail(o->runtime, "text layout allocation");
  BwTextLayout layout;
  int result = object_layout(o, lines, capacity, &layout);
  if (!result)
    *out = layout.height;
  free(lines);
  return result;
}
static void center_button_text(Object *o, Object *t) {
  t->point_count = 1;
  t->points[0] = (Point){0};
  t->points[0].point = t->points[0].relative_point = 4;
  t->points[0].relative = o;
}
static int replace_text(Object *o, const char *text) {
  if (!text)
    text = "";
  if (strlen(text) > 1048576)
    return fail(o->runtime, "text capacity");
  if (!strcmp(o->text ? o->text : "", text))
    return 0;
  char *p = copy(text);
  if (!p)
    return fail(o->runtime, "text allocation");
  free(o->text);
  o->text = p;
  o->text_version++;
  if (!o->text_version)
    o->text_version = 1;
  return 0;
}
static int edit_publish(Object *o, int changed) {
  BwUiRuntime *r = o->runtime;
  if (replace_text(o, o->edit->bytes))
    return 1;
  if (o->edit_display) {
    char masked[BW_EDIT_CAPACITY + 1];
    const char *display = o->edit->bytes;
    if (o->edit_password) {
      memset(masked, '*', o->edit->length);
      masked[o->edit->length] = 0;
      display = masked;
    }
    if (replace_text(o->edit_display, display))
      return 1;
  }
  if (changed)
    o->edit_dirty = 1;
  (void)r;
  return 0;
}
static int edit_set(Object *o, const char *text) {
  if (!text)
    text = "";
  uint32_t n = (uint32_t)strlen(text);
  if (n > BW_EDIT_CAPACITY)
    return fail(o->runtime, "edit text capacity");
  if (!strcmp(o->edit->bytes, text))
    return 0;
  char before[BW_EDIT_CAPACITY + 1];
  strcpy(before, o->edit->bytes);
  o->edit->limit = o->max_letters > 0 ? (uint32_t)o->max_letters : 0;
  if (bw_edit_set(o->edit, text, n))
    return fail(o->runtime, "unsupported edit text bytes");
  if (!strcmp(before, o->edit->bytes))
    return 0;
  if (edit_publish(o, 1))
    return 1;
  return invoke(o, "OnTextSet");
}
static int edit_insert(Object *o, const char *text, uint32_t n, int user) {
  char before[BW_EDIT_CAPACITY + 1];
  strcpy(before, o->edit->bytes);
  o->edit->limit = o->max_letters > 0 ? (uint32_t)o->max_letters : 0;
  if (bw_edit_insert(o->edit, text, n))
    return fail(o->runtime, "unsupported edit insertion");
  int changed = strcmp(before, o->edit->bytes) != 0;
  if (edit_publish(o, changed))
    return 1;
  if (changed && user)
    o->edit_user = 1;
  uint32_t id = event_id(o, "OnChar");
  lua_pushlstring(o->runtime->L, text, n);
  return bw_ui_call_script(o->runtime->L, &o->scripts[id], o->reference, 1, o->runtime->error,
                           sizeof(o->runtime->error));
}
/* Native-owned paragraph construction observed by simplehtml-trace. Deferred
 * HTML height/position remains separate; no browser DOM layout participates. */
static int html_text(Object *o, const char *text) {
  BwUiRuntime *r = o->runtime;
  if (o->html_loaded && !strcmp(o->text ? o->text : "", text ? text : ""))
    return 0;
  if (!o->font_object && !font_source(o))
    return fail(r, "SimpleHTML font not set");
  BwHtmlDocument doc;
  if (bw_html_parse(text ? text : "", &doc))
    return fail(r, "unsupported SimpleHTML grammar");
  uint32_t reusable = 0;
  for (uint32_t i = 0; i < r->count; i++)
    if (r->objects[i]->parent == o && !strcmp(r->objects[i]->tag, "FontString")) {
      r->objects[i]->shown = 0;
      reusable++;
    }
  if (doc.count > MAX_OBJECTS - r->count + reusable) {
    bw_html_free(&doc);
    return fail(r, "SimpleHTML region capacity");
  }
  uint32_t reuse = 0, oldCount = r->count;
  double paragraph_offset = 0;
  for (uint32_t i = 0; i < doc.count; i++) {
    BwHtmlParagraph *p = &doc.paragraphs[i];
    Object *t = NULL;
    while (reuse < oldCount) {
      Object *c = r->objects[reuse++];
      if (c->parent == o && !strcmp(c->tag, "FontString")) {
        t = c;
        break;
      }
    }
    if (!t)
      t = create(r, "FontString", NULL, o);
    if (!t) {
      bw_html_free(&doc);
      return 1;
    }
    t->shown = 1;
    t->height = 0;
    t->height_known = 0;
    free(t->text);
    t->text = NULL;
    t->text_version++;

    t->font_object = o->html_fonts[p->heading] ? o->html_fonts[p->heading] : o->font_object;
    t->width = o->width;
    t->width_known = o->width_known;
    t->text = p->text;
    p->text = NULL;
    /* Source-defined line spacing also separates adjacent HTML regions.
   * Glyph raster size remains unchanged; only their baselines/anchors move. */
    Object *style = font_source(t);
    const char *spacing = style ? attr(style->composed, "spacing") : NULL;
    t->font_spacing = 0;
    if (spacing) {
      char *end;
      t->font_spacing = strtod(spacing, &end);
      if (*end || !isfinite(t->font_spacing) || t->font_spacing < 0 || t->font_spacing > 128) {
        bw_html_free(&doc);
        return fail(r, "invalid SimpleHTML font spacing");
      }
    }
    /* Anchor directly to the HTML container: a credits document has over
   * a thousand sibling paragraphs and must not build a recursive anchor chain. */
    t->point_count = 1;
    t->points[0] = (Point){0};
    t->points[0].point = 6;
    t->points[0].relative_point = 6;
    t->points[0].relative = o;
    t->points[0].y = -paragraph_offset;
    strcpy(t->justify_h, p->align);
    if (!strcmp(o->name, "CreditsText") && !p->align_explicit) {
      const char *alignment = style ? attr(style->composed, "justifyH") : NULL;
      strcpy(t->justify_h, alignment ? alignment : "CENTER");
    }
    strcpy(t->justify_v, "TOP");
    if (!t->text[0]) {
      t->height = p->break_line && style ? style->font_height : 1;
      t->height_known = 1;
    }
    double paragraph_height = t->height_known ? t->height : 1;
    if (t->text[0] && natural_height(t, &paragraph_height)) {
      bw_html_free(&doc);
      return 1;
    }
    /* Paragraph width and font are fixed for this parsed document. Preserve
     * the measured height instead of allocating a layout buffer every frame. */
    t->height = paragraph_height;
    t->height_known = 1;
    paragraph_offset += paragraph_height + t->font_spacing;
  }
  bw_html_free(&doc);
  o->html_loaded = 1;
  return replace_text(o, text);
}
/* Selected native Button::Click:0x978260 -> vtable+e8 ->0x96fd70
 * guards disabled/reentrant state;0x96f090 calls Pre/On/Post with button,bool. */
static int click_button(Object *o, const char *button, int down) {
  if (o->type != BW_UI_BUTTON && o->type != BW_UI_CHECKBUTTON)
    return fail(o->runtime, "selected Click supports Button/CheckButton only");
  /* CheckButton vtable a9f988+e8 ->9623c0 toggles before Button::Click. */
  if (o->type == BW_UI_CHECKBUTTON)
    o->checked = !o->checked;
  if (!o->enabled || o->click_busy)
    return 0;
  if (!button)
    button = "LeftButton";
  if (strlen(button) > 63)
    return fail(o->runtime, "click button name capacity");
  o->click_busy = 1;
  const char *events[] = {"PreClick", "OnClick", "PostClick"};
  int status = 0;
  for (uint32_t i = 0; i < 3; i++) {
    uint32_t id = event_id(o, events[i]);
    if (!id)
      continue;
    lua_pushstring(o->runtime->L, button);
    lua_pushboolean(o->runtime->L, down);
    status = bw_ui_call_script(o->runtime->L, &o->scripts[id], o->reference, 2, o->runtime->error,
                               sizeof(o->runtime->error));
    if (status)
      break;
  }
  o->click_busy = 0;
  return status;
}
static int native_flag(lua_State *L, int index, int fallback) {
  int type = lua_type(L, index);
  if (type == LUA_TNONE)
    return fallback;
  if (type == LUA_TNIL)
    return 0;
  if (type == LUA_TBOOLEAN)
    return lua_toboolean(L, index);
  if (type == LUA_TNUMBER) {
    double v = lua_tonumber(L, index);
    if (!isfinite(v) || v < (-2147483647.0 - 1) || v > 2147483647.0)
      luaL_error(L, "unsupported flag conversion");
    return (int)v != 0;
  }
  if (type == LUA_TSTRING) {
    const char *s = lua_tostring(L, index);
    char c = *s;
    if (c == '0' || c == 'F' || c == 'f' || c == 'N' || c == 'n')
      return 0;
    if ((c >= '1' && c <= '9') || c == 'T' || c == 't' || c == 'Y' || c == 'y')
      return 1;
    if (!strcmp(s, "off") || !strcmp(s, "disabled"))
      return 0;
    if (!strcmp(s, "on") || !strcmp(s, "enabled"))
      return 1;
  }
  return fallback;
}
static int set_focus(BwUiRuntime *r, Object *next) {
  Object *old = r->focus;
  if (old == next || (next && !visible(next)))
    return 0;
  if (next) {
    if (old && invoke(old, "OnEditFocusLost"))
      return 1;
    r->focus = next;
    return invoke(next, "OnEditFocusGained");
  }
  r->focus = NULL;
  return old ? invoke(old, "OnEditFocusLost") : 0;
}
static int slider_value(Object *o, float requested) {
  if (!o->range_initialized)
    return 0;
  float maximum = o->minimum + o->range, v = requested;
  if (v < o->minimum)
    v = o->minimum;
  if (v > maximum)
    v = maximum;
  if (o->step > 0) {
    double steps = floor(((double)v - o->minimum) / o->step + 0.5);
    v = (float)(o->minimum + steps * o->step);
    if (maximum < (double)v + o->step)
      v = maximum;
    if ((double)v - o->step < o->minimum)
      v = o->minimum;
  }
  if (o->value_initialized && fabs((double)v - o->value) <= 2.384185791015625e-7)
    return 0;
  o->value = v;
  o->value_initialized = 1;
  if (o->runtime->deferred_lifecycle && !o->loaded)
    return 0;
  uint32_t id = event_id(o, "OnValueChanged");
  if (!id)
    return 0;
  lua_State *L = o->runtime->L;
  lua_pushnumber(L, v);
  return bw_ui_call_script(L, &o->scripts[id], o->reference, 1, o->runtime->error,
                           sizeof(o->runtime->error));
}
static int instantiate(BwUiRuntime *, const BwXmlNode *, Object *, uint32_t);
static int attach_scroll(Object *o, Object *c) {
  for (Object *p = o; p; p = p->parent)
    if (p == c)
      return fail(o->runtime, "scroll child cycle");
  if (!frame_type(c->type))
    return fail(o->runtime, "scroll child must be frame");
  if (o->scroll_child && o->scroll_child != c) {
    o->scroll_child->parent = NULL;
    o->scroll_child->point_count = 0;
    o->scroll_child->all_points = 0;
  }
  o->scroll_child = c;
  c->parent = o;
  c->all_points = c->generated_points = 0;
  c->point_count = 1;
  memset(c->points, 0, sizeof(c->points));
  c->points[0].point = c->points[0].relative_point = 6;
  c->points[0].relative = o;
  c->points[0].x = -o->scroll_x;
  c->points[0].y = o->scroll_y;
  return 0;
}
static int method(lua_State *L) {
  Object *o = owner(L, 1);
  BwUiRuntime *r = o->runtime;
  const char *m = lua_tostring(L, lua_upvalueindex(2));
  if (!strcmp(m, "StartMovie")) {
    const char *path = luaL_checkstring(L, 2);
    double volume = luaL_checknumber(L, 3);
    int status = bw_movie_start(r->movie, o->id + 1, path, volume);
    if (status < 0)
      return luaL_error(L, "movie request rejected");
    return numeric_flag(L, status);
  }
  if (!strcmp(m, "StopMovie")) {
    int stopped = bw_movie_stop(r->movie, o->id + 1);
    if (stopped < 0)
      return luaL_error(L, "movie stop rejected");
    if (stopped && invoke(o, "OnMovieFinished"))
      return luaL_error(L, "movie completion handler failed");
    return 0;
  }
  if (!strcmp(m, "EnableSubtitles")) {
    if (bw_movie_subtitles(r->movie, o->id + 1, native_flag(L, 2, 0)) < 0)
      return luaL_error(L, "movie subtitles rejected");
    return 0;
  }
  if (!strcmp(m, "CreateFontString") || !strcmp(m, "CreateTexture")) {
    const char *region_type = !strcmp(m, "CreateTexture") ? "Texture" : "FontString";
    if (!strcmp(m, "CreateTexture") && lua_gettop(L) > 4)
      return luaL_error(L, "texture sublayer branch not reconstructed");
    if (!frame_type(o->type))
      return luaL_error(L, "region owner must be a frame");
    const char *name = luaL_optstring(L, 2, "");
    const char *layer = luaL_optstring(L, 3, "ARTWORK");
    uint32_t draw = !strcmp(layer, "BACKGROUND")  ? 1
                    : !strcmp(layer, "BORDER")    ? 2
                    : !strcmp(layer, "ARTWORK")   ? 3
                    : !strcmp(layer, "OVERLAY")   ? 4
                    : !strcmp(layer, "HIGHLIGHT") ? 5
                                                  : 0;
    if (!draw || strlen(name) >= MAX_NAME)
      return luaL_error(L, "invalid region name/layer");
    Object *t = NULL;
    if (!lua_isnoneornil(L, 4) && *luaL_checkstring(L, 4)) {
      BwXmlAttribute inherits = {"inherits", (char *)lua_tostring(L, 4), NULL},
                     named = {"name", (char *)name, &inherits};
      BwXmlNode raw = {(char *)region_type, NULL, &named, NULL, NULL};
      uint32_t first = r->count;
      if (instantiate(r, &raw, o, 0))
        return luaL_error(L, "%s", r->error);
      t = r->objects[first];
    } else
      t = create(r, region_type, name, o);
    if (!t)
      return luaL_error(L, "%s", r->error);
    t->draw_layer = draw;
    lua_rawgeti(L, LUA_REGISTRYINDEX, t->reference);
    return 1;
  }
  if (!strcmp(m, "GetName")) {
    if (o->name[0])
      lua_pushstring(L, o->name);
    else
      lua_pushnil(L);
    return 1;
  }
  if (!strcmp(m, "GetObjectType")) {
    lua_pushstring(L, o->type == BW_UI_MODELFFX ? "Model" : o->tag);
    return 1;
  }
  if (!strcmp(m, "EnableKeyboard") || !strcmp(m, "EnableMouse")) {
    uint32_t mask = !strcmp(m, "EnableKeyboard") ? 3 : 4;
    if (native_flag(L, 2, 1))
      o->input_flags |= mask;
    else
      o->input_flags &= ~mask;
    return 0;
  }
  if (!strcmp(m, "IsKeyboardEnabled") || !strcmp(m, "IsMouseEnabled")) {
    uint32_t mask = !strcmp(m, "IsKeyboardEnabled") ? 3 : 4;
    if (o->input_flags & mask)
      lua_pushnumber(L, 1);
    else
      lua_pushnil(L);
    return 1;
  }
  /* Selected native Raise walks to an XML toplevel ancestor (4951a8..d4).
  * Modal ordering uses the existing level propagation; overlap-sensitive native
  * grouping is not represented by this bounded portable compositor. Lower's
  * selected native destination4899f0 is an explicit no-op. */
  if (!strcmp(m, "Lower"))
    return 0;
  if (!strcmp(m, "Raise")) {
    Object *top = o;
    while (top) {
      const char *flag = attr(top->composed, "toplevel");
      if (flag && (!strcmp(flag, "true") || !strcmp(flag, "1")))
        break;
      top = top->parent;
    }
    if (!top)
      return 0;
    int level = top->frame_level;
    for (uint32_t i = 0; i < r->count; i++) {
      Object *peer = r->objects[i];
      if (!frame_type(peer->type) || peer->frame_stratum != top->frame_stratum || !visible(peer))
        continue;
      Object *branch = peer;
      while (branch && branch != top)
        branch = branch->parent;
      if (!branch && peer->frame_level >= level) {
        if (peer->frame_level == 2147483647)
          return luaL_error(L, "frame raise capacity");
        level = peer->frame_level + 1;
      }
    }
    if (set_level(top, level, 0))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "GetFrameLevel")) {
    lua_pushnumber(L, o->frame_level);
    return 1;
  }
  if (!strcmp(m, "SetFrameLevel")) {
    double value = luaL_checknumber(L, 2);
    if (!isfinite(value) || value < (-2147483647.0 - 1) || value > 2147483647.0 || (int)value < 0)
      return luaL_error(L, "invalid frame level for %s: %f", o->name, value);
    if (set_level(o, (int)value, 0))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "GetFrameStrata")) {
    lua_pushstring(L, strata_names[o->frame_stratum - 1]);
    return 1;
  }
  if (!strcmp(m, "SetFrameStrata")) {
    int value = stratum(luaL_checkstring(L, 2));
    if (!value)
      return luaL_error(L, "unsupported frame stratum");
    if (set_stratum(o, value, 0))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "SetAllPoints")) {
    Object *target = o->parent;
    if (lua_istable(L, 2))
      target = owner(L, 2);
    else if (lua_type(L, 2) == LUA_TSTRING) {
      target = find(r, lua_tostring(L, 2));
      if (!target)
        return luaL_error(L, "unknown all-points target");
    } else if (!lua_isnoneornil(L, 2))
      return luaL_error(L, "unsupported all-points target");
    if (target == o)
      return luaL_error(L, "self all-points target");
    all_points(o, target);
    return 0;
  }
  if (!strcmp(m, "GetTexture")) {
    if (o->texture_file)
      lua_pushstring(L, o->texture_file);
    else
      lua_pushnil(L);
    return 1;
  }
  if (!strcmp(m, "SetTexture")) {
    if (lua_isnoneornil(L, 2)) {
      free(o->texture_file);
      o->texture_file = NULL;
      lua_pushnumber(L, 1);
      return 1;
    }
    if (lua_type(L, 2) != LUA_TSTRING || lua_isnumber(L, 2) || lua_gettop(L) != 2)
      return luaL_error(L, "unsupported texture assignment branch");
    const char *path = lua_tostring(L, 2);
    TextureAsset *asset = NULL;
    for (uint32_t i = 0; i < r->texture_asset_count; i++)
      if (same_path(path, r->texture_assets[i].path)) {
        asset = &r->texture_assets[i];
        break;
      }
    if (!asset)
      return luaL_error(L, "texture asset not admitted: %s", path);
    if (!asset->width && !asset->height) {
      free(o->texture_file);
      o->texture_file = NULL;
      lua_pushnil(L);
      return 1;
    }
    char *file = copy(asset->path);
    if (!file)
      return luaL_error(L, "texture assignment allocation");
    free(o->texture_file);
    o->texture_file = file;
    lua_pushnumber(L, 1);
    return 1;
  }
  if (!strcmp(m, "GetNormalTexture") || !strcmp(m, "GetPushedTexture") ||
      !strcmp(m, "GetDisabledTexture") || !strcmp(m, "GetHighlightTexture")) {
    int slot = !strcmp(m, "GetNormalTexture")     ? 0
               : !strcmp(m, "GetPushedTexture")   ? 1
               : !strcmp(m, "GetDisabledTexture") ? 2
                                                  : 3;
    Object *t = o->button_textures[slot];
    if (t)
      lua_rawgeti(L, LUA_REGISTRYINDEX, t->reference);
    else
      lua_pushnil(L);
    return 1;
  }
  if (!strcmp(m, "SetNormalTexture") || !strcmp(m, "SetPushedTexture") ||
      !strcmp(m, "SetDisabledTexture") || !strcmp(m, "SetHighlightTexture")) {
    int slot = !strcmp(m, "SetNormalTexture")     ? 0
               : !strcmp(m, "SetPushedTexture")   ? 1
               : !strcmp(m, "SetDisabledTexture") ? 2
                                                  : 3;
    Object *texture = o->button_textures[slot];
    const char *path = lua_isnoneornil(L, 2) ? NULL : luaL_checkstring(L, 2);
    TextureAsset *asset = NULL;
    for (uint32_t i = 0; path && i < r->texture_asset_count; i++)
      if (same_path(path, r->texture_assets[i].path)) {
        asset = &r->texture_assets[i];
        break;
      }
    if (path && !asset)
      return luaL_error(L, "texture asset not admitted: %s", path);
    if (!texture && path) {
      texture = create(r, "Texture", NULL, o);
      if (!texture)
        return luaL_error(L, "%s", r->error);
      all_points(texture, o);
      texture->draw_layer = slot == 3 ? 3 : 2;
      o->button_textures[slot] = texture;
    }
    if (texture) {
      char *file = path ? copy(asset->path) : NULL;
      if (path && !file)
        return luaL_error(L, "button texture allocation");
      free(texture->texture_file);
      texture->texture_file = file;
    }
    return 0;
  }
  if (!strcmp(m, "SetDesaturated")) {
    o->desaturated = lua_toboolean(L, 2);
    lua_pushboolean(L, 1);
    return 1;
  }
  if (!strcmp(m, "GetModelScale")) {
    lua_pushnumber(L, o->model_scale);
    return 1;
  }
  if (!strcmp(m, "AdvanceTime")) {
    o->model_advance_requested++;
    return 0;
  }
  if (!strcmp(m, "ClearFog")) {
    o->model_fog_enabled = 0;
    return 0;
  }
  if (!strcmp(m, "SetFogColor")) {
    for (uint32_t i = 0; i < 3; i++) {
      double value = luaL_checknumber(L, 2 + i);
      if (!isfinite(value) || value < 0 || value > 1)
        return luaL_error(L, "invalid model fog color");
      o->model_fog[i] = (float)value;
    }
    o->model_fog_enabled = 1;
    return 0;
  }
  if (!strcmp(m, "SetFogNear") || !strcmp(m, "SetFogFar") || !strcmp(m, "SetGlow")) {
    double value = luaL_checknumber(L, 2);
    if (!isfinite(value) || value < 0 || value > 1000000)
      return luaL_error(L, "invalid model environment parameter");
    if (!strcmp(m, "SetGlow"))
      o->model_glow = (float)value;
    else {
      o->model_fog[!strcmp(m, "SetFogNear") ? 3 : 4] = (float)value;
      o->model_fog_enabled = 1;
    }
    return 0;
  }
  if (!strcmp(m, "ResetLights")) {
    memset(o->model_light_count, 0, sizeof(o->model_light_count));
    memset(o->model_lights, 0, sizeof(o->model_lights));
    return 0;
  }
  if (!strcmp(m, "AddLight") || !strcmp(m, "AddCharacterLight") || !strcmp(m, "AddPetLight")) {
    int state = luaL_checkinteger(L, 2);
    if (state < 0 || state > 1)
      return luaL_error(L, "invalid model lighting state");
    uint32_t group = (uint32_t)state + (!strcmp(m, "AddCharacterLight") ? 2
                                        : !strcmp(m, "AddPetLight")     ? 4
                                                                        : 0);
    uint32_t index = o->model_light_count[group];
    if (index >= 4)
      return luaL_error(L, "model lighting capacity");
    float values[13];
    for (uint32_t i = 0; i < 14; i++) {
      double value = luaL_checknumber(L, 3 + i);
      if (!isfinite(value) || fabs(value) > 1000000)
        return luaL_error(L, "invalid model lighting parameter");
      values[i] = (float)value;
    }
    memcpy(o->model_lights[group][index], values, sizeof(values));
    o->model_light_count[group]++;
    return 0;
  }
  if (!strcmp(m, "SetModelScale")) {
    float value = (float)luaL_checknumber(L, 2);
    if (!isfinite(value) || fabsf(value) > 1000000)
      return luaL_error(L, "unsupported model scale");
    if (r->model_count >= 512)
      return luaL_error(L, "model request capacity");
    o->model_scale = value;
    BwUiModelRequest request = {0};
    request.object = o->id;
    request.operation = BW_UI_MODEL_SCALE;
    request.scalar = value;
    r->model_requests[(r->model_head + r->model_count) % 512] = request;
    r->model_count++;
    return 0;
  }
  if (!strcmp(m, "SetSequenceTime")) {
    double sequence = luaL_checknumber(L, 2), time = luaL_checknumber(L, 3);
    if (!isfinite(sequence) || sequence < 0 || sequence >= 506 || !isfinite(time) || time < 0 ||
        time > 4294967295.0)
      return luaL_error(L, "unsupported sequence time range");
    if (r->model_count >= 512)
      return luaL_error(L, "model request capacity");
    BwUiModelRequest request = {0};
    request.object = o->id;
    request.operation = BW_UI_MODEL_SEQUENCE_TIME;
    request.value = (uint32_t)sequence;
    request.time_ms = (uint32_t)time;
    r->model_requests[(r->model_head + r->model_count) % 512] = request;
    r->model_count++;
    return 0;
  }
  if (!strcmp(m, "SetCamera") || !strcmp(m, "SetSequence") || !strcmp(m, "SetModel")) {
    if (r->model_count >= 512)
      return luaL_error(L, "model request capacity");
    BwUiModelRequest request = {0};
    request.object = o->id;
    if (!strcmp(m, "SetModel")) {
      const char *path = luaL_checkstring(L, 2);
      if (strlen(path) >= sizeof(request.path))
        return luaL_error(L, "model path capacity");
      int found = 0;
      for (uint32_t i = 0; i < r->model_asset_count; i++)
        if (same_path(r->model_assets[i], path)) {
          found = 1;
          break;
        }
      if (!found)
        return luaL_error(L, "model resource not supplied");
      request.operation = BW_UI_MODEL_FILE;
      strcpy(request.path, path);
      strcpy(o->model_path, path);
      o->model_pending = 1;
    } else {
      double value = luaL_checknumber(L, 2);
      if (!isfinite(value) || value < 0 || value > 4294967295.0)
        return luaL_error(L, "model index range");
      request.value = (uint32_t)value;
      if (!strcmp(m, "SetSequence")) {
        if (request.value >= 506)
          return luaL_error(L, "sequence index range");
        request.operation = BW_UI_MODEL_SEQUENCE;
        o->sequence = request.value;
      } else {
        request.operation = BW_UI_MODEL_CAMERA;
        o->camera = request.value;
      }
    }
    r->model_requests[(r->model_head + r->model_count) % 512] = request;
    r->model_count++;
    return 0;
  }
  if (!strcmp(m, "GetID")) {
    lua_pushnumber(L, o->user_id);
    return 1;
  }
  if (!strcmp(m, "SetID")) {
    double v = luaL_checknumber(L, 2);
    if (!isfinite(v) || v < (-2147483647.0 - 1) || v > 2147483647.0)
      return luaL_error(L, "invalid frame ID");
    o->user_id = (int)v;
    return 0;
  }
  if (!strcmp(m, "GetParent")) {
    if (o->parent)
      lua_rawgeti(L, LUA_REGISTRYINDEX, o->parent->reference);
    else
      lua_pushnil(L);
    return 1;
  }
  if (!strcmp(m, "IsShown"))
    return numeric_flag(L, o->shown);
  if (!strcmp(m, "IsVisible"))
    return numeric_flag(L, visible(o));
  if (!strcmp(m, "LockHighlight") || !strcmp(m, "UnlockHighlight")) {
    o->highlight_locked = !strcmp(m, "LockHighlight");
    return 0;
  }
  if (!strcmp(m, "GetChecked"))
    return numeric_flag(L, o->checked);
  if (!strcmp(m, "GetButtonState")) {
    const char *state = !o->enabled ? "DISABLED" : o->pressed ? "PUSHED" : "NORMAL";
    lua_pushstring(L, state);
    return 1;
  }
  if (!strcmp(m, "SetButtonState")) {
    const char *state = luaL_checkstring(L, 2);
    if (strcmp(state, "NORMAL") && strcmp(state, "PUSHED"))
      return luaL_error(L, "unsupported button state: %s", state);
    o->pressed = !strcmp(state, "PUSHED");
    if (!o->pressed) {
      if (r->pressed_button == o)
        r->pressed_button = NULL;
    } else {
      r->pressed_button = o;
    }
    return 0;
  }
  if (!strcmp(m, "Click")) {
    if (click_button(o, lua_isnoneornil(L, 2) ? NULL : luaL_checkstring(L, 2),
                     native_flag(L, 3, 0)))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "RegisterForClicks")) {
    uint64_t mask = 0;
    for (int i = 2; i <= lua_gettop(L); i++) {
      const char *v = lua_tostring(L, i);
      if (!v)
        break;
      const char *names[] = {"LeftButtonDown", "LeftButtonUp",    "MiddleButtonDown",
                             "MiddleButtonUp", "RightButtonDown", "RightButtonUp"};
      int found = 0;
      for (int j = 0; j < 6; j++)
        if (!strcmp(v, names[j])) {
          mask |= (uint64_t)1 << (j % 2 ? 31 + j / 2 : j / 2);
          found = 1;
          break;
        }
      if (!found)
        return luaL_error(L, "unsupported click registration: %s", v);
    }
    o->click_mask = mask;
    o->click_registered = 1;
    return 0;
  }
  if (!strcmp(m, "SetChecked")) {
    o->checked = native_flag(L, 2, 0);
    return 0;
  }
  if (!strcmp(m, "HasFocus")) {
    lua_pushboolean(L, r->focus == o);
    return 1;
  }
  if (!strcmp(m, "SetFocus") || !strcmp(m, "ClearFocus")) {
    if (!strcmp(m, "SetFocus")) {
      if (set_focus(r, o))
        return luaL_error(L, "%s", r->error);
    } else if (r->focus == o && set_focus(r, NULL))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "SetAutoFocus")) {
    o->auto_focus = native_flag(L, 2, 1);
    return 0;
  }
  if (!strcmp(m, "GetMaxLetters")) {
    lua_pushnumber(L, o->max_letters);
    return 1;
  }
  if (!strcmp(m, "SetMaxLetters")) {
    if (lua_gettop(L) != 2)
      return luaL_error(L, "SetMaxLetters requires one value");
    double v = lua_tonumber(L, 2);
    if (!isfinite(v) || v < (-2147483647.0 - 1) || v > 2147483647.0)
      return luaL_error(L, "invalid letter limit");
    o->max_letters = (int)v;
    return 0;
  }
  if (!strcmp(m, "IsEnabled")) { /* Button0x977170 pushes numeric0/1; Slider0x972050 pushes1/nil. */
    if (o->type == 2 || o->type == 3) {
      lua_pushnumber(L, o->enabled ? 1 : 0);
      return 1;
    }
    return numeric_flag(L, o->enabled);
  }
  if (!strcmp(m, "SetTexCoord")) {
    int count = lua_gettop(L) - 1;
    if (count != 4 && count != 8)
      return luaL_error(L, "SetTexCoord requires four/eight values");
    float v[8];
    for (int i = 0; i < count; i++) {
      v[i] = (float)lua_tonumber(L, i + 2);
      if (!isfinite(v[i]) || v[i] < -10000 || v[i] > 10000)
        return luaL_error(L, "invalid texture coordinate");
    }
    if (count == 4) {
      if (v[0] > v[1] || v[2] > v[3])
        return luaL_error(L, "unsupported reversed rectangle");
      float points[8] = {v[0], v[2], v[0], v[3], v[1], v[2], v[1], v[3]};
      memcpy(o->uv, points, sizeof(points));
    } else
      memcpy(o->uv, v, sizeof(v));
    return 0;
  }
  if (!strcmp(m, "GetTexCoord")) {
    for (int i = 0; i < 8; i++)
      lua_pushnumber(L, o->uv[i]);
    return 8;
  }
  if (!strcmp(m, "GetJustifyH") || !strcmp(m, "GetJustifyV")) {
    lua_pushstring(L, !strcmp(m, "GetJustifyH") ? o->justify_h : o->justify_v);
    return 1;
  }
  if (!strcmp(m, "SetJustifyH") || !strcmp(m, "SetJustifyV")) {
    const char *value = luaL_checkstring(L, 2);
    int horizontal = !strcmp(m, "SetJustifyH");
    if (horizontal ? strcmp(value, "LEFT") && strcmp(value, "RIGHT") && strcmp(value, "CENTER")
                   : strcmp(value, "TOP") && strcmp(value, "BOTTOM") && strcmp(value, "MIDDLE"))
      return luaL_error(L, "invalid text justification");
    strcpy(horizontal ? o->justify_h : o->justify_v, value);
    return 0;
  }
  if (!strcmp(m, "GetStringWidth") || !strcmp(m, "GetTextWidth")) {
    Object *source = !strcmp(m, "GetTextWidth") ? o->button_text : o;
    double width = 0;
    if (source) {
      if (!strcmp(m, "GetTextWidth") && source->width_known && source->width > 0)
        width = source->width;
      else {
        double natural;
        if (source->height_known && source->height > 0 && natural_height(source, &natural))
          return luaL_error(L, "%s", r->error);
        if (intrinsic(source, &width))
          return luaL_error(L, "%s", r->error);
      }
    }
    lua_pushnumber(L, width);
    return 1;
  }
  if (!strcmp(m, "GetWidth") && o->type == 12 && (!o->width_known || o->width == 0) &&
      !o->all_points && o->point_count < 2) {
    double width;
    if (intrinsic(o, &width))
      return luaL_error(L, "%s", r->error);
    lua_pushnumber(L, width);
    return 1;
  }
  if (!strcmp(m, "GetStringHeight") ||
      (!strcmp(m, "GetHeight") && o->type == 12 && (!o->height_known || o->height == 0))) {
    double height;
    if (natural_height(o, &height))
      return luaL_error(L, "%s", r->error);
    lua_pushnumber(L, height);
    return 1;
  }
  if (!strcmp(m, "GetFontString")) {
    if (o->button_text)
      lua_rawgeti(L, LUA_REGISTRYINDEX, o->button_text->reference);
    else
      lua_pushnil(L);
    return 1;
  }
  if (!strcmp(m, "GetCursorPosition")) {
    lua_pushnumber(L, o->edit->cursor);
    return 1;
  }
  if (!strcmp(m, "SetCursorPosition")) {
    double v = luaL_checknumber(L, 2);
    if (!isfinite(v) || v < INT32_MIN || v > INT32_MAX || bw_edit_cursor(o->edit, (int32_t)v))
      return luaL_error(L, "invalid edit cursor");
    return 0;
  }
  if (!strcmp(m, "HighlightText")) {
    int32_t values[2] = {0, -1};
    for (int i = 0; i < 2; i++)
      if (!lua_isnoneornil(L, i + 2)) {
        double v = luaL_checknumber(L, i + 2);
        if (!isfinite(v) || v < INT32_MIN || v > INT32_MAX)
          return luaL_error(L, "invalid edit selection");
        values[i] = (int32_t)v;
      }
    if (bw_edit_select(o->edit, values[0], values[1]))
      return luaL_error(L, "invalid edit selection");
    return 0;
  }
  if (!strcmp(m, "Insert")) {
    size_t n;
    const char *text = luaL_checklstring(L, 2, &n);
    if (n > BW_EDIT_CAPACITY || edit_insert(o, text, (uint32_t)n, 0))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "GetText")) {
    Object *source = (o->type == 2 || o->type == 3) ? o->button_text : o;
    if (!source || !source->text || (!source->text[0] && (o->type == 2 || o->type == 3)))
      lua_pushnil(L);
    else
      lua_pushstring(L, source->text);
    if (o->type == 4 && lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushliteral(L, "");
    }
    return 1;
  }
  if (!strcmp(m, "SetFormattedText")) {
    char formatted[4096];
    bw_ui_format(L, 2, formatted, sizeof(formatted));
    Object *target = o;
    if (o->type == 2 || o->type == 3) {
      target = o->button_text;
      if (!target && formatted[0]) {
        target = create(r, "FontString", NULL, o);
        if (!target)
          return luaL_error(L, "%s", r->error);
        o->button_text = target;
        target->font_object = o->normal_font;
        center_button_text(o, target);
      }
      if (!target)
        return 0;
    }
    if (o->type == 12 && !font_source(o) && !o->font_object)
      return luaL_error(L, "Font not set");
    if (replace_text(target, formatted))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "SetText")) {
    if (o->type == BW_UI_SIMPLEHTML) {
      if (html_text(o, lua_tostring(L, 2)))
        return luaL_error(L, "%s", r->error);
      return 0;
    }
    if (o->type == 4 && !lua_isstring(L, 2))
      return luaL_error(L, "EditBox SetText requires string");
    const char *text = lua_tostring(L, 2);
    Object *target = o;
    if (o->type == 2 || o->type == 3) {
      target = o->button_text;
      if (!target && text && *text) {
        target = create(r, "FontString", NULL, o);
        if (!target)
          return luaL_error(L, "%s", r->error);
        o->button_text = target;
        target->font_object = o->normal_font;
        center_button_text(o, target);
      }
      if (!target)
        return 0;
    }
    if (o->type == 12 && !font_source(o) && !o->font_object)
      return luaL_error(L, "Font not set");
    if (o->type == BW_UI_EDITBOX ? edit_set(o, text) : replace_text(target, text))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "GetFont")) {
    Object *f = font_source(o);
    if (!f) {
      lua_pushnil(L);
      lua_pushnumber(L, 0);
      lua_pushliteral(L, "");
    } else {
      lua_pushstring(L, f->font_file);
      lua_pushnumber(L, f->font_height);
      lua_pushstring(L, f->font_flags);
    }
    return 3;
  }
  if (!strcmp(m, "SetFontObject") || !strcmp(m, "SetNormalFontObject") ||
      !strcmp(m, "SetHighlightFontObject") || !strcmp(m, "SetDisabledFontObject")) {
    Object *f = font_argument(L, 2);
    for (Object *p = f; p; p = p->font_object)
      if (p == o)
        return luaL_error(L, "font inheritance cycle");
    if (!strcmp(m, "SetFontObject"))
      o->font_object = f;
    else if (!strcmp(m, "SetNormalFontObject")) {
      o->normal_font = f;
      if (o->button_text)
        o->button_text->font_object = f;
    } else if (!strcmp(m, "SetHighlightFontObject"))
      o->highlight_font = f;
    else
      o->disabled_font = f;
    return 0;
  }
  if (!strcmp(m, "GetFontObject") || !strcmp(m, "GetNormalFontObject") ||
      !strcmp(m, "GetHighlightFontObject") || !strcmp(m, "GetDisabledFontObject")) {
    Object *f = !strcmp(m, "GetFontObject")            ? o->font_object
                : !strcmp(m, "GetNormalFontObject")    ? o->normal_font
                : !strcmp(m, "GetHighlightFontObject") ? o->highlight_font
                                                       : o->disabled_font;
    if (f)
      lua_rawgeti(L, LUA_REGISTRYINDEX, f->reference);
    else
      lua_pushnil(L);
    return 1;
  }
  if (!strcmp(m, "GetStatusBarTexture")) {
    if (o->bar_texture)
      lua_rawgeti(L, LUA_REGISTRYINDEX, o->bar_texture->reference);
    else
      lua_pushnil(L);
    return 1;
  }
  if (!strcmp(m, "SetStatusBarTexture")) {
    const char *path = luaL_checkstring(L, 2);
    if (!o->bar_texture) {
      o->bar_texture = create(r, "Texture", NULL, o);
      if (!o->bar_texture)
        return luaL_error(L, "%s", r->error);
      all_points(o->bar_texture, o);
    }
    char *replacement = copy(path);
    if (!replacement)
      return luaL_error(L, "status bar texture allocation");
    free(o->bar_texture->texture_file);
    o->bar_texture->texture_file = replacement;
    return 0;
  }
  if (!strcmp(m, "SetStatusBarColor") || !strcmp(m, "GetStatusBarColor")) {
    if (!o->bar_texture)
      return luaL_error(L, "status bar texture is unavailable");
    if (!strcmp(m, "GetStatusBarColor")) {
      for (int i = 0; i < 4; i++)
        lua_pushnumber(L, o->bar_texture->vertex_color[i] / 255.);
      return 4;
    }
    for (int i = 0; i < 4; i++) {
      double value = luaL_optnumber(L, i + 2, 1);
      if (!isfinite(value) || value < 0 || value > 1)
        return luaL_error(L, "invalid status bar color");
      o->bar_texture->vertex_color[i] = (uint8_t)(value * 255 + .5);
    }
    return 0;
  }
  if (!strcmp(m, "GetValue") || !strcmp(m, "GetValueStep")) {
    lua_pushnumber(L, !strcmp(m, "GetValue") ? o->value : o->step);
    return 1;
  }
  if (!strcmp(m, "GetMinMaxValues")) {
    lua_pushnumber(L, o->minimum);
    lua_pushnumber(L, (float)(o->minimum + o->range));
    return 2;
  }
  if (!strcmp(m, "SetValue") || !strcmp(m, "SetValueStep") || !strcmp(m, "SetMinMaxValues")) {
    float first = (float)luaL_checknumber(L, 2);
    if (!isfinite(first))
      return luaL_error(L, "nonfinite slider scalar");
    int status = 0;
    if (!strcmp(m, "SetValue"))
      status = slider_value(o, first);
    else if (!strcmp(m, "SetMinMaxValues")) {
      float maximum = (float)luaL_checknumber(L, 3);
      if (!isfinite(maximum) || maximum < first || !isfinite(maximum - first))
        return luaL_error(L, "invalid slider range");
      float range = maximum - first;
      if (!o->range_initialized || fabs((double)first - o->minimum) > 2.384185791015625e-7 ||
          fabs((double)range - o->range) > 2.384185791015625e-7) {
        o->minimum = first;
        o->range = range;
        o->range_initialized = 1;
        if (o->value_initialized)
          status = slider_value(o, o->value);
      }
    } else {
      if (first < 1.1920928955078125e-7f)
        first = 1.1920928955078125e-7f;
      if (fabs((double)first - o->step) > 2.384185791015625e-7) {
        o->step = first;
        if (o->value_initialized)
          status = slider_value(o, o->value);
      }
    }
    if (status)
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "SetScrollChild")) {
    Object *c = owner(L, 2);
    if (attach_scroll(o, c))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "GetScrollChild")) {
    if (o->scroll_child)
      lua_rawgeti(L, LUA_REGISTRYINDEX, o->scroll_child->reference);
    else
      lua_pushnil(L);
    return 1;
  }
  if (!strcmp(m, "GetVerticalScrollRange") || !strcmp(m, "GetHorizontalScrollRange") ||
      !strcmp(m, "GetVerticalScroll") || !strcmp(m, "GetHorizontalScroll")) {
    lua_pushnumber(L, !strcmp(m, "GetVerticalScrollRange")     ? o->scroll_range_y
                      : !strcmp(m, "GetHorizontalScrollRange") ? o->scroll_range_x
                      : !strcmp(m, "GetVerticalScroll")        ? o->scroll_y
                                                               : o->scroll_x);
    return 1;
  }
  if (!strcmp(m, "SetVerticalScroll") || !strcmp(m, "SetHorizontalScroll")) {
    double v = luaL_checknumber(L, 2);
    if (!isfinite(v))
      return luaL_error(L, "nonfinite scroll offset");
    int vertical = !strcmp(m, "SetVerticalScroll");
    double *offset = vertical ? &o->scroll_y : &o->scroll_x;
    if (*offset != v) {
      *offset = v;
      if (o->scroll_child) {
        Object *c = o->scroll_child;
        if (c->point_count != 1 || c->points[0].relative != o)
          return luaL_error(L, "scroll child anchor profile not reconstructed");
        if (vertical)
          c->points[0].y = v;
        else
          c->points[0].x = -v;
      }
      uint32_t id = event_id(o, vertical ? "OnVerticalScroll" : "OnHorizontalScroll");
      if (id) {
        lua_pushnumber(L, v);
        if (bw_ui_call_script(L, &o->scripts[id], o->reference, 1, r->error, sizeof(r->error)))
          return luaL_error(L, "%s", r->error);
      }
    }
    return 0;
  }
  if (!strcmp(m, "SetBackdropColor") || !strcmp(m, "SetBackdropBorderColor")) {
    uint8_t value[4];
    color(L, 2, value, 0);
    if (o->has_backdrop)
      memcpy(!strcmp(m, "SetBackdropColor") ? o->backdrop_color : o->border_color, value, 4);
    return 0;
  }
  if (!strcmp(m, "GetTextColor")) {
    Object *f = o->has_color ? o : font_source(o);
    for (int i = 0; i < 4; i++)
      lua_pushnumber(L, f ? f->vertex_color[i] / 255.0 : 1);
    return 4;
  }
  if (!strcmp(m, "SetTextColor")) {
    color(L, 2, o->vertex_color, 0);
    o->has_color = 1;
    return 0;
  }
  if (!strcmp(m, "SetVertexColor")) {
    color(L, 2, o->vertex_color, 1);
    o->has_color = 1;
    return 0;
  }
  if (!strcmp(m, "RegisterEvent") || !strcmp(m, "UnregisterEvent") ||
      !strcmp(m, "UnregisterAllEvents") || !strcmp(m, "IsEventRegistered")) {
    const char *event = !strcmp(m, "UnregisterAllEvents") ? NULL : luaL_checkstring(L, 2);
    uint32_t i = 0;
    while (i < r->subscription_count &&
           (r->subscriptions[i].object != o || (event && strcmp(r->subscriptions[i].event, event))))
      i++;
    if (!strcmp(m, "IsEventRegistered"))
      return numeric_flag(L, i < r->subscription_count);
    if (!strcmp(m, "RegisterEvent")) {
      if (!known_event(r, event) || i < r->subscription_count)
        return 0;
      if (r->subscription_count >= 8192)
        return luaL_error(L, "event subscription capacity");
      Subscription *s = &r->subscriptions[r->subscription_count++];
      s->object = o;
      strcpy(s->event, event);
      return 0;
    }
    for (i = 0; i < r->subscription_count;) {
      Subscription *s = &r->subscriptions[i];
      if (s->object == o && (!event || !strcmp(s->event, event))) {
        memmove(s, s + 1, (r->subscription_count - i - 1) * sizeof(*s));
        r->subscription_count--;
      } else
        i++;
    }
    return 0;
  }
  if (!strcmp(m, "Enable") || !strcmp(m, "Disable")) {
    o->enabled = !strcmp(m, "Enable");
    if (!o->enabled && r->pressed_button == o) {
      o->pressed = 0;
      r->pressed_button = NULL;
    }
    return 0;
  }
  if (!strcmp(m, "Show") || !strcmp(m, "Hide")) {
    int next = !strcmp(m, "Show");
    if (change_visibility(o, next))
      return luaL_error(L, "%s", r->error);
    return 0;
  }
  if (!strcmp(m, "GetWidth") || !strcmp(m, "GetHeight") || !strcmp(m, "GetLeft") ||
      !strcmp(m, "GetBottom") || !strcmp(m, "GetRight") || !strcmp(m, "GetTop") ||
      !strcmp(m, "GetCenter")) {
    int width = !strcmp(m, "GetWidth"), height = !strcmp(m, "GetHeight");
    double stored = width ? o->width : o->height;
    /* 0x49d3b0/0x49d550 prefer nonzero stored size, or skip rectangle
   * evaluation when argument2 converts true. Unresolved size remains zero. */
    if ((width || height) && (stored != 0 || native_flag(L, 2, 0))) {
      lua_pushnumber(L, stored);
      return 1;
    }
    BwAnchorRect rect;
    int status = rectangle(o, &rect, 0);
    if (status == 1)
      return luaL_error(L, "%s", r->error);
    if (status == 2) {
      if (width || height)
        lua_pushnumber(L, stored);
      else
        lua_pushnil(L);
      if (!strcmp(m, "GetCenter")) {
        lua_pushnil(L);
        return 2;
      }
      return 1;
    }
    if (!strcmp(m, "GetCenter")) {
      lua_pushnumber(L, rect.left + rect.width * 0.5);
      lua_pushnumber(L, rect.bottom + rect.height * 0.5);
      return 2;
    }
    lua_pushnumber(L, width                    ? rect.width
                      : height                 ? rect.height
                      : !strcmp(m, "GetLeft")  ? rect.left
                      : !strcmp(m, "GetRight") ? rect.left + rect.width
                      : !strcmp(m, "GetTop")   ? rect.bottom + rect.height
                                               : rect.bottom);
    return 1;
  }

  if (!strcmp(m, "GetAlpha") || !strcmp(m, "GetScale") || !strcmp(m, "GetEffectiveScale")) {
    double v = !strcmp(m, "GetAlpha") ? o->alpha : o->scale;
    if (!strcmp(m, "GetEffectiveScale"))
      for (Object *p = o->parent; p; p = p->parent)
        v *= p->scale;
    lua_pushnumber(L, v);
    return 1;
  }
  if (!strcmp(m, "SetWidth") || !strcmp(m, "SetHeight") || !strcmp(m, "SetAlpha") ||
      !strcmp(m, "SetScale")) {
    double v = luaL_checknumber(L, 2);
    if (!isfinite(v) || v < 0 || (!strcmp(m, "SetScale") && v == 0))
      return luaL_error(L, "invalid frame scalar");
    if (!strcmp(m, "SetWidth")) {
      o->width = v;
      o->width_known = 1;
    } else if (!strcmp(m, "SetHeight")) {
      o->height = v;
      o->height_known = 1;
    } else if (!strcmp(m, "SetAlpha"))
      o->alpha = v;
    else
      o->scale = v;
    return 0;
  }
  if (!strcmp(m, "ClearAllPoints")) {
    o->point_count = 0;
    o->all_points = 0;
    o->generated_points = 0;
    return 0;
  }
  if (!strcmp(m, "SetPoint")) {
    int p = point_id(luaL_checkstring(L, 2));
    if (p < 0)
      return luaL_error(L, "invalid anchor point");
    Point point = {0};
    point.point = (uint32_t)p;
    point.relative_point = (uint32_t)p;
    int delta = 3;
    if (lua_istable(L, 3)) {
      point.relative = owner(L, 3);
      delta = 4;
    } else if (lua_type(L, 3) == LUA_TSTRING) {
      const char *s = lua_tostring(L, 3);
      if (expand_name(s, o->parent ? o->parent->name : "", point.relative_name))
        return luaL_error(L, "anchor name capacity");
      delta = 4;
    } else if (lua_isnil(L, 3))
      delta = 4;
    if (lua_type(L, delta) == LUA_TSTRING) {
      int q = point_id(lua_tostring(L, delta++));
      if (q < 0)
        return luaL_error(L, "invalid relative point");
      point.relative_point = (uint32_t)q;
    }
    if (o->generated_points) {
      o->point_count = 0;
      o->all_points = 0;
      o->generated_points = 0;
    }
    point.x = luaL_optnumber(L, delta, 0);
    point.y = luaL_optnumber(L, delta + 1, 0);
    if (!isfinite(point.x) || !isfinite(point.y))
      return luaL_error(L, "nonfinite anchor offset");
    uint32_t i = 0;
    while (i < o->point_count && o->points[i].point != point.point)
      i++;
    if (i >= 32)
      return luaL_error(L, "anchor capacity");
    o->points[i] = point;
    if (i == o->point_count)
      o->point_count++;
    o->all_points = 0;
    return 0;
  }
  if (!strcmp(m, "GetNumPoints")) {
    lua_pushnumber(L, o->all_points ? 2 : o->point_count);
    return 1;
  }
  if (!strcmp(m, "GetPoint")) {
    int index = luaL_optint(L, 2, 1) - 1;
    Point point = {0};
    if (o->all_points) {
      if (index < 0 || index > 1)
        return luaL_error(L, "anchor index");
      point.point = point.relative_point = index ? BW_ANCHOR_BOTTOMRIGHT : BW_ANCHOR_TOPLEFT;
      point.relative = o->all_points_target;
    } else {
      if (index < 0 || (uint32_t)index >= o->point_count)
        return luaL_error(L, "anchor index");
      point = o->points[index];
    }
    Object *relative = point.relative ? point.relative : o->parent;
    if (point.relative_name[0])
      relative = find(r, point.relative_name);
    lua_pushstring(L, point_names[point.point]);
    if (relative)
      lua_rawgeti(L, LUA_REGISTRYINDEX, relative->reference);
    else
      lua_pushnil(L);
    lua_pushstring(L, point_names[point.relative_point]);
    lua_pushnumber(L, point.x);
    lua_pushnumber(L, point.y);
    return 5;
  }
  if (!strcmp(m, "GetNumRegions")) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < r->count; i++)
      if (r->objects[i]->parent == o && !frame_type(r->objects[i]->type))
        count++;
    lua_pushnumber(L, count);
    return 1;
  }
  if (!strcmp(m, "GetChildren") || !strcmp(m, "GetRegions")) {
    int count = 0;
    for (uint32_t i = 0; i < r->count; i++) {
      Object *c = r->objects[i];
      if (c->parent == o && ((frame_type(c->type)) == !strcmp(m, "GetChildren"))) {
        if (!lua_checkstack(L, 1))
          return luaL_error(L, "child result capacity");
        lua_rawgeti(L, LUA_REGISTRYINDEX, c->reference);
        count++;
      }
    }
    return count;
  }
  if (!strcmp(m, "HasScript")) {
    uint32_t id = event_id(o, luaL_checkstring(L, 2));
    return numeric_flag(L, id != 0);
  }
  if (!strcmp(m, "GetScript") || !strcmp(m, "SetScript")) {
    const char *s = luaL_checkstring(L, 2);
    uint32_t id = event_id(o, s);
    if (!id)
      return luaL_error(L, "unsupported frame event");
    if (!strcmp(m, "GetScript")) {
      if (o->scripts[id].reference > 0)
        lua_rawgeti(L, LUA_REGISTRYINDEX, o->scripts[id].reference);
      else
        lua_pushnil(L);
      return 1;
    }
    if (!lua_isnil(L, 3))
      luaL_checktype(L, 3, LUA_TFUNCTION);
    bw_ui_clear_script(L, &o->scripts[id]);
    if (!lua_isnil(L, 3)) {
      lua_pushvalue(L, 3);
      o->scripts[id].reference = luaL_ref(L, LUA_REGISTRYINDEX);
      o->scripts[id].context = bw_ui_execution_context(L);
    }
    return 0;
  }
  return luaL_error(L, "unimplemented frame method %s", m);
}
static int number(BwUiRuntime *r, const char *s, double *value) {
  if (!s)
    return 0;
  char *end;
  double n = strtod(s, &end);
  if (*end || !isfinite(n) || n < 0)
    return fail(r, "invalid XML dimension/scalar");
  *value = n;
  return 0;
}
static int expand_name(const char *source, const char *parent, char *out) {
  size_t at = 0;
  for (const char *s = source ? source : ""; *s;) {
    const char *t = s;
    size_t n = 1;
    if (!strncmp(s, "$parent", 7)) {
      t = parent;
      n = strlen(parent);
      s += 7;
    } else
      s++;
    if (at + n >= MAX_NAME)
      return 1;
    memcpy(out + at, t, n);
    at += n;
  }
  out[at] = 0;
  return 0;
}
static Object *create(BwUiRuntime *r, const char *type, const char *name, Object *parent) {
  uint32_t kind = type_id(type);
  if (!kind || r->count >= MAX_OBJECTS) {
    fail(r, "frame type/capacity");
    return NULL;
  }
  if (name && *name && find(r, name)) {
    fail(r, "duplicate object name");
    return NULL;
  }
  Object *o = calloc(1, sizeof(*o));
  if (!o) {
    fail(r, "object allocation");
    return NULL;
  }
  if (kind == BW_UI_EDITBOX) {
    o->edit = calloc(1, sizeof(*o->edit));
    if (!o->edit) {
      free(o);
      fail(r, "edit allocation");
      return NULL;
    }
  }
  o->runtime = r;
  o->id = r->count;
  o->type = kind;
  o->shown = 1;
  o->enabled = 1;
  o->alpha = o->scale = 1;
  o->parent = parent;
  o->model_scale = 1;
  o->slider_vertical = kind != BW_UI_STATUSBAR;
  o->frame_stratum = parent ? parent->frame_stratum : 3;
  o->frame_level = parent ? (parent->frame_level < 127 ? parent->frame_level + 1 : 128) : 0;
  strcpy(o->justify_h, kind == 12 ? "CENTER" : "LEFT");
  strcpy(o->justify_v, "MIDDLE");
  const float default_uv[8] = {0, 0, 0, 1, 1, 0, 1, 1};
  memcpy(o->uv, default_uv, sizeof(default_uv));
  memset(o->backdrop_color, 255, 4);
  memset(o->border_color, 255, 4);
  memset(o->vertex_color, 255, 4);
  snprintf(o->name, sizeof(o->name), "%s", name ? name : "");
  snprintf(o->tag, sizeof(o->tag), "%s", !strcmp(type, "WorldFrame") ? type : type_names[kind - 1]);
  r->objects[r->count++] = o;
  lua_State *L = r->L;
  lua_newtable(L);
  lua_pushlightuserdata(L, o);
  lua_rawseti(L, -2, 0);
  const char *methods[] = {"Raise",
                           "Lower",
                           "StartMovie",
                           "StopMovie",
                           "EnableSubtitles",
                           "CreateFontString",
                           "CreateTexture",
                           "GetTexture",
                           "SetTexture",
                           "SetDesaturated",
                           "GetNormalTexture",
                           "GetPushedTexture",
                           "GetDisabledTexture",
                           "GetHighlightTexture",
                           "SetNormalTexture",
                           "SetPushedTexture",
                           "SetDisabledTexture",
                           "SetHighlightTexture",
                           "EnableKeyboard",
                           "IsKeyboardEnabled",
                           "EnableMouse",
                           "IsMouseEnabled",
                           "GetName",
                           "GetFrameLevel",
                           "SetFrameLevel",
                           "GetFrameStrata",
                           "SetFrameStrata",
                           "SetAllPoints",
                           "GetObjectType",
                           "GetParent",
                           "GetID",
                           "SetID",
                           "IsShown",
                           "IsVisible",
                           "Show",
                           "Hide",
                           "GetWidth",
                           "GetHeight",
                           "GetCenter",
                           "GetLeft",
                           "GetBottom",
                           "GetRight",
                           "GetTop",
                           "SetWidth",
                           "SetHeight",
                           "GetAlpha",
                           "SetAlpha",
                           "GetScale",
                           "SetScale",
                           "GetEffectiveScale",
                           "GetChildren",
                           "GetRegions",
                           "GetNumRegions",
                           "HasScript",
                           "GetScript",
                           "SetScript",
                           "Enable",
                           "Disable",
                           "IsEnabled",
                           "ClearAllPoints",
                           "SetPoint",
                           "GetPoint",
                           "GetNumPoints",
                           "RegisterEvent",
                           "UnregisterEvent",
                           "UnregisterAllEvents",
                           "IsEventRegistered",
                           "SetBackdropColor",
                           "SetBackdropBorderColor",
                           "SetVertexColor",
                           "SetTextColor",
                           "GetTextColor",
                           "SetScrollChild",
                           "GetScrollChild",
                           "GetVerticalScrollRange",
                           "GetHorizontalScrollRange",
                           "GetVerticalScroll",
                           "GetHorizontalScroll",
                           "SetVerticalScroll",
                           "SetHorizontalScroll",
                           "GetStatusBarTexture",
                           "SetStatusBarTexture",
                           "SetStatusBarColor",
                           "GetStatusBarColor",
                           "GetValue",
                           "SetValue",
                           "GetValueStep",
                           "SetValueStep",
                           "GetMinMaxValues",
                           "SetMinMaxValues",
                           "GetText",
                           "SetText",
                           "SetFormattedText",
                           "GetFontString",
                           "GetStringWidth",
                           "GetStringHeight",
                           "GetTextWidth",
                           "GetFont",
                           "GetFontObject",
                           "SetFontObject",
                           "GetNormalFontObject",
                           "SetNormalFontObject",
                           "SetModel",
                           "SetCamera",
                           "SetSequence",
                           "SetSequenceTime",
                           "SetModelScale",
                           "GetModelScale",
                           "AdvanceTime",
                           "SetFogColor",
                           "SetFogNear",
                           "SetFogFar",
                           "ClearFog",
                           "SetGlow",
                           "ResetLights",
                           "AddLight",
                           "AddCharacterLight",
                           "AddPetLight",
                           "SetJustifyH",
                           "SetJustifyV",
                           "GetJustifyH",
                           "GetJustifyV",
                           "GetTexCoord",
                           "SetTexCoord",
                           "LockHighlight",
                           "UnlockHighlight",
                           "GetHighlightFontObject",
                           "SetHighlightFontObject",
                           "GetDisabledFontObject",
                           "SetDisabledFontObject",
                           "GetChecked",
                           "SetChecked",
                           "GetButtonState",
                           "SetButtonState",
                           "HasFocus",
                           "SetFocus",
                           "ClearFocus",
                           "SetAutoFocus",
                           "GetMaxLetters",
                           "SetMaxLetters",
                           "HighlightText",
                           "Insert",
                           "GetCursorPosition",
                           "SetCursorPosition",
                           "RegisterForClicks",
                           "Click",
                           NULL};
  if (!r->metatables[kind]) {
    lua_newtable(L);
    for (const char **m = methods; *m; m++) {
      if ((!strcmp(*m, "StartMovie") || !strcmp(*m, "StopMovie") ||
           !strcmp(*m, "EnableSubtitles")) &&
          kind != 10)
        continue;
      if ((!strcmp(*m, "HighlightText") || !strcmp(*m, "Insert") || strstr(*m, "CursorPosition")) &&
          kind != 4)
        continue;
      if (!strcmp(*m, "GetEffectiveScale") && kind == 12)
        continue;
      if (!strcmp(*m, "CreateTexture") && !frame_type(kind))
        continue;
      if (strstr(*m, "StatusBar") && kind != BW_UI_STATUSBAR)
        continue;
      if ((!strcmp(*m, "GetTexture") || !strcmp(*m, "SetTexture")) && kind != 11)
        continue;
      if ((!strcmp(*m, "GetNormalTexture") || !strcmp(*m, "GetPushedTexture") ||
           !strcmp(*m, "GetDisabledTexture") || !strcmp(*m, "GetHighlightTexture") ||
           !strcmp(*m, "SetNormalTexture") || !strcmp(*m, "SetPushedTexture") ||
           !strcmp(*m, "SetDisabledTexture") || !strcmp(*m, "SetHighlightTexture")) &&
          kind != 2 && kind != 3)
        continue;
      int frame_only = strstr(*m, "Keyboard") || strstr(*m, "Mouse") || strstr(*m, "FrameLevel") ||
                       strstr(*m, "FrameStrata") || !strcmp(*m, "GetID") || !strcmp(*m, "SetID") ||
                       !strcmp(*m, "GetChildren") || !strcmp(*m, "GetRegions") ||
                       !strcmp(*m, "GetNumRegions") || !strcmp(*m, "HasScript") ||
                       !strcmp(*m, "GetScript") || !strcmp(*m, "SetScript") ||
                       strstr(*m, "Event") || strstr(*m, "Backdrop");
      if (frame_only && !frame_type(kind))
        continue;
      if ((!strcmp(*m, "SetModel") || !strcmp(*m, "SetCamera") || !strcmp(*m, "SetSequence") ||
           !strcmp(*m, "SetSequenceTime") || !strcmp(*m, "SetModelScale") ||
           !strcmp(*m, "GetModelScale") || !strcmp(*m, "AdvanceTime") || strstr(*m, "Fog") ||
           !strcmp(*m, "SetGlow") || strstr(*m, "Light")) &&
          kind != 8 && kind != 9)
        continue;
      if (strstr(*m, "TexCoord") && kind != 11)
        continue;
      if (!strcmp(*m, "SetVertexColor") && kind != 11 && kind != 12)
        continue;
      if (strstr(*m, "Justify") && kind != 12 && kind != 13 && kind != 4 && kind != 7)
        continue;
      if (!strcmp(*m, "SetFormattedText") && kind != 2 && kind != 3 && kind != 12)
        continue;
      if (!strcmp(*m, "GetStringWidth") && kind != 12)
        continue;
      if (!strcmp(*m, "GetTextWidth") && kind != 2 && kind != 3)
        continue;
      if (strstr(*m, "Text") && strcmp(*m, "CreateTexture") && strcmp(*m, "GetTexture") &&
          strcmp(*m, "SetTexture") && kind != 2 && kind != 3 && kind != 4 && kind != 12 &&
          !(kind == 13 && (!strcmp(*m, "GetTextColor") || !strcmp(*m, "SetTextColor"))) &&
          !(kind == 7 && !strcmp(*m, "SetText")))
        continue;
      if ((!strcmp(*m, "GetFontString") || !strcmp(*m, "LockHighlight") ||
           !strcmp(*m, "UnlockHighlight")) &&
          kind != 2 && kind != 3)
        continue;
      if ((strstr(*m, "NormalFont") || strstr(*m, "HighlightFont") || strstr(*m, "DisabledFont")) &&
          kind != 2 && kind != 3)
        continue;
      if ((!strcmp(*m, "GetFont") || !strcmp(*m, "GetFontObject") ||
           !strcmp(*m, "SetFontObject")) &&
          kind != 12 && kind != 13 && kind != 4 && kind != 7)
        continue;
      if (strstr(*m, "Scroll") && kind != BW_UI_SCROLLFRAME)
        continue;
      if ((strstr(*m, "Value")) && kind != BW_UI_SLIDER && kind != BW_UI_STATUSBAR)
        continue;
      if (!strcmp(*m, "RegisterForClicks") && kind != 2 && kind != 3)
        continue;
      if (!strcmp(*m, "Click") && kind != 2 && kind != 3)
        continue;
      if ((!strcmp(*m, "GetChecked") || !strcmp(*m, "SetChecked")) && kind != BW_UI_CHECKBUTTON)
        continue;
      if ((!strcmp(*m, "GetButtonState") || !strcmp(*m, "SetButtonState")) && kind != 2 &&
          kind != 3)
        continue;
      if ((strstr(*m, "Focus") || strstr(*m, "MaxLetters")) && kind != BW_UI_EDITBOX)
        continue;
      if ((!strcmp(*m, "Enable") || !strcmp(*m, "Disable") || !strcmp(*m, "IsEnabled")) &&
          kind != 2 && kind != 3 && kind != 4 && kind != 6)
        continue;
      lua_rawgeti(L, LUA_REGISTRYINDEX, r->handle_reference);
      lua_pushstring(L, *m);
      lua_pushcclosure(L, method, 2);
      lua_setfield(L, -2, *m);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    r->metatables[kind] = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, r->metatables[kind]);
  lua_setmetatable(L, -2);
  if (o->name[0]) {
    lua_pushvalue(L, -1);
    lua_setglobal(L, o->name);
  }
  o->reference = luaL_ref(L, LUA_REGISTRYINDEX);
  return o;
}
static const BwXmlNode *child(const BwXmlNode *n, const char *s) {
  for (n = n ? n->child : NULL; n; n = n->next)
    if (tag(n, s))
      return n;
  return NULL;
}
static Object *inherited_font(BwUiRuntime *r, const BwXmlNode *raw, uint32_t depth) {
  if (depth > 64)
    return NULL;
  const char *s = attr(raw, "inherits");
  Object *found = NULL;
  while (s && *s) {
    while (*s == ' ' || *s == '\t' || *s == ',')
      s++;
    const char *end = s;
    while (*end && *end != ',')
      end++;
    const char *trim = end;
    while (trim > s && (trim[-1] == ' ' || trim[-1] == '\t'))
      trim--;
    size_t n = (size_t)(trim - s);
    if (!n || n >= MAX_NAME)
      return found;
    char name[MAX_NAME];
    memcpy(name, s, n);
    name[n] = 0;
    Object *f = find(r, name);
    if (f && f->type == 13)
      found = f;
    else {
      Definition *d = definition(r, name);
      if (d) {
        f = inherited_font(r, d->node, depth + 1);
        if (f)
          found = f;
      }
    }
    s = *end ? end + 1 : end;
  }
  return found;
}
static int instantiate(BwUiRuntime *r, const BwXmlNode *raw, Object *parent, uint32_t depth) {
  if (depth >= 128)
    return fail(r, "frame nesting limit");
  BwXmlNode *n = calloc(1, sizeof(*n));
  if (!n)
    return fail(r, "XML composition allocation");
  n->name = copy(raw->name);
  const BwXmlNode *stack[64];
  if (!n->name || inherit(r, n, raw, stack, 0)) {
    release(n);
    return 1;
  }
  const char *explicit_parent = attr(n, "parent");
  if (explicit_parent) {
    Object *p = find(r, explicit_parent);
    if (!p) {
      release(n);
      return fail(r, "unknown explicit parent");
    }
    parent = p;
  }
  char name[MAX_NAME];
  if (expand_name(attr(raw, "name"), parent ? parent->name : "", name)) {
    release(n);
    return fail(r, "expanded name capacity");
  }
  const char *kind = local(raw->name);
  if (strstr(kind, "Texture"))
    kind = "Texture";
  else if (!strcmp(kind, "ButtonText"))
    kind = "FontString";
  Object *o = create(r, kind, name, parent);
  if (!o) {
    release(n);
    return 1;
  }
  o->composed = n;
  if (o->type == 12)
    o->font_object = inherited_font(r, raw, 0);
  if (o->type == 11 && attr(n, "file")) {
    o->texture_file = copy(attr(n, "file"));
    if (!o->texture_file)
      return fail(r, "texture descriptor allocation");
  }
  o->has_backdrop = child(n, "Backdrop") != NULL;
  o->shown = !(attr(n, "hidden") && !strcmp(attr(n, "hidden"), "true"));
  if (attr(n, "enableMouse") && !strcmp(attr(n, "enableMouse"), "true"))
    o->input_flags |= 4;
  if (attr(n, "enableKeyboard") && !strcmp(attr(n, "enableKeyboard"), "true"))
    o->input_flags |= 3;
  if (o->type == 13 || o->type == 12 || o->type == 4) {
    const char *font = attr(n, "font");
    if (font) {
      Object *definition = find(r, font);
      if (definition && definition->type == 13)
        o->font_object = definition;
      else {
        o->font_file = copy(font);
        if (!o->font_file)
          return fail(r, "font descriptor allocation");
      }
    }
    const BwXmlNode *height = child(child(n, "FontHeight"), "AbsValue");
    if (number(r, attr(height, "val"), &o->font_height))
      return 1;
    const BwXmlNode *shadow = child(child(child(n, "Shadow"), "Offset"), "AbsDimension");
    if (shadow) {
      const char *x = attr(shadow, "x"), *y = attr(shadow, "y");
      char *end;
      o->has_shadow = 1;
      if (x) {
        o->shadow_x = strtod(x, &end);
        if (*end || !isfinite(o->shadow_x) || fabs(o->shadow_x) > 128)
          return fail(r, "invalid font shadow");
      }
      if (y) {
        o->shadow_y = strtod(y, &end);
        if (*end || !isfinite(o->shadow_y) || fabs(o->shadow_y) > 128)
          return fail(r, "invalid font shadow");
      }
    }
    const char *outline = attr(n, "outline");
    if (outline)
      snprintf(o->font_flags, sizeof(o->font_flags), "%s",
               !strcmp(outline, "THICK")    ? "THICKOUTLINE"
               : !strcmp(outline, "NORMAL") ? "OUTLINE"
                                            : "");
  }
  for (const BwXmlNode *f = n->child; f; f = f->next) {
    Object **slot = tag(f, "NormalFont")      ? &o->normal_font
                    : tag(f, "HighlightFont") ? &o->highlight_font
                    : tag(f, "DisabledFont")  ? &o->disabled_font
                                              : NULL;
    if (slot) {
      const char *name = attr(f, "style");
      if (name) {
        *slot = find(r, name);
        if (!*slot || (*slot)->type != 13)
          return fail(r, "unknown XML font style");
      }
    }
  }
  const BwXmlNode *pressed_offset = child(child(n, "PushedTextOffset"), "AbsDimension");
  if (pressed_offset && (number(r, attr(pressed_offset, "x"), &o->press_x) ||
                         number(r, attr(pressed_offset, "y"), &o->press_y)))
    return 1;
  const BwXmlNode *rgba = child(n, "Color");
  if (rgba) {
    o->has_color = 1;
    const char *keys[4] = {"r", "g", "b", "a"};
    for (int i = 0; i < 4; i++) {
      const char *v = attr(rgba, keys[i]);
      if (v) {
        char *end;
        double value = strtod(v, &end);
        if (*end || !isfinite(value) || value < 0 || value > 1)
          return fail(r, "invalid XML color");
        o->vertex_color[i] = (uint8_t)(value * 255 + 0.5);
      }
    }
  }
  const BwXmlNode *tc = child(n, "TexCoords");
  if (tc) {
    const char *keys[4] = {"left", "right", "top", "bottom"};
    float values[4] = {0, 1, 0, 1};
    for (int i = 0; i < 4; i++) {
      const char *v = attr(tc, keys[i]);
      if (v) {
        char *end;
        double value = strtod(v, &end);
        if (*end || !isfinite(value) || fabs(value) > 10000)
          return fail(r, "invalid XML texture coordinate");
        values[i] = (float)value;
      }
    }
    float uv[8] = {values[0], values[2], values[0], values[3],
                   values[1], values[2], values[1], values[3]};
    memcpy(o->uv, uv, sizeof(uv));
  }
  const char *text = attr(n, "text");
  if (text) {
    lua_getglobal(r->L, text);
    const char *translated = lua_tostring(r->L, -1);
    if (replace_text(o, translated ? translated : text)) {
      lua_pop(r->L, 1);
      return 1;
    }
    lua_pop(r->L, 1);
  }
  if (o->type == BW_UI_SLIDER || o->type == BW_UI_STATUSBAR) {
    const char *axis = attr(n, "orientation");
    if (axis) {
      if (strcmp(axis, "VERTICAL") && strcmp(axis, "HORIZONTAL"))
        return fail(r, "invalid slider orientation");
      o->slider_vertical = !strcmp(axis, "VERTICAL");
    }
  }
  const char *identifier = attr(n, "id");
  if (identifier) {
    char *end;
    long id = strtol(identifier, &end, 10);
    if (*end || id < (-2147483647L - 1) || id > 2147483647L)
      return fail(r, "invalid XML ID");
    o->user_id = (int)id;
  }
  const char *jh = attr(n, "justifyH"), *jv = attr(n, "justifyV");
  if (!jh && o->parent &&
      (!strcmp(o->parent->name, "CinematicsBackground") || !strcmp(o->name, "GlueDialogText") ||
       !strcmp(o->name, "CharSelectRealmName")))
    strcpy(o->justify_h, "CENTER");
  if (jh) {
    if (strlen(jh) >= sizeof(o->justify_h))
      return fail(r, "justify capacity");
    strcpy(o->justify_h, jh);
  }
  if (jv) {
    if (strlen(jv) >= sizeof(o->justify_v))
      return fail(r, "justify capacity");
    strcpy(o->justify_v, jv);
  }
  if (number(r, attr(n, "alpha"), &o->alpha) || number(r, attr(n, "scale"), &o->scale))
    return 1;
  const BwXmlNode *size = child(n, "Size");
  const BwXmlNode *dimension = child(size, "AbsDimension");
  if (!dimension)
    dimension = size;
  if (number(r, attr(dimension, "x"), &o->width) || number(r, attr(dimension, "y"), &o->height))
    return 1;
  o->width_known = attr(dimension, "x") != NULL;
  o->height_known = attr(dimension, "y") != NULL;
  o->all_points = attr(n, "setAllPoints") && !strcmp(attr(n, "setAllPoints"), "true");
  if (o->all_points)
    all_points(o, parent);
  const char *frame_strata = attr(n, "frameStrata");
  if (frame_strata) {
    int value = stratum(frame_strata);
    if (!value || set_stratum(o, value, 0))
      return fail(r, "unsupported XML frame strata");
  }
  const char *frame_level = attr(n, "frameLevel");
  if (frame_level) {
    char *end;
    double value = strtod(frame_level, &end);
    if (*end || !isfinite(value) || value < 0 || value > 2147483647)
      return fail(r, "invalid XML frame level");
    if (set_level(o, (int)value, 0))
      return 1;
  }
  const BwXmlNode *anchors = child(n, "Anchors");
  for (const BwXmlNode *a = anchors ? anchors->child : NULL; a; a = a->next) {
    if (o->point_count >= 32)
      return fail(r, "anchor capacity");
    Point *p = &o->points[o->point_count++];
    int point = point_id(attr(a, "point")),
        relative = point_id(attr(a, "relativePoint") ? attr(a, "relativePoint") : attr(a, "point"));
    if (point < 0 || relative < 0)
      return fail(r, "invalid XML point");
    p->point = (uint32_t)point;
    p->relative_point = (uint32_t)relative;
    if (expand_name(attr(a, "relativeTo"), parent ? parent->name : "", p->relative_name))
      return fail(r, "anchor reference capacity");
    const BwXmlNode *offset = child(a, "Offset"), *delta = child(offset, "AbsDimension");
    if (!delta)
      delta = offset;
    const char *x = attr(delta, "x"), *y = attr(delta, "y");
    if (!x)
      x = attr(a, "x");
    if (!y)
      y = attr(a, "y");
    char *end;
    if (x) {
      p->x = strtod(x, &end);
      if (*end || !isfinite(p->x))
        return fail(r, "invalid XML anchor offset");
    }
    if (y) {
      p->y = strtod(y, &end);
      if (*end || !isfinite(p->y))
        return fail(r, "invalid XML anchor offset");
    }
  }
  /* Native texture XML post-load 0x815f92 -> 0x4830e0 -> 0x48a3e0:
  * missing explicit anchors produce parent all-points with generated flag.
  * ThumbTexture positioning is a separate deferred Slider contract. */
  if (o->type == 11 && parent && !o->point_count && !o->all_points && !tag(raw, "ThumbTexture")) {
    all_points(o, parent);
    o->generated_points = 1;
  }
  const BwXmlNode *scripts = child(n, "Scripts");
  for (const BwXmlNode *s = scripts ? scripts->child : NULL; s; s = s->next) {
    const char *event = local(s->name);
    uint32_t id = event_id(o, event);
    if (!id)
      return fail(r, "unsupported XML script event");
    const char *body = s->text ? s->text : "";
    if (bw_ui_bind_script(r->L, o->type, event, (uint32_t)strlen(event), body,
                          (uint32_t)strlen(body), attr(s, "function"), &o->scripts[id], r->error,
                          sizeof(r->error)))
      return 1;
  }
  for (const BwXmlNode *c = n->child; c; c = c->next) {
    if (tag(c, "Frames") || tag(c, "ScrollChild")) {
      for (const BwXmlNode *f = c->child; f; f = f->next) {
        uint32_t start = r->count;
        if (instantiate(r, f, o, depth + 1))
          return 1;
        if (tag(c, "ScrollChild") && attach_scroll(o, r->objects[start]))
          return 1;
      }
    } else if (tag(c, "Layers")) {
      for (const BwXmlNode *l = c->child; l; l = l->next) {
        const char *level = attr(l, "level");
        uint32_t layer = !level || !strcmp(level, "ARTWORK") ? 3
                         : !strcmp(level, "BACKGROUND")      ? 1
                         : !strcmp(level, "BORDER")          ? 2
                         : !strcmp(level, "OVERLAY")         ? 4
                         : !strcmp(level, "HIGHLIGHT")       ? 5
                                                             : 0;
        if (!layer)
          return fail(r, "unsupported XML draw layer");
        for (const BwXmlNode *f = l->child; f; f = f->next) {
          uint32_t start = r->count;
          if (instantiate(r, f, o, depth + 1))
            return 1;
          r->objects[start]->draw_layer = layer;
        }
      }
    } else if (strstr(local(c->name), "Texture") || tag(c, "ButtonText")) {
      uint32_t start = r->count;
      if (instantiate(r, c, o, depth + 1))
        return 1;
      if (tag(c, "NormalTexture")) {
        o->button_textures[0] = r->objects[start];
        r->objects[start]->draw_layer = 1;
      } else if (tag(c, "PushedTexture")) {
        o->button_textures[1] = r->objects[start];
        r->objects[start]->draw_layer = 1;
      } else if (tag(c, "DisabledTexture")) {
        o->button_textures[2] = r->objects[start];
        r->objects[start]->draw_layer = 1;
      } else if (tag(c, "HighlightTexture")) {
        o->button_textures[3] = r->objects[start];
        r->objects[start]->draw_layer = 5;
      }
      if (tag(c, "CheckedTexture")) {
        o->button_textures[4] = r->objects[start];
        r->objects[start]->draw_layer = 3;
      } else if (tag(c, "DisabledCheckedTexture")) {
        o->button_textures[5] = r->objects[start];
        r->objects[start]->draw_layer = 3;
      }
      if (tag(c, "BarTexture") && o->type == BW_UI_STATUSBAR) {
        o->bar_texture = r->objects[start];
        o->bar_texture->draw_layer = 3;
        const BwXmlNode *color = child(n, "BarColor");
        if (color) {
          const char *keys[] = {"r", "g", "b", "a"};
          for (uint32_t channel = 0; channel < 4; channel++) {
            double value = 1;
            if (number(r, attr(color, keys[channel]), &value) || value < 0 || value > 1)
              return fail(r, "invalid status bar color");
            o->bar_texture->vertex_color[channel] = (uint8_t)(value * 255 + .5);
          }
        }
      }
      if (tag(c, "ThumbTexture") && o->type == BW_UI_SLIDER)
        o->thumb = r->objects[start];
      if (tag(c, "ButtonText")) {
        o->button_text = r->objects[start];
        if (!o->button_text->font_object && !o->button_text->font_file)
          o->button_text->font_object = o->normal_font;
      }
    }
  }
  if (o->type == BW_UI_EDITBOX) {
    const char *letters = attr(n, "letters");
    if (letters) {
      char *end;
      long limit = strtol(letters, &end, 10);
      if (*end || limit < 0 || limit > BW_EDIT_CAPACITY)
        return fail(r, "edit letter capacity");
      o->max_letters = (int)limit;
    }
    const char *password = attr(n, "password");
    o->edit_password = password && (!strcmp(password, "1") || !strcmp(password, "true"));
    const BwXmlNode *descriptor = child(n, "FontString");
    if (descriptor) {
      BwXmlNode *style = clone(descriptor);
      if (!style)
        return fail(r, "edit font descriptor allocation");
      uint32_t start = r->count;
      int status = instantiate(r, style, o, depth + 1);
      release(style);
      if (status)
        return status;
      Object *t = r->objects[start];
      o->edit_display = t;
      // Input glyphs follow the EditBox alignment, not FontString's centered
      // default used by labels such as the separate Name caption.
      strcpy(t->justify_h, o->justify_h);
      strcpy(t->justify_v, o->justify_v);
      const BwXmlNode *insets = child(child(n, "TextInsets"), "AbsInset");
      double left = 0, right = 0, bottom = 0, top = 0;
      if (number(r, attr(insets, "left"), &left) || number(r, attr(insets, "right"), &right) ||
          number(r, attr(insets, "bottom"), &bottom) || number(r, attr(insets, "top"), &top))
        return 1;
      t->point_count = 2;
      t->points[0] = (Point){.point = BW_ANCHOR_BOTTOMLEFT,
                             .relative_point = BW_ANCHOR_BOTTOMLEFT,
                             .relative = o,
                             .x = left,
                             .y = bottom};
      t->points[1] = (Point){.point = BW_ANCHOR_TOPRIGHT,
                             .relative_point = BW_ANCHOR_TOPRIGHT,
                             .relative = o,
                             .x = -right,
                             .y = -top};
      t->draw_layer = 4;
    }
  }
  if (o->type == BW_UI_SIMPLEHTML && attr(n, "file")) {
    HtmlAsset *asset = NULL;
    for (uint32_t i = 0; i < r->html_asset_count; i++)
      if (same_path(attr(n, "file"), r->html_assets[i].path))
        asset = &r->html_assets[i];
    if (asset) {
      const char *tags[] = {"FontString", "FontStringHeader1", "FontStringHeader2",
                            "FontStringHeader3"};
      for (uint32_t i = 0; i < 4; i++) {
        const BwXmlNode *descriptor = child(n, tags[i]);
        if (!descriptor)
          continue;
        BwXmlNode *style = clone(descriptor);
        if (!style)
          return fail(r, "HTML font allocation");
        free(style->name);
        style->name = copy("Font");
        if (!style->name) {
          release(style);
          return fail(r, "HTML font name allocation");
        }
        uint32_t start = r->count;
        int status = instantiate(r, style, NULL, depth + 1);
        release(style);
        if (status)
          return status;
        o->html_fonts[i] = r->objects[start];
      }
      o->font_object = o->html_fonts[0];
      if (html_text(o, asset->text))
        return 1;
    }
  }
  if ((o->type == 2 || o->type == 3) && o->text && o->text[0]) {
    if (!o->button_text) {
      o->button_text = create(r, "FontString", NULL, o);
      if (!o->button_text)
        return 1;
      o->button_text->font_object = o->normal_font;
      center_button_text(o, o->button_text);
    }
    if (replace_text(o->button_text, o->text))
      return 1;
  }
  if (r->auto_load) {
    o->loaded = 1;
    if (invoke(o, "OnLoad"))
      return 1;
    if (visible(o) && invoke(o, "OnShow"))
      return 1;
  }
  return 0;
}
static int instantiate(BwUiRuntime *r, const BwXmlNode *raw, Object *parent, uint32_t depth);
/* Diagnostic observer preserves swallowed errors as an explicit integration gate. */
static int protected_error(lua_State *L) {
  BwUiRuntime *r = current(L);
  if (!r->protected_failures) {
    const char *error = lua_tostring(L, 1);
    snprintf(r->protected_error, sizeof(r->protected_error), "%s",
             error ? error : "non-string protected error");
  }
  if (r->protected_failures < 0xffffffffu)
    r->protected_failures++;
  if (r->error_handler > 0)
    lua_rawgeti(L, LUA_REGISTRYINDEX, r->error_handler);
  else
    lua_pushnil(L);
  lua_pushvalue(L, 1);
  lua_call(L, 1, 1);
  return 1;
}
/* Selected 0x816f30 call/return boundary, without native result-taint tags. */
static int secure_call(lua_State *L) {
  BwUiRuntime *r = current(L);
  uint32_t context = bw_ui_execution_context(L);
  int count = lua_gettop(L);
  if (count && lua_isstring(L, 1)) {
    const char *name = lua_tostring(L, 1);
    lua_getglobal(L, name);
    lua_replace(L, 1);
  }
  if (!count) {
    lua_pushnil(L);
    count = 1;
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, r->handle_reference);
  lua_pushcclosure(L, protected_error, 1);
  lua_insert(L, 1);
  int status = lua_pcall(L, count - 1, LUA_MULTRET, 1);
  bw_ui_execution_swap(L, context);
  if (status) {
    lua_settop(L, 0);
    return 0;
  }
  lua_remove(L, 1);
  return lua_gettop(L);
}
static int error_handler(lua_State *L) {
  BwUiRuntime *r = current(L);
  if (lua_toboolean(L, lua_upvalueindex(2))) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (r->error_handler > 0)
      luaL_unref(L, LUA_REGISTRYINDEX, r->error_handler);
    r->error_handler = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
  }
  if (r->error_handler > 0)
    lua_rawgeti(L, LUA_REGISTRYINDEX, r->error_handler);
  else
    lua_pushnil(L);
  return 1;
}
static int screen(lua_State *L) {
  BwUiRuntime *r = current(L);
  if (r->viewport_height <= 0)
    return luaL_error(L, "viewport not configured");
  lua_pushnumber(L, lua_toboolean(L, lua_upvalueindex(2)) ? r->viewport_height : r->viewport_width);
  return 1;
}
static int registered_frames(lua_State *L) {
  BwUiRuntime *r = current(L);
  const char *event = luaL_checkstring(L, 1);
  int n = 0;
  for (uint32_t i = 0; i < r->subscription_count; i++)
    if (!strcmp(r->subscriptions[i].event, event)) {
      if (!lua_checkstack(L, 1))
        return luaL_error(L, "event result capacity");
      lua_rawgeti(L, LUA_REGISTRYINDEX, r->subscriptions[i].object->reference);
      n++;
    }
  return n;
}
static int create_frame(lua_State *L) {
  BwUiRuntime *r = current(L);
  const char *type = luaL_checkstring(L, 1);
  const char *name = luaL_optstring(L, 2, "");
  Object *parent = lua_isnoneornil(L, 3) ? NULL : owner(L, 3);
  if (strlen(name) >= MAX_NAME)
    return luaL_error(L, "frame name capacity");
  if (!frame_type(type_id(type)) || !type_id(type))
    return luaL_error(L, "invalid frame type");
  if (!lua_isnoneornil(L, 4) && *luaL_checkstring(L, 4)) {
    const char *templates = lua_tostring(L, 4);
    BwXmlAttribute inherits = {"inherits", (char *)templates, NULL},
                   named = {"name", (char *)name, &inherits};
    BwXmlNode raw = {(char *)type, NULL, &named, NULL, NULL};
    uint32_t first = r->count;
    if (instantiate(r, &raw, parent, 0))
      return luaL_error(L, "%s", r->error);
    lua_rawgeti(L, LUA_REGISTRYINDEX, r->objects[first]->reference);
    return 1;
  }
  Object *o = create(r, type, name, parent);
  if (!o)
    return luaL_error(L, "%s", r->error);
  lua_rawgeti(L, LUA_REGISTRYINDEX, o->reference);
  return 1;
}
static int enumerate(lua_State *L) {
  BwUiRuntime *r = current(L);
  uint32_t start = 0;
  if (!lua_isnoneornil(L, 1))
    start = owner(L, 1)->id + 1;
  for (uint32_t i = start; i < r->count; i++)
    if (frame_type(r->objects[i]->type)) {
      lua_rawgeti(L, LUA_REGISTRYINDEX, r->objects[i]->reference);
      return 1;
    }
  lua_pushnil(L);
  return 1;
}
BwUiRuntime *bw_ui_runtime_new(lua_State *L) {
  if (!L)
    return NULL;
  BwUiRuntime *r = calloc(1, sizeof(*r));
  if (r) {
    r->movie = bw_movie_create();
    if (!r->movie) {
      free(r);
      return NULL;
    }
    r->L = L;
    r->handle = lua_newuserdata(L, sizeof(*r->handle));
    *r->handle = r;
    r->handle_reference = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  return r;
}
uint32_t bw_ui_runtime_viewport(BwUiRuntime *r, double width, double height) {
  if (!r || !isfinite(width) || !isfinite(height) || width <= 0 || height <= 0)
    return 1;
  r->viewport_height = 768;
  r->viewport_width = 768 * (width / height);
  /* Full browser viewport policy: modal dimming must include wide side areas. */
  Object *root = find(r, "GlueParent");
  if (root)
    all_points(root, NULL);
  return 0;
}
uint32_t bw_ui_runtime_define_event(BwUiRuntime *r, const char *event) {
  if (!r || !event || !event[0] || strlen(event) >= 128 || r->event_count >= 1024)
    return 1;
  if (known_event(r, event))
    return 0;
  strcpy(r->events[r->event_count++], event);
  return 0;
}
uint32_t bw_ui_runtime_event(BwUiRuntime *r, const char *event, uint32_t arguments) {
  if (!r || !event || arguments > 1024 || arguments > (uint32_t)lua_gettop(r->L) ||
      r->event_depth >= 64)
    return 1;
  lua_State *L = r->L;
  int top = lua_gettop(L), base = top - (int)arguments;
  if (!known_event(r, event)) {
    lua_settop(L, base);
    return 0;
  }
  uint32_t previous_context = bw_ui_execution_swap(L, 0);
  r->event_depth++;
  Object *receivers[8192];
  uint32_t count = 0;
  for (uint32_t i = 0; i < r->subscription_count; i++)
    if (!strcmp(r->subscriptions[i].event, event))
      receivers[count++] = r->subscriptions[i].object;
  uint32_t status = 0;
  for (uint32_t i = 0; i < count; i++) {
    Object *o = receivers[i];
    uint32_t id = event_id(o, "OnEvent");
    if (!id)
      continue;
    if (!lua_checkstack(L, (int)arguments + 4)) {
      status = fail(r, "event stack capacity");
      break;
    }
    lua_pushstring(L, event);
    for (uint32_t j = 0; j < arguments; j++)
      lua_pushvalue(L, base + 1 + (int)j);
    status = bw_ui_call_script(L, &o->scripts[id], o->reference, arguments + 1, r->error,
                               sizeof(r->error));
    if (status)
      break;
  }
  lua_settop(L, base);
  r->event_depth--;
  bw_ui_execution_swap(L, previous_context);
  return status;
}
/* Native name resolution/type check -> selected model role globals:
 * 0x4e2f60/b6b1fc and 0x4e05b0/b6b184. No realm/character side effects. */
static int model_role(lua_State *L) {
  BwUiRuntime *r = current(L);
  const char *name = luaL_checkstring(L, 1);
  Object *o = find(r, name);
  uint32_t role = (uint32_t)lua_tointeger(L, lua_upvalueindex(2));
  if (o && (o->type == 8 || o->type == 9))
    r->model_roles[role] = o;
  return 0;
}
static int model_background(lua_State *L) {
  BwUiRuntime *r = current(L);
  uint32_t role = (uint32_t)lua_tointeger(L, lua_upvalueindex(2));
  Object *o = role < 2 ? r->model_roles[role] : NULL;
  const char *path = luaL_checkstring(L, 1);
  if (!o || strlen(path) >= sizeof(o->model_path))
    return luaL_error(L, "character model frame or path unavailable");
  int admitted = 0;
  for (uint32_t i = 0; i < r->model_asset_count; i++)
    if (same_path(r->model_assets[i], path)) {
      admitted = 1;
      break;
    }
  if (!admitted || r->model_count >= 512)
    return luaL_error(L, "character background resource not supplied");
  BwUiModelRequest request = {0};
  request.object = o->id;
  request.operation = BW_UI_MODEL_FILE;
  strcpy(request.path, path);
  strcpy(o->model_path, path);
  o->model_pending = 1;
  r->model_requests[(r->model_head + r->model_count) % 512] = request;
  r->model_count++;
  return 0;
}
uint32_t bw_ui_runtime_model_role(BwUiRuntime *r, uint32_t role) {
  return r && role < 2 && r->model_roles[role] ? r->model_roles[role]->id + 1 : 0;
}
static int credits_text(lua_State *L) {
  BwUiRuntime *r = current(L);
  int index = luaL_checkinteger(L, 1);
  const char *path = index == 1   ? "Credits.html"
                     : index == 2 ? "Credits_BC.html"
                     : index == 3 ? "Credits_LK.html"
                                  : NULL;
  if (!path)
    return luaL_error(L, "invalid credits edition");
  for (uint32_t i = 0; i < r->html_asset_count; i++)
    if (same_path(path, r->html_assets[i].path)) {
      lua_pushstring(L, r->html_assets[i].text);
      return 1;
    }
  return luaL_error(L, "local credits document unavailable");
}
static int cursor(lua_State *L) {
  BwUiRuntime *r = current(L);
  r->cursor_hidden = lua_toboolean(L, lua_upvalueindex(2));
  return 0;
}
static int cursor_position(lua_State *L) {
  BwUiRuntime *r = current(L);
  lua_pushnumber(L, r->cursor_x);
  lua_pushnumber(L, r->cursor_y);
  return 2;
}
double bw_ui_runtime_model_parameter(BwUiRuntime *r, uint32_t role, uint32_t field,
                                     uint32_t index) {
  Object *o = r && role < 2 ? r->model_roles[role] : NULL;
  if (!o)
    return 0;
  if (field == 0)
    return o->model_fog_enabled;
  if (field >= 1 && field <= 5)
    return o->model_fog[field - 1];
  if (field == 6)
    return o->model_glow;
  if (field == 7 && index < 6)
    return o->model_light_count[index];
  if (field == 8 && index < 6 * 4 * 13)
    return ((float *)o->model_lights)[index];
  if (field == 9)
    return o->model_advance_requested;
  return 0;
}
uint32_t bw_ui_runtime_register(BwUiRuntime *r) {
  if (!r)
    return 1;
  lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->handle_reference);
  lua_pushcclosure(r->L, cursor_position, 1);
  lua_setglobal(r->L, "GetCursorPosition");
  for (int i = 0; i < 2; i++) {
    lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->handle_reference);
    lua_pushboolean(r->L, i);
    lua_pushcclosure(r->L, cursor, 2);
    lua_setglobal(r->L, i ? "HideCursor" : "ShowCursor");
  }
  lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->handle_reference);
  lua_pushcclosure(r->L, credits_text, 1);
  lua_setglobal(r->L, "GetCreditsText");
  bw_ui_execution_register(r->L);
  lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->handle_reference);
  lua_pushcclosure(r->L, secure_call, 1);
  lua_setglobal(r->L, "securecall");
  const char *names[] = {"CreateFrame", "EnumerateFrames", "GetFramesRegisteredForEvent"};
  lua_CFunction functions[] = {create_frame, enumerate, registered_frames};
  for (uint32_t i = 0; i < 3; i++) {
    lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->handle_reference);
    lua_pushcclosure(r->L, functions[i], 1);
    lua_setglobal(r->L, names[i]);
  }
  for (int i = 0; i < 2; i++) {
    lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->handle_reference);
    lua_pushinteger(r->L, i);
    lua_pushcclosure(r->L, model_role, 2);
    lua_setglobal(r->L, i ? "SetCharCustomizeFrame" : "SetCharSelectModelFrame");
    lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->handle_reference);
    lua_pushinteger(r->L, i);
    lua_pushcclosure(r->L, model_background, 2);
    lua_setglobal(r->L, i ? "SetCharCustomizeBackground" : "SetCharSelectBackground");
  }
  for (int i = 0; i < 2; i++) {
    lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->handle_reference);
    lua_pushboolean(r->L, i);
    lua_pushcclosure(r->L, error_handler, 2);
    lua_setglobal(r->L, i ? "seterrorhandler" : "geterrorhandler");
    lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->handle_reference);
    lua_pushboolean(r->L, i);
    lua_pushcclosure(r->L, screen, 2);
    lua_setglobal(r->L, i ? "GetScreenHeight" : "GetScreenWidth");
  }
  return 0;
}
void bw_ui_runtime_dispatch(BwUiRuntime *r, BwUiXmlDispatch dispatch, void *context) {
  if (r) {
    r->dispatch = dispatch;
    r->dispatch_context = context;
  }
}
uint32_t bw_ui_runtime_document(BwUiRuntime *r, const BwXmlNode *root) {
  if (!r || !tag(root, "Ui"))
    return 1;
  r->error[0] = 0;
  for (const BwXmlNode *n = root->child; n; n = n->next) {
    const char *name = attr(n, "name");
    if (tag(n, "Script") || tag(n, "Include")) {
      if (!r->dispatch)
        return fail(r, "external/inline XML dispatch requires ordered loader");
      if (r->dispatch(r->dispatch_context, n))
        return fail(r, "XML dispatch failed");
      continue;
    }
    if (tag(n, "Font") || (attr(n, "virtual") && !strcmp(attr(n, "virtual"), "true"))) {
      if (!name || strlen(name) >= MAX_NAME || r->definitions >= MAX_DEFINITIONS)
        return fail(r, "definition name/capacity");
      if (definition(r, name))
        return fail(r, "duplicate definition");
      Definition *d = &r->definition[r->definitions++];
      strcpy(d->name, name);
      d->node = n;
      if (tag(n, "Font") && instantiate(r, n, NULL, 0))
        return 1;
    } else if (!r->template_only && instantiate(r, n, NULL, 0))
      return 1;
  }
  return 0;
}
static int load_object(Object *o, uint32_t depth) {
  BwUiRuntime *r = o->runtime;
  if (depth >= 128)
    return fail(r, "lifecycle nesting limit");
  for (uint32_t i = 0; i < r->count; i++)
    if (r->objects[i]->parent == o && load_object(r->objects[i], depth + 1))
      return 1;
  if (!o->loaded) {
    o->loaded = 1;
    if (invoke(o, "OnLoad"))
      return 1;
  }
  return 0;
}
void bw_ui_runtime_templates(BwUiRuntime *r, int enabled) {
  if (r)
    r->template_only = enabled != 0;
}
void bw_ui_runtime_defer_lifecycle(BwUiRuntime *r) {
  if (r) {
    r->auto_load = 0;
    r->deferred_lifecycle = 1;
  }
}
void bw_ui_runtime_auto_load(BwUiRuntime *r, int enabled) {
  if (r)
    r->auto_load = enabled != 0;
}
uint32_t bw_ui_runtime_load(BwUiRuntime *r) {
  if (!r || r->load_depth)
    return 1;
  r->load_depth = 1;
  for (uint32_t i = 0; i < r->count; i++)
    if (!r->objects[i]->parent && load_object(r->objects[i], 0)) {
      r->load_depth = 0;
      return 1;
    }
  r->load_depth = 0;
  return 0;
}
uint32_t bw_ui_runtime_model_asset(BwUiRuntime *r, const char *path) {
  if (!r || !path || !*path || strlen(path) >= 512 || r->model_asset_count >= 64)
    return 1;
  for (uint32_t i = 0; i < r->model_asset_count; i++)
    if (same_path(path, r->model_assets[i]))
      return 1;
  strcpy(r->model_assets[r->model_asset_count++], path);
  return 0;
}
uint32_t bw_ui_runtime_model_request(BwUiRuntime *r, BwUiModelRequest *out) {
  if (!r || !out || !r->model_count)
    return 1;
  *out = r->model_requests[r->model_head];
  r->model_head = (r->model_head + 1) % 512;
  r->model_count--;
  return 0;
}
uint32_t bw_ui_runtime_count(const BwUiRuntime *r) {
  return r ? r->count : 0;
}
const char *bw_ui_runtime_error(const BwUiRuntime *r) {
  return r ? r->error : "no frame runtime";
}
void bw_ui_runtime_free(BwUiRuntime *r) {
  if (!r)
    return;
  for (uint32_t i = 0; i < r->html_asset_count; i++)
    free(r->html_assets[i].text);
  bw_movie_destroy(r->movie);
  for (uint32_t i = 0; i < r->font_face_count; i++)
    bw_font_close(r->font_faces[i].font);
  for (uint32_t i = 0; i < r->font_asset_count; i++)
    free(r->font_assets[i].bytes);
  for (uint32_t i = 0; i < r->count; i++) {
    Object *o = r->objects[i];
    for (uint32_t j = 0; j < MAX_HANDLERS; j++)
      bw_ui_clear_script(r->L, &o->scripts[j]);
    if (o->name[0]) {
      lua_getglobal(r->L, o->name);
      lua_rawgeti(r->L, LUA_REGISTRYINDEX, o->reference);
      int own = lua_rawequal(r->L, -1, -2);
      lua_pop(r->L, 2);
      if (own) {
        lua_pushnil(r->L);
        lua_setglobal(r->L, o->name);
      }
    }
    luaL_unref(r->L, LUA_REGISTRYINDEX, o->reference);
    release(o->composed);
    free(o->edit);
    free(o->layout_lines);
    free(o->text);
    free(o->font_file);
    free(o->texture_file);
    free(o);
  }
  if (r->error_handler > 0)
    luaL_unref(r->L, LUA_REGISTRYINDEX, r->error_handler);
  for (int i = 0; i < 15; i++)
    if (r->metatables[i] > 0)
      luaL_unref(r->L, LUA_REGISTRYINDEX, r->metatables[i]);
  *r->handle = NULL;
  luaL_unref(r->L, LUA_REGISTRYINDEX, r->handle_reference);
  free(r);
}

uint32_t bw_ui_runtime_font_asset(BwUiRuntime *r, const char *path, const uint8_t *bytes,
                                  uint32_t length) {
  if (!r || !path || !*path || strlen(path) >= 256 || !bytes || length < 12 || length > 1048576 ||
      r->font_asset_count >= 8 || r->font_bytes + length > 8388608)
    return 1;
  for (uint32_t i = 0; i < r->font_asset_count; i++)
    if (same_path(path, r->font_assets[i].path))
      return 1;
  uint8_t *data = malloc(length);
  if (!data)
    return 1;
  memcpy(data, bytes, length);
  FontAsset *a = &r->font_assets[r->font_asset_count++];
  strcpy(a->path, path);
  a->bytes = data;
  a->length = length;
  r->font_bytes += length;
  return 0;
}

uint32_t bw_ui_runtime_protected_failures(const BwUiRuntime *r) {
  return r ? r->protected_failures : 0;
}
const char *bw_ui_runtime_protected_error(const BwUiRuntime *r) {
  return r ? r->protected_error : "no runtime";
}

uint32_t bw_ui_runtime_texture_asset(BwUiRuntime *r, const char *path, uint32_t width,
                                     uint32_t height) {
  if (!r || !path || !*path || strlen(path) >= 512 || r->texture_asset_count >= 2048 ||
      ((width == 0) != (height == 0)) || width > 512 || height > 512 || (width & (width - 1)) ||
      (height & (height - 1)))
    return 1;
  if (!width &&
      strncmp(path, "Interface\\Glues\\Credits\\", sizeof("Interface\\Glues\\Credits\\") - 1))
    return 1;
  for (uint32_t i = 0; i < r->texture_asset_count; i++)
    if (same_path(path, r->texture_assets[i].path))
      return 1;
  TextureAsset *asset = &r->texture_assets[r->texture_asset_count++];
  strcpy(asset->path, path);
  asset->width = width;
  asset->height = height;
  return 0;
}

/* Snapshot is not a second UI implementation: resolve the owned C tree and
 * report incomplete geometry explicitly. No script execution occurs here. */
static double draw_number(const BwXmlNode *n, const char *key, double fallback) {
  const char *s = attr(n, key);
  if (!s)
    return fallback;
  char *end;
  double value = strtod(s, &end);
  return *end || !isfinite(value) ? fallback : value;
}
static Object *draw_font_source(Object *o) {
  Object *parent = o->parent;
  if (parent && parent->button_text == o) {
    Object *f = !parent->enabled && parent->disabled_font ? parent->disabled_font
                : (parent->highlight_locked || parent->hovered) && parent->highlight_font
                    ? parent->highlight_font
                    : parent->normal_font;
    if (f)
      return font_source(f);
  }
  return font_source(o);
}
static int draw_visible(Object *o) {
  if (!visible(o))
    return 0;
  Object *p = o->parent;
  if (!p)
    return 1;
  for (int i = 0; i < 6; i++)
    if (p->button_textures[i] == o) {
      if (i == 0)
        return (!p->enabled && !p->button_textures[2]) ||
               (p->enabled && (!p->pressed || !p->button_textures[1]));
      if (i == 1)
        return p->enabled && p->pressed;
      if (i == 2)
        return !p->enabled;
      if (i == 4)
        return p->checked && (p->enabled || !p->button_textures[5]);
      if (i == 5)
        return p->checked && !p->enabled;
      return p->enabled && (p->highlight_locked || p->hovered);
    }
  return 1;
}
/* Authentication messages use blue buttons. Other GlueDialog uses (settings,
 * character creation and realm prompts) retain their original red artwork. */
static int authentication_dialog(Object *dialog) {
  lua_State *L = dialog->runtime->L;
  lua_rawgeti(L, LUA_REGISTRYINDEX, dialog->reference);
  lua_getfield(L, -1, "which");
  const char *which = lua_tostring(L, -1);
  int authentication =
      which && (!strcmp(which, "DISCONNECTED") || !strcmp(which, "PARENTAL_CONTROL") ||
                !strcmp(which, "CONNECTION_HELP") || !strcmp(which, "CONNECTION_HELP_HTML") ||
                !strcmp(which, "CLIENT_ACCOUNT_MISMATCH") || !strcmp(which, "CLIENT_TRIAL") ||
                !strcmp(which, "ACCOUNT_MSG"));
  Object *login = find(dialog->runtime, "AccountLogin");
  if (!authentication && which && login && visible(login))
    authentication = !strcmp(which, "CANCEL") || !strcmp(which, "OKAY") ||
                     !strcmp(which, "OKAY_HTML") || !strcmp(which, "OKAY_WITH_URL");
  lua_pop(L, 2);
  return authentication;
}
uint32_t bw_ui_runtime_draw_item(BwUiRuntime *r, uint32_t index, BwUiDrawItem *out) {
  if (!r || !out || index >= r->count)
    return 1;
  memset(out, 0, sizeof(*out));
  Object *o = r->objects[index], *f = draw_font_source(o), *shadow = o->has_shadow ? o : f;
  out->object = index;
  out->parent = o->parent ? o->parent->id : UINT32_MAX;
  out->type = o->type;
  out->name = o->name;
  out->html_loaded = o->html_loaded;
  out->enabled = o->enabled;
  out->pressed = o->pressed;
  out->hovered = o->hovered;
  out->mouse_enabled = (o->input_flags & 4) != 0;
  out->additive = attr(o->composed, "alphaMode") && !strcmp(attr(o->composed, "alphaMode"), "ADD");
  out->visible = draw_visible(o);
  out->texture = o->texture_file;
  /* Production snapshots do not need geometry for hidden research/UI trees. */
  if (r->options_readonly && !out->visible) {
    out->geometry_status = 2;
    return 0;
  }
  for (Object *branch = o, *p = o->parent; p; branch = p, p = p->parent)
    if (p->type == BW_UI_SCROLLFRAME && p->scroll_child == branch) {
      BwAnchorRect clip;
      if (rectangle(p, &clip, 0)) {
        out->geometry_status = 1;
        return 0;
      }
      if (!out->has_clip) {
        out->clip_left = clip.left;
        out->clip_bottom = clip.bottom;
        out->clip_width = clip.width;
        out->clip_height = clip.height;
        out->has_clip = 1;
      } else {
        double right = fmin(out->clip_left + out->clip_width, clip.left + clip.width),
               top = fmin(out->clip_bottom + out->clip_height, clip.bottom + clip.height);
        out->clip_left = fmax(out->clip_left, clip.left);
        out->clip_bottom = fmax(out->clip_bottom, clip.bottom);
        out->clip_width = fmax(0, right - out->clip_left);
        out->clip_height = fmax(0, top - out->clip_bottom);
      }
    }
  if (o->parent && o->parent->edit_display == o) {
    BwAnchorRect clip;
    if (rectangle(o, &clip, 0))
      return 1;
    out->has_clip = 1;
    out->clip_left = clip.left;
    out->clip_bottom = clip.bottom;
    out->clip_width = clip.width;
    out->clip_height = clip.height;
  }
  out->layer = o->draw_layer ? o->draw_layer : 3;
  out->stratum = o->frame_stratum;
  out->level = o->frame_level;
  if (!frame_type(o->type) && o->parent) {
    out->stratum = o->parent->frame_stratum;
    out->level = o->parent->frame_level;
  }
  out->own_alpha = o->alpha;
  out->alpha = 1;
  out->effective_scale = 1;
  for (Object *p = o; p; p = p->parent) {
    out->alpha *= p->alpha;
    out->effective_scale *= p->scale;
  }
  char previous[sizeof(r->error)];
  memcpy(previous, r->error, sizeof(previous));
  BwAnchorRect rect = {0};
  out->geometry_status = rectangle(o, &rect, 0);
  if (out->geometry_status)
    snprintf(out->geometry_error, sizeof(out->geometry_error), "%s", r->error);
  memcpy(r->error, previous, sizeof(previous));
  if (!out->geometry_status) {
    out->left = rect.left;
    out->bottom = rect.bottom;
    out->width = rect.width;
    out->height = rect.height;
    if (o->parent && o->parent->type == BW_UI_STATUSBAR && o->parent->bar_texture == o) {
      Object *bar = o->parent;
      double fraction = bar->range > 0 && bar->value_initialized
                            ? fmax(0, fmin(1, (bar->value - bar->minimum) / bar->range))
                            : 0;
      if (bar->slider_vertical)
        out->height *= fraction;
      else
        out->width *= fraction;
    }
  }
  out->texture = o->texture_file;
  /* Only authentication messages override the shared red dialog template.
   * Realm selection and post-login controls keep their source-defined colors. */
  for (Object *ancestor = o->parent; out->texture && ancestor; ancestor = ancestor->parent) {
    if (strcmp(ancestor->name, "GlueDialog"))
      continue;
    if (!authentication_dialog(ancestor))
      break;
    if (same_path(out->texture, "Interface\\Glues\\Common\\Glue-Panel-Button-Up"))
      out->texture = "Interface\\Glues\\Common\\Glue-Panel-Button-Up-Blue";
    else if (same_path(out->texture, "Interface\\Glues\\Common\\Glue-Panel-Button-Down"))
      out->texture = "Interface\\Glues\\Common\\Glue-Panel-Button-Down-Blue";
    else if (same_path(out->texture, "Interface\\Glues\\Common\\Glue-Panel-Button-Highlight"))
      out->texture = "Interface\\Glues\\Common\\Glue-Panel-Button-Highlight-Blue";
    break;
  }
  out->text = o->type == BW_UI_EDITBOX && o->edit_password ? NULL : o->text;
  out->font = f ? f->font_file : NULL;
  out->font_flags = f ? f->font_flags : NULL;
  out->font_height = f ? f->font_height : 0;
  out->outline = f ? (strstr(f->font_flags, "THICK")     ? 4
                      : strstr(f->font_flags, "OUTLINE") ? 2
                                                         : 0)
                   : 0;
  out->justify_h = o->justify_h;
  out->justify_v = o->justify_v;
  out->shadow_x = shadow ? shadow->shadow_x : 0;
  out->shadow_y = shadow ? shadow->shadow_y : 0;
  out->shadow_color[3] = 255;
  if (shadow) {
    const BwXmlNode *sc = child(child(shadow->composed, "Shadow"), "Color");
    if (sc) {
      const char *keys[4] = {"r", "g", "b", "a"};
      for (int i = 0; i < 4; i++) {
        double value = draw_number(sc, keys[i], i == 3 ? 1 : 0);
        if (value < 0)
          value = 0;
        if (value > 1)
          value = 1;
        out->shadow_color[i] = (uint8_t)(value * 255 + 0.5);
      }
    }
  }
  memcpy(out->uv, o->uv, sizeof(out->uv));
  memcpy(out->color, !o->has_color && f ? f->vertex_color : o->vertex_color, 4);
  memcpy(out->backdrop_color, o->backdrop_color, 4);
  memcpy(out->border_color, o->border_color, 4);
  const BwXmlNode *b = child(o->composed, "Backdrop");
  out->backdrop = o->has_backdrop;
  if (b) {
    out->backdrop_bg = attr(b, "bgFile");
    out->backdrop_edge = attr(b, "edgeFile");
    out->tile = attr(b, "tile") && !strcmp(attr(b, "tile"), "true");
    const BwXmlNode *e = child(child(b, "EdgeSize"), "AbsValue"),
                    *t = child(child(b, "TileSize"), "AbsValue"),
                    *in = child(child(b, "BackgroundInsets"), "AbsInset");
    out->edge_size = draw_number(e, "val", 0);
    out->tile_size = draw_number(t, "val", 0);
    out->inset_left = draw_number(in, "left", 0);
    out->inset_right = draw_number(in, "right", 0);
    out->inset_top = draw_number(in, "top", 0);
    out->inset_bottom = draw_number(in, "bottom", 0);
  }
  return 0;
}
uint32_t bw_ui_runtime_draw_glyph(BwUiRuntime *r, uint32_t index, uint32_t cp, BwFontGlyph *out,
                                  uint8_t *coverage, uint32_t capacity) {
  if (!r || index >= r->count || !out)
    return 1;
  Object *o = r->objects[index], *f = draw_font_source(o);
  if (!f)
    return fail(r, "unavailable draw font descriptor");
  BwFont *font;
  if (object_font(f, &font))
    return 1;
  return bw_font_glyph(font, cp, out, coverage, capacity);
}

static uint32_t draw_cp(const uint8_t *s, uint32_t n, uint32_t *at, uint32_t *cp) {
  if (*at >= n)
    return 2;
  uint32_t c = s[(*at)++];
  if (c < 128) {
    *cp = c;
    return c ? 0 : 1;
  }
  uint32_t count = c >= 0xc2 && c <= 0xdf   ? 1
                   : c >= 0xe0 && c <= 0xef ? 2
                                            : 0,
           min = count == 1 ? 0x80 : 0x800;
  if (!count || count > n - *at)
    return 1;
  c &= count == 1 ? 31 : 15;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t b = s[(*at)++];
    if ((b & 0xc0) != 0x80)
      return 1;
    c = (c << 6) | (b & 63);
  }
  if (c < min || (c >= 0xd800 && c <= 0xdfff))
    return 1;
  *cp = c;
  return 0;
}
static int color_hex(uint8_t c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                : -1;
}
/* Original inline ARGB/reset syntax, using the same token boundaries as layout. */
static int draw_token(const uint8_t *s, uint32_t n, uint32_t *at, uint32_t *cp, uint8_t color[4],
                      const uint8_t base[4]) {
  while (*at < n) {
    if (s[*at] != '|')
      return draw_cp(s, n, at, cp);
    uint32_t pos = *at;
    if (pos + 1 >= n)
      return 2;
    uint8_t code = s[pos + 1];
    if (code == 'n') {
      *at += 2;
      *cp = 10;
      return 0;
    }
    if (code == '|') {
      *at += 2;
      *cp = '|';
      return 0;
    }
    if (code == 'r') {
      memcpy(color, base, 4);
      *at += 2;
      continue;
    }
    if (code != 'c' || n - pos < 10)
      return 2;
    uint8_t argb[4];
    for (int i = 0; i < 4; i++) {
      int a = color_hex(s[pos + 2 + i * 2]), b = color_hex(s[pos + 3 + i * 2]);
      if (a < 0 || b < 0)
        return 2;
      argb[i] = (uint8_t)(a * 16 + b);
    }
    color[0] = argb[1];
    color[1] = argb[2];
    color[2] = argb[3];
    color[3] = (uint8_t)((argb[0] * (uint32_t)base[3] + 127) / 255);
    *at += 10;
  }
  return 1;
}
/* Selected C edit presentation: source font/insets, single line and a
 * clipped caret/selection. Native blink timing/pixels remain unvalidated. */
static int edit_prefix(Object *o, uint32_t bytes, double *out) {
  Object *t = o->edit_display;
  if (!t || bytes > o->edit->length)
    return 1;
  char text[BW_EDIT_CAPACITY + 1];
  if (o->edit_password)
    memset(text, '*', bytes);
  else
    memcpy(text, o->edit->bytes, bytes);
  text[bytes] = 0;
  BwFont *font;
  Object *f = draw_font_source(t);
  if (!f || object_font(f, &font))
    return 1;
  float width = 0;
  uint32_t outline = strstr(f->font_flags, "THICK") ? 4 : strstr(f->font_flags, "OUTLINE") ? 2 : 0;
  if (bw_font_text_extent(font, (const uint8_t *)text, bytes, outline, 0, 1, &width))
    return 1;
  *out = width;
  return 0;
}
static int edit_scroll(Object *o, double *out) {
  BwAnchorRect box;
  double cursor;
  if (!o->edit_display || rectangle(o->edit_display, &box, 0) ||
      edit_prefix(o, o->edit->cursor, &cursor))
    return 1;
  *out = fmax(0, cursor - fmax(0, box.width - 1));
  return 0;
}
uint32_t bw_ui_runtime_text_quad(BwUiRuntime *r, uint32_t index, uint32_t wanted, BwUiTextQuad *out,
                                 uint8_t *coverage, uint32_t capacity) {
  if (!r || index >= r->count || !out)
    return 1;
  Object *o = r->objects[index];
  if (o->type != 12)
    return 1;
  BwUiDrawItem item;
  if (bw_ui_runtime_draw_item(r, index, &item) || item.geometry_status)
    return 1;
  if (!o->text || !o->text[0])
    return 2;

  uint32_t size = (uint32_t)strlen(o->text), limit = size + 1;
  if (limit > 4096)
    limit = 4096;
  BwTextLine *lines = calloc(limit, sizeof(*lines));
  if (!lines)
    return fail(r, "text draw allocation");
  BwTextLayout layout;
  uint32_t result = 1;
  if (object_layout(o, lines, limit, &layout))
    goto done;
  Object *f = draw_font_source(o);
  BwFont *font;
  if (!f || object_font(f, &font))
    goto done;
  double top = item.bottom + item.height;
  if (!strcmp(item.justify_v, "MIDDLE"))
    top -= (item.height - layout.height) / 2;
  else if (!strcmp(item.justify_v, "BOTTOM"))
    top = item.bottom + layout.height;
  const uint8_t *text = (const uint8_t *)o->text;
  uint32_t count = 0;
  for (uint32_t line = 0; line < layout.lines; line++) {
    BwTextLine *l = &lines[line];
    double pen = item.left;
    if (!strcmp(item.justify_h, "CENTER"))
      pen += (item.width - l->width) / 2;
    else if (!strcmp(item.justify_h, "RIGHT"))
      pen += item.width - l->width;
    if (o->parent && o->parent->edit_display == o) {
      double scroll;
      if (edit_scroll(o->parent, &scroll))
        goto done;
      pen -= scroll;
    }
    double baseline = top - line * (item.font_height + o->font_spacing) - bw_font_ascent(font) +
                      (o->parent && o->parent->edit_display == o ? 0 : 1);
    if (o->parent && o->parent->edit_display == o && !strcmp(item.justify_v, "MIDDLE"))
      baseline = item.bottom + item.height / 2 - (bw_font_ascent(font) + bw_font_descent(font)) / 2;
    uint32_t at = l->start, end = at + l->length, cp, previous = 0;
    float previous_advance = 0;
    uint8_t text_color[4];
    memcpy(text_color, item.color, 4);
    uint32_t prefix = 0;
    while (prefix < at) {
      uint32_t ignored;
      if (draw_token(text, at, &prefix, &ignored, text_color, item.color) > 1)
        goto done;
    }
    uint32_t dots = o->layout_ellipsis && line + 1 == layout.lines ? 3 : 0;
    while (at < end || dots) {
      int token_status;
      if (at < end) {
        token_status = draw_token(text, end, &at, &cp, text_color, item.color);
        if (token_status == 1)
          continue;
        if (token_status)
          goto done;
      } else {
        cp = '.';
        dots--;
      }
      if (cp == 10)
        continue;
      if (previous) {
        int32_t units;
        if (bw_font_kerning(font, previous, cp, &units))
          goto done;
        if (units > 0)
          units = 0;
        pen += ceilf(previous_advance + units * (float)item.font_height / bw_font_units(font));
      }
      uint32_t width;
      float advance, bearing;
      if (bw_font_client_glyph(font, cp, item.outline, &width, &advance, &bearing))
        goto done;
      uint32_t parts = item.outline ? 2 : 1, first = count;
      count += parts;
      if (wanted >= first && wanted < count) {
        BwFontGlyph glyph;
        if (bw_font_glyph(font, cp, &glyph, coverage, capacity))
          goto done;
        *out = (BwUiTextQuad){
            cp, glyph.width, glyph.rows, pen + glyph.left, baseline + glyph.top - glyph.rows, {0}};
        memcpy(out->color, text_color, 4);
        if (item.outline && wanted == first && glyph.width && glyph.rows) {
          /* C-owned black coverage border. Width already participates in native-style
      * metrics; no browser text/CSS stroke or change to text advance. Exact
      * native packing/raster pixels remain a separate comparison gate. */
          uint32_t radius = item.outline / 2, w = glyph.width + 2 * radius,
                   h = glyph.rows + 2 * radius;
          if (w * h > capacity)
            goto done;
          uint8_t *source = malloc(glyph.width * glyph.rows);
          if (!source)
            goto done;
          memcpy(source, coverage, glyph.width * glyph.rows);
          memset(coverage, 0, w * h);
          for (uint32_t y = 0; y < glyph.rows; y++)
            for (uint32_t x = 0; x < glyph.width; x++) {
              uint8_t value = source[y * glyph.width + x];
              for (uint32_t dy = 0; dy <= 2 * radius; dy++)
                for (uint32_t dx = 0; dx <= 2 * radius; dx++) {
                  uint32_t at = (y + dy) * w + x + dx;
                  if (coverage[at] < value)
                    coverage[at] = value;
                }
            }
          free(source);
          out->left -= radius;
          out->bottom -= radius;
          out->width = w;
          out->height = h;
          out->color[0] = out->color[1] = out->color[2] = 0;
        }
        result = 0;
        goto done;
      }
      previous = cp;
      previous_advance = advance;
    }
  }
  result = 2;
done:
  free(lines);
  return result;
}

static int draw_compare(const void *a, const void *b) {
  Object *x = *(Object *const *)a, *y = *(Object *const *)b,
         *xf = !frame_type(x->type) && x->parent ? x->parent : x,
         *yf = !frame_type(y->type) && y->parent ? y->parent : y;
  int64_t d = (int64_t)xf->frame_stratum - yf->frame_stratum;
  if (!d)
    d = (int64_t)xf->frame_level - yf->frame_level;
  if (!d)
    d = (int64_t)(x->has_backdrop ? 0
                  : x->draw_layer ? x->draw_layer
                                  : 3) -
        (y->has_backdrop ? 0
         : y->draw_layer ? y->draw_layer
                         : 3);
  if (!d)
    d = (int64_t)x->id - y->id;
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}
uint32_t bw_ui_runtime_draw_order(BwUiRuntime *r, uint32_t *out, uint32_t capacity) {
  if (!r || !out || capacity < r->count)
    return UINT32_MAX;
  Object **list = malloc(r->count * sizeof(*list));
  if (!list && r->count)
    return UINT32_MAX;
  uint32_t n = 0;
  for (uint32_t i = 0; i < r->count; i++) {
    Object *o = r->objects[i];
    if (draw_visible(o) && (o->type == 7 || o->type == 11 || o->type == 12 || o->has_backdrop))
      list[n++] = o;
  }
  qsort(list, n, sizeof(*list), draw_compare);
  for (uint32_t i = 0; i < n; i++)
    out[i] = list[i]->id;
  free(list);
  return n;
}

uint32_t bw_ui_runtime_image_quad(BwUiRuntime *r, uint32_t index, uint32_t part,
                                  BwUiImageQuad *out) {
  BwUiDrawItem d;
  if (!out || bw_ui_runtime_draw_item(r, index, &d) || d.geometry_status)
    return 1;
  memset(out, 0, sizeof(*out));
  out->left = d.left;
  out->bottom = d.bottom;
  out->width = d.width;
  out->height = d.height;
  float uv[8] = {0, 0, 0, 1, 1, 0, 1, 1};
  memcpy(out->uv, uv, sizeof(uv));
  if (d.type == BW_UI_EDITBOX && part == 9) {
    Object *o = r->objects[index];
    if (r->focus != o || !o->edit_display)
      return 3;
    BwAnchorRect box;
    double a, b, scroll;
    if (rectangle(o->edit_display, &box, 0) || edit_scroll(o, &scroll))
      return 1;
    int selected = o->edit->start != o->edit->end;
    uint32_t start = selected ? o->edit->start : o->edit->cursor,
             end = selected ? o->edit->end : o->edit->cursor;
    if (edit_prefix(o, start, &a) || edit_prefix(o, end, &b))
      return 1;
    Object *f = draw_font_source(o->edit_display);
    if (!f)
      return 1;
    double left = box.left + a - scroll, right = box.left + b - scroll + (selected ? 0 : 1);
    left = fmax(box.left, left);
    right = fmin(box.left + box.width, right);
    out->left = left;
    out->bottom = box.bottom + fmax(0, (box.height - f->font_height) / 2);
    out->width = fmax(0, right - left);
    out->height = fmin(box.height, f->font_height);
    out->asset = "@solid";
    out->color[0] = selected ? 50 : 255;
    out->color[1] = selected ? 100 : 255;
    out->color[2] = 255;
    out->color[3] = selected ? 128 : 255;
    return out->width > 0 && out->height > 0 ? 0 : 3;
  }
  if (d.type == 11) {
    if (part)
      return 2;
    out->asset = d.texture;
    out->desaturated = r->objects[index]->desaturated;
    memcpy(out->uv, d.uv, sizeof(d.uv));
    memcpy(out->color, d.color, 4);
    const BwXmlNode *gradient = child(r->objects[index]->composed, "Gradient");
    if (gradient) {
      const char *orientation = attr(gradient, "orientation");
      int horizontal = orientation && !strcmp(orientation, "HORIZONTAL");
      const char *keys[] = {"r", "g", "b", "a"};
      for (uint32_t corner = 0; corner < 4; corner++) {
        int maximum = horizontal ? corner >= 2 : !(corner & 1);
        const BwXmlNode *stop = child(gradient, maximum ? "MaxColor" : "MinColor");
        for (uint32_t channel = 0; channel < 4; channel++) {
          double value = 1;
          if (number(r, attr(stop, keys[channel]), &value) || value < 0 || value > 1)
            return fail(r, "invalid texture gradient color");
          out->vertex_colors[corner * 4 + channel] = (uint8_t)(d.color[channel] * value + .5);
        }
      }
      out->has_vertex_colors = 1;
    }
    if (!out->asset || !*out->asset) {
      if (!r->objects[index]->has_color)
        return 3;
      out->asset = "@solid";
    }
    return 0;
  }
  if (!d.backdrop)
    return 2;
  if (part > 8)
    return 2;
  if (!part) {
    out->asset = d.backdrop_bg;
    if (!out->asset || !*out->asset)
      return 3;
    out->left += d.inset_left;
    out->bottom += d.inset_bottom;
    out->width -= d.inset_left + d.inset_right;
    out->height -= d.inset_top + d.inset_bottom;
    memcpy(out->color, d.backdrop_color, 4);
    if (d.tile) {
      if (d.tile_size <= 0)
        return 1;
      out->uv[3] = out->uv[7] = out->height / d.tile_size;
      out->uv[4] = out->uv[6] = out->width / d.tile_size;
    }
  } else {
    out->asset = d.backdrop_edge;
    if (!out->asset || !*out->asset)
      return 3;
    if (d.edge_size <= 0)
      return 1;
    double x = fmin(d.edge_size, d.width / 2), y = fmin(d.edge_size, d.height / 2);
    uint32_t i = part - 1;
    double u = (double)i / 8, v = (double)(i + 1) / 8;
    float edgeuv[8] = {u, 0, u, 1, v, 0, v, 1};
    if (i == 2 || i == 3) {
      float horizontal[8] = {u, 1, v, 1, u, 0, v, 0};
      memcpy(edgeuv, horizontal, sizeof(edgeuv));
    }
    memcpy(out->uv, edgeuv, sizeof(edgeuv));
    memcpy(out->color, d.border_color, 4);
    switch (i) {
    case 0:
      out->bottom += y;
      out->width = x;
      out->height -= 2 * y;
      break;
    case 1:
      out->left += d.width - x;
      out->bottom += y;
      out->width = x;
      out->height -= 2 * y;
      break;
    case 2:
      out->left += x;
      out->bottom += d.height - y;
      out->width -= 2 * x;
      out->height = y;
      break;
    case 3:
      out->left += x;
      out->width -= 2 * x;
      out->height = y;
      break;
    default:
      if (i == 5 || i == 7)
        out->left += d.width - x;
      if (i == 4 || i == 5)
        out->bottom += d.height - y;
      out->width = x;
      out->height = y;
      break;
    }
  }
  return out->width > 0 && out->height > 0 ? 0 : 3;
}

BwMovie *bw_ui_runtime_movie(BwUiRuntime *r) {
  return r ? r->movie : NULL;
}
uint32_t bw_ui_runtime_movie_complete(BwUiRuntime *r, uint32_t token) {
  uint32_t object;
  if (!r)
    return 1;
  if (!bw_movie_complete(r->movie, token, &object))
    return 2;
  if (!object || object > r->count || r->objects[object - 1]->type != BW_UI_MOVIEFRAME)
    return 1;
  return invoke(r->objects[object - 1], "OnMovieFinished");
}
uint32_t bw_ui_runtime_cursor_hidden(BwUiRuntime *r) {
  return r && r->cursor_hidden ? 1 : 0;
}

/* Selected deferred scroll update: own + descendant region bounds, not a
 * guessed SimpleHTML automatic height. Unit-scale profile only. */
static int content_bounds(Object *o, BwAnchorRect *out, uint32_t depth) {
  if (depth >= 128)
    return fail(o->runtime, "scroll content depth");
  int status = rectangle(o, out, 0);
  if (status)
    return status;
  /* Text and texture regions are leaves. Scanning all objects for each
   * paragraph made a scrolling credits document quadratic in tree size. */
  if (o->type == 11 || o->type == 12 || o->type == 13)
    return 0;
  for (uint32_t i = 0; i < o->runtime->count; i++) {
    Object *c = o->runtime->objects[i];
    if (c->parent != o || !c->shown || c->type == 13)
      continue;
    BwAnchorRect b;
    status = content_bounds(c, &b, depth + 1);
    if (status == 2)
      continue;
    if (status)
      return status;
    double right = fmax(out->left + out->width, b.left + b.width),
           top = fmax(out->bottom + out->height, b.bottom + b.height);
    out->left = fmin(out->left, b.left);
    out->bottom = fmin(out->bottom, b.bottom);
    out->width = right - out->left;
    out->height = top - out->bottom;
  }
  return 0;
}
uint32_t bw_ui_runtime_update_scroll(BwUiRuntime *r) {
  if (!r)
    return 1;
  for (uint32_t i = 0; i < r->count; i++) {
    Object *o = r->objects[i];
    if (o->type != BW_UI_SCROLLFRAME || !o->scroll_child || !visible(o))
      continue;
    BwAnchorRect frame, content;
    if (o->scale != 1)
      return fail(r, "scaled scroll update not reconstructed");
    int status = rectangle(o, &frame, 0);
    if (status == 2)
      continue;
    if (!status)
      status = content_bounds(o->scroll_child, &content, 0);
    if (status == 2)
      continue; /* Unpositioned content has no scroll extent yet. */
    if (status) {
      char message[1024];
      snprintf(message, sizeof(message), "scroll %.96s: %.880s", o->name, r->error);
      return fail(r, message);
    }
    double x = fmax(0, content.width - frame.width), y = fmax(0, content.height - frame.height);
    if (fabs(x - o->scroll_range_x) > .002 || fabs(y - o->scroll_range_y) > .002) {
      o->scroll_range_x = x;
      o->scroll_range_y = y;
      uint32_t id = event_id(o, "OnScrollRangeChanged");
      if (id) {
        lua_pushnumber(r->L, x);
        lua_pushnumber(r->L, y);
        if (bw_ui_call_script(r->L, &o->scripts[id], o->reference, 2, r->error, sizeof(r->error)))
          return 1;
      }
    }
  }
  return 0;
}
uint32_t bw_ui_runtime_html_asset(BwUiRuntime *r, const char *path, const uint8_t *bytes,
                                  uint32_t length) {
  if (!r || !path || !*path || strlen(path) >= 64 || !bytes || !length || length > 1048576 ||
      r->html_asset_count >= 8 || memchr(bytes, 0, length))
    return 1;
  for (const char *p = path; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
          *p == '.' || *p == '_' || *p == '-'))
      return 1;
  for (uint32_t i = 0; i < r->html_asset_count; i++)
    if (same_path(path, r->html_assets[i].path))
      return 1;
  char *text = malloc((size_t)length + 1);
  if (!text)
    return 1;
  memcpy(text, bytes, length);
  text[length] = 0;
  BwHtmlDocument doc;
  if (bw_html_parse(text, &doc) || doc.literal_fallback) {
    bw_html_free(&doc);
    free(text);
    return 1;
  }
  bw_html_free(&doc);
  HtmlAsset *a = &r->html_assets[r->html_asset_count++];
  strcpy(a->path, path);
  a->text = text;
  return 0;
}

static int mouse_script(Object *o, const char *event) {
  uint32_t id = event_id(o, event);
  if (!id || o->scripts[id].reference <= 0)
    return 0;
  BwUiRuntime *r = o->runtime;
  lua_pushstring(r->L, "LeftButton");
  return bw_ui_call_script(r->L, &o->scripts[id], o->reference, 1, r->error, sizeof(r->error));
}
/* Pointer capture keeps the original model-panel Lua rotation active until up. */
uint32_t bw_ui_runtime_input_at(BwUiRuntime *r, uint32_t kind, double x, double y, int32_t delta) {
  if (!r || (kind != 1 && kind != 2 && kind != 5 && kind != 6 && kind != 7 && kind != 8) ||
      !isfinite(x) || !isfinite(y) || delta < -16 || delta > 16)
    return 1;
  r->last_input_target = 0;
  r->cursor_x = x;
  r->cursor_y = y;
  if (kind == 7) {
    Object *capture = r->mouse_capture;
    r->mouse_capture = NULL;
    r->drag_slider = NULL;
    if (r->pressed_button)
      r->pressed_button->pressed = 0;
    r->pressed_button = NULL;
    return capture ? mouse_script(capture, "OnMouseUp") : 0;
  }
  if (kind == 6) {
    if (r->mouse_capture) {
      if (!visible(r->mouse_capture)) {
        Object *capture = r->mouse_capture;
        r->mouse_capture = NULL;
        return mouse_script(capture, "OnMouseUp");
      }
      r->last_input_target = r->mouse_capture->id + 1;
      return 0;
    }
    if (r->pressed_button) {
      Object *o = r->pressed_button;
      BwAnchorRect box;
      if (rectangle(o, &box, 0))
        return 1;
      o->pressed = visible(o) && o->enabled && x >= box.left && x < box.left + box.width &&
                   y >= box.bottom && y < box.bottom + box.height;
      r->last_input_target = o->id + 1;
      return 0;
    }
    Object *o = r->drag_slider;
    if (!o || !visible(o) || !o->enabled)
      return 2;
    BwAnchorRect box;
    if (rectangle(o, &box, 0))
      return 1;
    double length = o->slider_vertical ? box.height : box.width,
           thumb = o->thumb ? (o->slider_vertical ? o->thumb->height : o->thumb->width) : 0;
    double travel = length - thumb;
    if (travel <= 0)
      return 2;
    double t = o->slider_vertical ? (box.bottom + box.height - y - r->drag_offset) / travel
                                  : (x - r->drag_offset - box.left) / travel;
    t = fmax(0, fmin(1, t));
    r->last_input_target = o->id + 1;
    return slider_value(o, o->minimum + o->range * t) ? 1 : 0;
  }

  if (kind == 8 && r->hover_button) {
    r->hover_button->hovered = 0;
    r->hover_button = NULL;
  }
  if (x < 0 || y < 0 || x >= r->viewport_width || y >= r->viewport_height)
    return kind == 8 ? 0 : 2;
  Object *target = NULL;
  for (uint32_t i = 0; i < r->count; i++) {
    Object *o = r->objects[i];
    if (!visible(o) || !frame_type(o->type))
      continue;
    uint32_t id = kind == 2 ? event_id(o, "OnMouseWheel") : 0;
    if (kind == 8   ? (o->type != BW_UI_BUTTON && o->type != BW_UI_CHECKBUTTON)
        : kind == 5 ? (o->type != BW_UI_SLIDER && o->type != BW_UI_BUTTON &&
                       o->type != BW_UI_CHECKBUTTON && !(o->input_flags & 4))
        : kind == 1
            ? (o->type != BW_UI_BUTTON && o->type != BW_UI_CHECKBUTTON && o->type != BW_UI_EDITBOX)
            : !id || o->scripts[id].reference <= 0)
      continue;
    BwUiDrawItem item;
    if (bw_ui_runtime_draw_item(r, i, &item))
      return fail(r, "input target snapshot unavailable");
    if (item.geometry_status == 2)
      continue;
    if (item.geometry_status)
      return fail(r, "input target geometry unavailable");
    if (item.effective_scale != 1)
      return fail(r, "scaled input geometry not reconstructed");
    if (x < item.left || y < item.bottom || x >= item.left + item.width ||
        y >= item.bottom + item.height)
      continue;
    if (item.has_clip &&
        (x < item.clip_left || y < item.clip_bottom || x >= item.clip_left + item.clip_width ||
         y >= item.clip_bottom + item.clip_height))
      continue;
    if (!target || draw_compare(&target, &o) < 0)
      target = o;
  }

  if (kind == 8) {
    if (r->hover_button)
      r->hover_button->hovered = 0;
    r->hover_button = target && target->enabled ? target : NULL;
    if (r->hover_button) {
      r->hover_button->hovered = 1;
      r->last_input_target = target->id + 1;
    }
    return 0;
  }
  if (!target)
    return 2;
  r->last_input_target = target->id + 1;
  if (kind == 5 && target->type != BW_UI_SLIDER && target->type != BW_UI_BUTTON &&
      target->type != BW_UI_CHECKBUTTON) {
    r->mouse_capture = target;
    return mouse_script(target, "OnMouseDown");
  }
  if (kind == 5 && (target->type == BW_UI_BUTTON || target->type == BW_UI_CHECKBUTTON)) {
    if (!target->enabled)
      return 2;
    if (r->pressed_button)
      r->pressed_button->pressed = 0;
    r->pressed_button = target;
    target->pressed = 1;
    return 0;
  }
  if (kind == 5) {
    if (!target->enabled || !target->range_initialized || !target->value_initialized)
      return 2;
    r->drag_slider = target;
    BwAnchorRect thumb;
    if (target->thumb && !rectangle(target->thumb, &thumb, 0) && x >= thumb.left &&
        x < thumb.left + thumb.width && y >= thumb.bottom && y < thumb.bottom + thumb.height)
      r->drag_offset = target->slider_vertical ? thumb.bottom + thumb.height - y : x - thumb.left;
    else {
      r->drag_offset =
          target->thumb
              ? (target->slider_vertical ? target->thumb->height : target->thumb->width) / 2
              : 0;
      return bw_ui_runtime_input_at(r, 6, x, y, 0);
    }
    return 0;
  }
  if (kind == 1) {
    if (target->type == BW_UI_EDITBOX)
      return !target->enabled ? 2 : set_focus(r, target) ? 1 : 0;
    if (!target->enabled ||
        (target->click_registered && !(target->click_mask & ((uint64_t)1 << 31))))
      return 2;
    return click_button(target, "LeftButton", 0) ? 1 : 0;
  }
  if (!delta)
    return 2;
  uint32_t id = event_id(target, "OnMouseWheel");
  lua_pushnumber(r->L, delta);
  return bw_ui_call_script(r->L, &target->scripts[id], target->reference, 1, r->error,
                           sizeof(r->error))
             ? 1
             : 0;
}
uint32_t bw_ui_runtime_options_readonly(BwUiRuntime *r) {
  if (!r)
    return 1;
  r->options_readonly = 1;
  return 0;
}
uint32_t bw_ui_runtime_input_target(BwUiRuntime *r) {
  return r ? r->last_input_target : 0;
}

/* Browser text service policy; not a reconstruction of Win32 key dispatch. */
uint32_t bw_ui_runtime_edit_action(BwUiRuntime *r, uint32_t operation, const char *text,
                                   uint32_t length) {
  if (!r)
    return 1;
  Object *o = r->focus;
  if (!o || o->type != BW_UI_EDITBOX || !visible(o) || !o->enabled)
    return 2;
  if (operation == 1) {
    if (!text || length > BW_EDIT_CAPACITY)
      return 1;
    for (uint32_t i = 0; i < length; i++)
      if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126)
        return fail(r, "browser edit profile accepts ASCII only");
    return edit_insert(o, text, length, 1);
  }
  if (operation == 8)
    return invoke(o, "OnTabPressed");
  if (operation == 9)
    return invoke(o, "OnEnterPressed");
  if (operation == 10)
    return invoke(o, "OnEscapePressed");
  if (operation == 11)
    return bw_edit_select(o->edit, 0, -1);
  if (operation == 2 || operation == 3) {
    char before[BW_EDIT_CAPACITY + 1];
    strcpy(before, o->edit->bytes);
    if (bw_edit_delete(o->edit, operation == 3))
      return fail(r, "edit deletion rejected");
    int changed = strcmp(before, o->edit->bytes) != 0;
    if (changed)
      o->edit_user = 1;
    return edit_publish(o, changed);
  }
  int32_t cursor = (int32_t)o->edit->cursor;
  if (operation == 4)
    cursor--;
  else if (operation == 5)
    cursor++;
  else if (operation == 6)
    cursor = 0;
  else if (operation == 7)
    cursor = (int32_t)o->edit->length;
  else
    return fail(r, "unsupported edit key");
  return bw_edit_cursor(o->edit, cursor);
}
uint32_t bw_ui_runtime_edit_pump(BwUiRuntime *r) {
  if (!r)
    return 1;
  for (uint32_t round = 0; round < 64; round++) {
    uint32_t pending = 0;
    for (uint32_t i = 0; i < r->count; i++) {
      Object *o = r->objects[i];
      if (!o->edit || !o->edit_dirty || !visible(o))
        continue;
      pending++;
      int user = o->edit_user;
      o->edit_dirty = o->edit_user = 0;
      uint32_t id = event_id(o, "OnTextChanged");
      lua_pushboolean(r->L, user);
      if (bw_ui_call_script(r->L, &o->scripts[id], o->reference, 1, r->error, sizeof(r->error)))
        return 1;
    }
    if (!pending)
      return 0;
  }
  return fail(r, "edit update cycle limit");
}
uint32_t bw_ui_runtime_edit_focus(BwUiRuntime *r) {
  return r && r->focus ? r->focus->id + 1 : 0;
}
