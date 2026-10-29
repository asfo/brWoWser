#ifndef BRWOWSER_GLUE_STATE_H
#define BRWOWSER_GLUE_STATE_H
#include "lua.h"
#include "glue_audio.h"
#include "realm_list.h"
#include "characters.h"
#include <stdint.h>
#include "../config/wtf.h"
typedef struct BwGlueState BwGlueState;
/* Selected INITIAL -> login boundary. Zero means the configured host barrier
 * completed successfully. A callback must not longjmp or destroy this state. */
typedef uint32_t (*BwGlueScreenBarrier)(void *context);
uint32_t bw_glue_state_screen_backend(BwGlueState *state, BwGlueScreenBarrier barrier,
                                      void *context);
/* Owned screens: 0 initial, 1 login, 2 movie, 3 credits, 4 character selection,
 * 5 character creation, 6 realm wizard. Not native lifecycle equivalence. */
uint32_t bw_glue_state_screen(BwGlueState *state);
/* Explicit browser-owned profile, not a claim about native user defaults. */
BwGlueState *bw_glue_state_new(lua_State *L, const char *locale);
uint32_t bw_glue_state_expansion(BwGlueState *state, uint32_t level);
/* Host-owned display profile. No Win32 hardware enumeration is fabricated.
 * Selected browser boundary currently admits one logical canvas mode. */
uint32_t bw_glue_state_video(BwGlueState *state, uint32_t width, uint32_t height, uint32_t refresh,
                             uint32_t color_bits, uint32_t depth_bits, uint32_t samples);
/* Explicit compatibility-backend capability profile: flag bits anisotropic,
 * pixel shader, vertex shader, trilinear, hardware cursor, stereo. Not hardware sniffing. */
uint32_t bw_glue_state_video_caps(BwGlueState *state, uint32_t flags, uint32_t buffering,
                                  uint32_t maximum_anisotropy);
uint32_t bw_glue_state_processors(BwGlueState *state, uint32_t count);
uint32_t bw_glue_state_audio_output(BwGlueState *state, const char *default_output);
BwGlueAudio *bw_glue_state_audio(BwGlueState *state);
/* Host supplies verified owned profile state; no acceptance inferred from login.
 * Bit order TOS, EULA, termination, scanning, contest; one initial snapshot. */
uint32_t bw_glue_state_agreements(BwGlueState *state, uint32_t accepted, uint32_t notices);
/* Explicit no-selected-live-realm branch, not a connection result. */
uint32_t bw_glue_state_offline_realm(BwGlueState *state, const char *name);
/* Explicit browser passthrough gamma profile. Only unchanged SetGamma(0), within 1e-7 slider quantization,
 * admitted; no native defaults or monitor-ramp mutation claimed. */
uint32_t bw_glue_state_movie_profile(BwGlueState *state);
uint32_t bw_glue_state_renderer_profile(BwGlueState *state);
uint32_t bw_glue_state_neutral_gamma(BwGlueState *state);
void bw_glue_state_free(BwGlueState *state);
uint32_t bw_glue_state_register(BwGlueState *state);
/* Explicit empty browser profile for selected offline tests; acceptance/scan
 * flags are deliberately not supplied by this operation. */
uint32_t bw_glue_state_fresh_profile(BwGlueState *state);
uint32_t bw_glue_state_define_cvar(BwGlueState *state, const char *name, const char *default_value,
                                   const char *value);
/* Import already parsed assignments into registered CVars only, before Glue
 * initialization. Unknown names stay in the caller-owned document. Duplicate
 * known names use the final assignment; registered defaults remain unchanged.
 * This does not set account, consent, locale, display or realm host profiles. */
uint32_t bw_glue_state_apply_config(BwGlueState *state, const BwWtfConfig *config,
                                    uint32_t *applied, uint32_t *unknown);
/* Canonical selected SET serialization; length excludes terminating NUL.
 * NULL/zero queries required length. On any failure no output bytes change.
 * Only registered current CVars are emitted; caller chooses local persistence. */
uint32_t bw_glue_state_write_config(BwGlueState *state, char *output, uint32_t capacity,
                                    uint32_t *length);
/* Native defaults at 0x401c1c/0x401c41. Configure before importing Config.wtf. */
uint32_t bw_glue_state_startup_profile(BwGlueState *state);
/* Selected native startup branch 0x404225..0x404295, explicit unknown native
 * byte 0xb2f9e1 supplied explicitly (0..255, equality to 1). Decides 1 login / 2 movie and consumes flags;
 * does not invoke SetCurrentScreen or imply cinematic decoding/persistence.
 * Selected portable policy rejects non-integral/overflowing flag strings. */
uint32_t bw_glue_state_startup_screen(BwGlueState *state, uint32_t expansion_flag,
                                      uint32_t *screen);
/* Explicit modeled native config+0x30 selector; no inferred imported consent. */
uint32_t bw_glue_state_agreement_profile(BwGlueState *, uint32_t, int32_t);
uint32_t bw_glue_state_scan_profile(BwGlueState *);
uint32_t bw_glue_state_scan_pending(BwGlueState *);
uint32_t bw_glue_state_scan_unavailable(BwGlueState *);
uint32_t bw_glue_state_system_profile(BwGlueState *, uint32_t);
uint32_t bw_glue_state_warning_profile(BwGlueState *);
uint32_t bw_glue_state_options_readonly(BwGlueState *);
uint32_t bw_glue_state_auth_enable(BwGlueState *);
uint32_t bw_glue_state_auth_request(BwGlueState *);
uint32_t bw_glue_state_auth_finish(BwGlueState *, uint32_t generation);
uint32_t bw_glue_state_auth_credentials(BwGlueState *, uint8_t *, uint32_t capacity);
uint32_t bw_glue_state_realm_category(BwGlueState *, uint32_t id, uint32_t flags,
                                      const char *label);
uint32_t bw_glue_state_realm_begin(BwGlueState *);
uint32_t bw_glue_state_realm_add(BwGlueState *, const BwRealm *);
uint32_t bw_glue_state_realm_finalize(BwGlueState *);
uint32_t bw_glue_state_realm_action(BwGlueState *);
uint32_t bw_glue_state_world_begin(BwGlueState *);
uint32_t bw_glue_state_world_busy(BwGlueState *);
void bw_glue_state_world_finish(BwGlueState *);
uint32_t bw_glue_state_characters_received(BwGlueState *, uint32_t count);
BwCharacters *bw_glue_state_characters(BwGlueState *);
uint32_t bw_glue_state_character_connected(BwGlueState *, uint32_t connected);
#endif
