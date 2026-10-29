#include "movie.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define RESOURCES 16
#define REQUESTS 128
struct BwMovie {
  char paths[RESOURCES][BW_MOVIE_PATH];
  uint32_t resources, next, active, object, head, count;
  BwMovieRequest requests[REQUESTS];
};
BwMovie *bw_movie_create(void) {
  return calloc(1, sizeof(BwMovie));
}
void bw_movie_destroy(BwMovie *m) {
  free(m);
}
static int valid_path(const char *p) {
  size_t n = p ? strlen(p) : 0;
  if (!n || n >= BW_MOVIE_PATH || strstr(p, "..") || strchr(p, ':') || p[0] == '/' || p[0] == '\\')
    return 0;
  return 1;
}
int bw_movie_register(BwMovie *m, const char *p) {
  if (!m || !valid_path(p))
    return -1;
  for (uint32_t i = 0; i < m->resources; i++)
    if (!strcmp(m->paths[i], p))
      return 1;
  if (m->resources == RESOURCES)
    return -1;
  strcpy(m->paths[m->resources++], p);
  return 1;
}
static BwMovieRequest *enqueue(BwMovie *m, uint32_t operation, uint32_t object) {
  if (m->count == REQUESTS || m->next == UINT32_MAX)
    return NULL;
  BwMovieRequest *r = &m->requests[(m->head + m->count) % REQUESTS];
  memset(r, 0, sizeof(*r));
  r->token = ++m->next;
  r->operation = operation;
  r->object = object;
  m->count++;
  return r;
}
int bw_movie_start(BwMovie *m, uint32_t object, const char *p, double volume) {
  if (!m || !object || !valid_path(p) || !isfinite(volume))
    return -1;
  uint32_t i;
  for (i = 0; i < m->resources; i++)
    if (!strcmp(m->paths[i], p))
      break;
  if (i == m->resources)
    return 0;
  BwMovieRequest *r = enqueue(m, 1, object);
  if (!r)
    return -1;
  strcpy(r->path, p);
  r->volume = volume;
  m->active = r->token;
  m->object = object;
  return 1;
}
int bw_movie_stop(BwMovie *m, uint32_t object) {
  if (!m || !object)
    return -1;
  if (!m->active || m->object != object)
    return 0;
  BwMovieRequest *r = enqueue(m, 2, object);
  if (!r)
    return -1;
  m->active = m->object = 0;
  return 1;
}
int bw_movie_subtitles(BwMovie *m, uint32_t object, int enabled) {
  if (!m || !object)
    return -1;
  BwMovieRequest *r = enqueue(m, 3, object);
  if (!r)
    return -1;
  r->volume = enabled ? 1 : 0;
  return 1;
}
uint32_t bw_movie_pending(const BwMovie *m) {
  return m ? m->count : 0;
}
int bw_movie_take(BwMovie *m, BwMovieRequest *r) {
  if (!m || !r || !m->count)
    return 0;
  *r = m->requests[m->head];
  m->head = (m->head + 1) % REQUESTS;
  m->count--;
  return 1;
}
int bw_movie_complete(BwMovie *m, uint32_t token, uint32_t *object) {
  if (!m || !token || token != m->active)
    return 0;
  if (object)
    *object = m->object;
  m->active = m->object = 0;
  return 1;
}
