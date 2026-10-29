/* Included by the reusable frame test adapter; field maps documented below. */
static BwUiDrawItem draw_item;
static BwFontGlyph draw_glyph;
static uint8_t draw_coverage[512 * 512];
uint32_t frame_draw(uint32_t index) {
  return bw_ui_runtime_draw_item(runtime, index, &draw_item);
}
double frame_draw_number(uint32_t field) {
  switch (field) {
  case 0:
    return draw_item.object;
  case 1:
    return draw_item.parent;
  case 2:
    return draw_item.type;
  case 3:
    return draw_item.visible;
  case 4:
    return draw_item.geometry_status;
  case 5:
    return draw_item.layer;
  case 6:
    return draw_item.enabled;
  case 7:
    return draw_item.stratum;
  case 8:
    return draw_item.level;
  case 9:
    return draw_item.left;
  case 10:
    return draw_item.bottom;
  case 11:
    return draw_item.width;
  case 12:
    return draw_item.height;
  case 13:
    return draw_item.alpha;
  case 14:
    return draw_item.effective_scale;
  case 15:
    return draw_item.font_height;
  case 16:
    return draw_item.shadow_x;
  case 17:
    return draw_item.shadow_y;
  case 18:
    return draw_item.backdrop;
  case 19:
    return draw_item.tile;
  case 20:
    return draw_item.outline;
  case 21:
    return draw_item.edge_size;
  case 22:
    return draw_item.tile_size;
  case 23:
    return draw_item.inset_left;
  case 24:
    return draw_item.inset_right;
  case 25:
    return draw_item.inset_top;
  case 26:
    return draw_item.inset_bottom;
  case 27:
    return draw_item.html_loaded;
  case 28:
    return draw_item.has_clip;
  case 56:
    return draw_item.clip_left;
  case 57:
    return draw_item.clip_bottom;
  case 58:
    return draw_item.clip_width;
  case 59:
    return draw_item.clip_height;
  case 60:
    return draw_item.additive;
  case 61:
    return draw_item.pressed;
  case 62:
    return draw_item.hovered;
  case 63:
    return draw_item.own_alpha;
  case 64:
    return draw_item.mouse_enabled;
  default:
    if (field >= 32 && field < 40)
      return draw_item.uv[field - 32];
    if (field >= 40 && field < 44)
      return draw_item.color[field - 40];
    if (field >= 44 && field < 48)
      return draw_item.backdrop_color[field - 44];
    if (field >= 48 && field < 52)
      return draw_item.border_color[field - 48];
    if (field >= 52 && field < 56)
      return draw_item.shadow_color[field - 52];
    return -1;
  }
}
const char *frame_draw_string(uint32_t field) {
  const char *s = NULL;
  switch (field) {
  case 0:
    s = draw_item.name;
    break;
  case 1:
    s = draw_item.texture;
    break;
  case 2:
    s = draw_item.text;
    break;
  case 3:
    s = draw_item.font;
    break;
  case 4:
    s = draw_item.font_flags;
    break;
  case 5:
    s = draw_item.justify_h;
    break;
  case 6:
    s = draw_item.justify_v;
    break;
  case 7:
    s = draw_item.backdrop_bg;
    break;
  case 8:
    s = draw_item.backdrop_edge;
    break;
  case 9:
    s = draw_item.geometry_error;
    break;
  }
  return s ? s : "";
}
uint32_t frame_glyph(uint32_t index, uint32_t cp) {
  return bw_ui_runtime_draw_glyph(runtime, index, cp, &draw_glyph, draw_coverage,
                                  sizeof(draw_coverage));
}
double frame_glyph_number(uint32_t field) {
  switch (field) {
  case 0:
    return draw_glyph.index;
  case 1:
    return draw_glyph.width;
  case 2:
    return draw_glyph.rows;
  case 3:
    return draw_glyph.bearing_x_26_6;
  case 4:
    return draw_glyph.advance_26_6;
  case 5:
    return draw_glyph.left;
  case 6:
    return draw_glyph.top;
  default:
    return -1;
  }
}
uint8_t *frame_glyph_pixels_ptr(void) {
  return draw_coverage;
}

static BwUiTextQuad text_quad;
uint32_t frame_text_quad(uint32_t index, uint32_t ordinal) {
  return bw_ui_runtime_text_quad(runtime, index, ordinal, &text_quad, draw_coverage,
                                 sizeof(draw_coverage));
}
double frame_text_quad_number(uint32_t field) {
  switch (field) {
  case 0:
    return text_quad.codepoint;
  case 1:
    return text_quad.width;
  case 2:
    return text_quad.height;
  case 3:
    return text_quad.left;
  case 4:
    return text_quad.bottom;
  default:
    if (field >= 5 && field < 9)
      return text_quad.color[field - 5];
    return -1;
  }
}

static uint32_t draw_order[32768], draw_order_count;
uint32_t frame_draw_begin(void) {
  draw_order_count = bw_ui_runtime_draw_order(runtime, draw_order, 32768);
  return draw_order_count;
}
uint32_t frame_draw_index(uint32_t ordinal) {
  return ordinal < draw_order_count ? draw_order[ordinal] : UINT32_MAX;
}

static BwUiImageQuad image_quad;
uint32_t frame_image_quad(uint32_t index, uint32_t part) {
  return bw_ui_runtime_image_quad(runtime, index, part, &image_quad);
}
const char *frame_image_quad_asset(void) {
  return image_quad.asset ? image_quad.asset : "";
}
double frame_image_quad_number(uint32_t field) {
  switch (field) {
  case 0:
    return image_quad.left;
  case 1:
    return image_quad.bottom;
  case 2:
    return image_quad.width;
  case 3:
    return image_quad.height;
  default:
    if (field >= 4 && field < 12)
      return image_quad.uv[field - 4];
    if (field >= 12 && field < 16)
      return image_quad.color[field - 12];
    if (field == 16)
      return image_quad.has_vertex_colors;
    if (field == 33)
      return image_quad.desaturated;
    if (field >= 17 && field < 33)
      return image_quad.vertex_colors[field - 17];
    return -1;
  }
}
