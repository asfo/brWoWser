#include "anchors.h"
#include <math.h>
#include <string.h>
static int valid_rect(BwAnchorRect r) {
  return isfinite(r.left) && isfinite(r.bottom) && isfinite(r.width) && isfinite(r.height) &&
         r.width >= 0 && r.height >= 0;
}
static double fraction(uint32_t p, int axis) {
  return axis ? (double)(p / 3) * 0.5 : (double)(p % 3) * 0.5;
}
static int solve_axis(const BwAnchor *a, uint32_t n, int axis, int known, double size, double tol,
                      double *origin, double *extent) {
  double f0 = fraction(a[0].point, axis),
         t0 = (axis ? a[0].relative.bottom : a[0].relative.left) +
              fraction(a[0].relative_point, axis) *
                  (axis ? a[0].relative.height : a[0].relative.width) +
              (axis ? a[0].y : a[0].x);
  {
    int found = 0;
    for (uint32_t i = 1; i < n; i++) {
      double f = fraction(a[i].point, axis);
      if (f != f0) {
        double t = (axis ? a[i].relative.bottom : a[i].relative.left) +
                   fraction(a[i].relative_point, axis) *
                       (axis ? a[i].relative.height : a[i].relative.width) +
                   (axis ? a[i].y : a[i].x);
        size = (t - t0) / (f - f0);
        found = 1;
        break;
      }
    }
    if (!found && !known)
      return BW_ANCHOR_UNDERDETERMINED;
  }
  if (!isfinite(size) || size < 0)
    return BW_ANCHOR_CONFLICT;
  double o = t0 - f0 * size;
  if (!isfinite(o))
    return BW_ANCHOR_INVALID;
  for (uint32_t i = 0; i < n; i++) {
    double target =
        (axis ? a[i].relative.bottom : a[i].relative.left) +
        fraction(a[i].relative_point, axis) * (axis ? a[i].relative.height : a[i].relative.width) +
        (axis ? a[i].y : a[i].x);
    if (!isfinite(target) || fabs(o + fraction(a[i].point, axis) * size - target) > tol)
      return BW_ANCHOR_CONFLICT;
  }
  *origin = o;
  *extent = size;
  return BW_ANCHOR_OK;
}
int bw_anchor_solve(const BwAnchor *a, uint32_t n, int wk, double w, int hk, double h, double tol,
                    BwAnchorRect *out) {
  if (!out)
    return BW_ANCHOR_INVALID;
  memset(out, 0, sizeof(*out));
  if (!a || !n || n > 32 || !isfinite(tol) || tol < 0 || (wk != 0 && wk != 1) ||
      (hk != 0 && hk != 1) || (wk && (!isfinite(w) || w < 0)) || (hk && (!isfinite(h) || h < 0)))
    return BW_ANCHOR_INVALID;
  for (uint32_t i = 0; i < n; i++)
    if (a[i].point > 8 || a[i].relative_point > 8 || !valid_rect(a[i].relative) ||
        !isfinite(a[i].x) || !isfinite(a[i].y))
      return BW_ANCHOR_INVALID;
  BwAnchorRect r;
  int status = solve_axis(a, n, 0, wk, w, tol, &r.left, &r.width);
  if (status)
    return status;
  status = solve_axis(a, n, 1, hk, h, tol, &r.bottom, &r.height);
  if (status)
    return status;
  *out = r;
  return 0;
}
