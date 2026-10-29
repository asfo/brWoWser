/* Portable model of the observed tree ordering and first-nonzero traversal.
 * Caller supplies candidate result codes; this is not an MPQ lookup engine. */
typedef unsigned int U;
static U rows[64][3], order[64];
U *rows_ptr(void) {
  return &rows[0][0];
}
U *order_ptr(void) {
  return order;
}
static int ahead(U a, U b) {
  int ap = (int)rows[a][0], bp = (int)rows[b][0];
  if (ap != bp)
    return ap > bp;
  return (int)rows[a][1] > (int)rows[b][1];
}
int rank_candidates(U n) {
  if (n > 64)
    return 0;
  for (U i = 0; i < n; i++) {
    U at = i;
    while (at && ahead(i, order[at - 1])) {
      order[at] = order[at - 1];
      at--;
    }
    order[at] = i;
  }
  return 1;
}
int first_nonzero(U n) {
  if (!rank_candidates(n))
    return -2;
  for (U i = 0; i < n; i++)
    if (rows[order[i]][2])
      return (int)order[i];
  return -1;
}
