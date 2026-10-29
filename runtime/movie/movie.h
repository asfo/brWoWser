#ifndef BRWOWSER_MOVIE_H
#define BRWOWSER_MOVIE_H
#include <stdint.h>
#define BW_MOVIE_PATH 256
typedef struct BwMovie BwMovie;
typedef struct {
  uint32_t token, operation, object;
  double volume;
  char path[BW_MOVIE_PATH];
} BwMovieRequest;
BwMovie *bw_movie_create(void);
void bw_movie_destroy(BwMovie *movie);
/* Register already prepared owner-local resources; no filename-derived success. */
int bw_movie_register(BwMovie *movie, const char *path);
/* 1 admitted, 0 unavailable, -1 invalid/capacity. Native Lua result mapping is 1/nil. */
int bw_movie_start(BwMovie *movie, uint32_t object, const char *path, double volume);
int bw_movie_stop(BwMovie *movie, uint32_t object);
int bw_movie_subtitles(BwMovie *movie, uint32_t object, int enabled);
uint32_t bw_movie_pending(const BwMovie *movie);
int bw_movie_take(BwMovie *movie, BwMovieRequest *request);
/* Host completion returns 1 once for active token; stale callbacks return 0. */
int bw_movie_complete(BwMovie *movie, uint32_t token, uint32_t *object);
#endif
