#ifndef BRWOWSER_WAIT_ALL_H
#define BRWOWSER_WAIT_ALL_H
#include <stdint.h>
/* Portable service interface, not an original structure layout. The native
 * lists/locks and provider lifetime are separate contracts. */
typedef struct BwWaitAll {
  void *context;
  uint32_t callback_count;
  uint32_t (*weight)(void *, uint32_t);
  int (*pending)(void *);
  void (*pump)(void *);
  uint32_t (*remaining)(void *);
  void (*progress)(void *, float);
  void (*yield)(void *, uint32_t);
  void (*log)(void *, int);
} BwWaitAll;
/* max_iterations is an explicit portable resource bound, unlike the original
 * unbounded loop. 0 success, 1 invalid interface, 2 bound reached. */
uint32_t bw_wait_all(BwWaitAll *, uint32_t max_iterations);
#endif
