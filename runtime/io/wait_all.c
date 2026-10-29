#include "wait_all.h"
uint32_t bw_wait_all(BwWaitAll *s, uint32_t limit) {
  if (!s || !limit || !s->pending || !s->pump || !s->yield || (s->callback_count && !s->weight) ||
      (s->progress && !s->remaining))
    return 1;
  if (s->log)
    s->log(s->context, 1);
  uint32_t total = 0, iterations = 0;
  for (uint32_t i = 0; i < s->callback_count; i++)
    total += s->weight(s->context, i);
  while (s->pending(s->context)) {
    if (iterations++ == limit)
      return 2;
    s->pump(s->context);
    if (s->progress) {
      float value = total ? (float)(int32_t)(total - s->remaining(s->context)) / (float)total : 1;
      if (value < 0)
        value = 0;
      if (value > 1)
        value = 1;
      s->progress(s->context, value);
    }
    s->yield(s->context, 1);
  }
  s->progress = 0;
  if (s->log)
    s->log(s->context, 0);
  return 0;
}
