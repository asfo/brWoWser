#include "wait_all.h"
__attribute__((import_module("provider"), import_name("load"))) extern uint32_t
load(uint32_t index);
static uint32_t jobs[128], count, completed, remaining_bytes, total, failed, progress_count;
static float progress_values[128];
uint32_t add_job(uint32_t bytes) {
  if (count >= 128 || !bytes || bytes > 16777216)
    return 1;
  jobs[count++] = bytes;
  return 0;
}
static uint32_t weight(void *ctx, uint32_t index) {
  (void)ctx;
  return jobs[index];
}
static int pending(void *ctx) {
  (void)ctx;
  return !failed && completed < count;
}
static void pump(void *ctx) {
  (void)ctx;
  uint32_t bytes = load(completed);
  if (bytes != jobs[completed]) {
    failed = 1;
    return;
  }
  remaining_bytes -= bytes;
  completed++;
}
static uint32_t remaining(void *ctx) {
  (void)ctx;
  return remaining_bytes;
}
static void progress(void *ctx, float value) {
  (void)ctx;
  if (progress_count < 128)
    progress_values[progress_count++] = value;
}
static void yield(void *ctx, uint32_t ticks) {
  (void)ctx;
  (void)ticks;
}
uint32_t run(void) {
  if (!count)
    return 1;
  completed = failed = progress_count = 0;
  total = 0;
  for (uint32_t i = 0; i < count; i++)
    total += jobs[i];
  remaining_bytes = total;
  BwWaitAll state = {0, count, weight, pending, pump, remaining, progress, yield, 0};
  uint32_t status = bw_wait_all(&state, 128);
  return status ? status : (failed ? 3 : 0);
}
uint32_t done(void) {
  return completed;
}
uint32_t remaining_bytes_value(void) {
  return remaining_bytes;
}
uint32_t progress_size(void) {
  return progress_count;
}
float progress_value(uint32_t index) {
  return index < progress_count ? progress_values[index] : -1;
}
