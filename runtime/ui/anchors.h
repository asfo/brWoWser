#ifndef BW_UI_ANCHORS_H
#define BW_UI_ANCHORS_H
#include <stdint.h>
/* Coordinates use bottom-left origin and one explicit common scale. */
typedef struct {
  double left, bottom, width, height;
} BwAnchorRect;
typedef struct {
  uint32_t point, relative_point;
  BwAnchorRect relative;
  double x, y;
} BwAnchor;
/* Nine points: BOTTOMLEFT,BOTTOM,BOTTOMRIGHT,LEFT,CENTER,RIGHT,TOPLEFT,TOP,TOPRIGHT. */
enum {
  BW_ANCHOR_BOTTOMLEFT = 0,
  BW_ANCHOR_BOTTOM = 1,
  BW_ANCHOR_BOTTOMRIGHT = 2,
  BW_ANCHOR_LEFT = 3,
  BW_ANCHOR_CENTER = 4,
  BW_ANCHOR_RIGHT = 5,
  BW_ANCHOR_TOPLEFT = 6,
  BW_ANCHOR_TOP = 7,
  BW_ANCHOR_TOPRIGHT = 8
};
enum {
  BW_ANCHOR_OK = 0,
  BW_ANCHOR_INVALID = 1,
  BW_ANCHOR_UNDERDETERMINED = 2,
  BW_ANCHOR_CONFLICT = 3
};
/* Independent anchor equations determine rendered extent on each axis, even
 * when a nominal width/height is supplied. A single anchor fraction uses the
 * nominal extent; absent both, the axis is underdetermined. Native selected
 * opposed-anchor runtime cases establish this distinction from nominal getters.
 * Output is zero on failure.
 * No dependency resolution: callers must provide resolved reference rectangles.
 * This deliberately rejects conflicting native layouts rather than inventing precedence. */
int bw_anchor_solve(const BwAnchor *anchors, uint32_t count, int width_known, double width,
                    int height_known, double height, double tolerance, BwAnchorRect *out);
#endif
