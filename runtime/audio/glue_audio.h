#ifndef BRWOWSER_GLUE_AUDIO_H
#define BRWOWSER_GLUE_AUDIO_H
#include <stdint.h>
#include "lua.h"
typedef struct BwGlueAudio BwGlueAudio;
typedef struct BwGlueAudioRequest {
  uint32_t token, operation, channel, asset;
  float fade_seconds;
} BwGlueAudioRequest;
enum {
  BW_GLUE_AUDIO_MUSIC = 0,
  BW_GLUE_AUDIO_AMBIENCE = 1,
  BW_GLUE_AUDIO_EFFECT = 2,
  BW_GLUE_AUDIO_PLAY = 1,
  BW_GLUE_AUDIO_STOP = 2
};
BwGlueAudio *bw_glue_audio_new(lua_State *L);
void bw_glue_audio_free(BwGlueAudio *audio);
uint32_t bw_glue_audio_register(BwGlueAudio *audio);
/* Trusted host admits only kits whose owner asset/DBC identities it verified. */
uint32_t bw_glue_audio_asset(BwGlueAudio *audio, uint32_t channel, const char *kit, uint32_t id);
uint32_t bw_glue_audio_next(BwGlueAudio *audio, BwGlueAudioRequest *request);
/* FIFO acknowledgements: 0 backend success, 1 explicit backend failure.
 * Enqueuing is never evidence of actual audio output. */
uint32_t bw_glue_audio_ack(BwGlueAudio *audio, uint32_t token, uint32_t failure);
uint32_t bw_glue_audio_pending(const BwGlueAudio *audio);
uint32_t bw_glue_audio_failures(const BwGlueAudio *audio);
uint32_t bw_glue_audio_completed(const BwGlueAudio *audio);
#endif
