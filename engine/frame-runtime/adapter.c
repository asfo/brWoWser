#include "frames.h"
#include "document.h"
#include "loader.h"
#include "glue_state.h"
#include <stdint.h>
#include <string.h>
#define CAPACITY 1048576
extern lua_State *bw_lua_state(void);
static BwUiRuntime *runtime;
static BwUiLoader *loader;
static BwGlueState *glue;
static char resource_name[512];
static BwXmlDocument *documents[128];
static uint32_t document_count;
static uint8_t input[CAPACITY];
static char last_error[1024];
void frame_close(void) {
  bw_glue_state_free(glue);
  glue = 0;
  bw_ui_loader_free(loader);
  loader = 0;
  bw_ui_runtime_free(runtime);
  runtime = 0;
  for (uint32_t i = 0; i < document_count; i++)
    bw_xml_free(documents[i]);
  document_count = 0;
}
uint32_t frame_init(void) {
  frame_close();
  runtime = bw_ui_runtime_new(bw_lua_state());
  return bw_ui_runtime_register(runtime);
}
uint8_t *frame_input_ptr(void) {
  return input;
}
uint32_t frame_document(uint32_t length) {
  if (!runtime || length > CAPACITY || document_count >= 128)
    return 1;
  BwXmlDocument *d = bw_xml_parse(input, length);
  if (!d)
    return 1;
  documents[document_count++] = d;
  if (d->error)
    return d->error;
  return bw_ui_runtime_document(runtime, d->root);
}
uint32_t frame_count(void) {
  return bw_ui_runtime_count(runtime);
}
uint32_t frame_load(void) {
  return bw_ui_runtime_load(runtime);
}
const char *frame_error(void) {
  (void)last_error;
  return bw_ui_runtime_error(runtime);
}

uint32_t loader_init(void) {
  if (loader)
    return 1;
  loader = bw_ui_loader_new(bw_lua_state(), runtime);
  return loader ? 0 : 1;
}
char *loader_name_ptr(void) {
  return resource_name;
}
uint32_t loader_add(uint32_t length) {
  resource_name[511] = 0;
  return length > CAPACITY ? 1 : bw_ui_loader_add(loader, resource_name, input, length);
}
uint32_t loader_load(void) {
  resource_name[511] = 0;
  return bw_ui_loader_load(loader, resource_name);
}
const char *loader_error(void) {
  return bw_ui_loader_error(loader);
}

uint32_t frame_viewport(double width, double height) {
  return bw_ui_runtime_viewport(runtime, width, height);
}
uint32_t frame_define_event(void) {
  resource_name[511] = 0;
  return bw_ui_runtime_define_event(runtime, resource_name);
}
uint32_t frame_event(uint32_t arguments) {
  resource_name[511] = 0;
  return bw_ui_runtime_event(runtime, resource_name, arguments);
}

uint32_t glue_init(void) {
  if (glue)
    return 1;
  resource_name[511] = 0;
  glue = bw_glue_state_new(bw_lua_state(), resource_name);
  return bw_glue_state_register(glue);
}

uint32_t glue_fresh_profile(void) {
  return bw_glue_state_fresh_profile(glue);
}

static BwUiModelRequest model_request;
uint32_t frame_model_asset(void) {
  resource_name[511] = 0;
  return bw_ui_runtime_model_asset(runtime, resource_name);
}
uint32_t frame_model_next(void) {
  return bw_ui_runtime_model_request(runtime, &model_request);
}
uint32_t frame_model_kind(void) {
  return model_request.operation;
}
uint32_t frame_model_owner(void) {
  return model_request.object;
}
uint32_t frame_model_value(void) {
  return model_request.value;
}
const char *frame_model_path(void) {
  return model_request.path;
}

uint32_t glue_expansion(uint32_t level) {
  return bw_glue_state_expansion(glue, level);
}
uint32_t glue_cvar(uint32_t length) {
  if (length >= CAPACITY)
    return 1;
  resource_name[511] = 0;
  input[length] = 0;
  return bw_glue_state_define_cvar(glue, resource_name, (char *)input, (char *)input);
}

uint32_t frame_font_asset(uint32_t length) {
  resource_name[511] = 0;
  return length > CAPACITY ? 1 : bw_ui_runtime_font_asset(runtime, resource_name, input, length);
}

uint32_t frame_model_role(uint32_t role) {
  return bw_ui_runtime_model_role(runtime, role);
}

uint32_t frame_model_time(void) {
  return model_request.time_ms;
}

float frame_model_scalar(void) {
  return model_request.scalar;
}

uint32_t glue_video(uint32_t width, uint32_t height, uint32_t refresh, uint32_t color,
                    uint32_t depth, uint32_t samples) {
  return bw_glue_state_video(glue, width, height, refresh, color, depth, samples);
}

uint32_t loader_seal_verified(void) {
  return bw_ui_loader_seal_verified(loader);
}

uint32_t glue_video_caps(uint32_t flags, uint32_t buffering, uint32_t maximum) {
  return bw_glue_state_video_caps(glue, flags, buffering, maximum);
}

uint32_t frame_protected_failures(void) {
  return bw_ui_runtime_protected_failures(runtime);
}
const char *frame_protected_error(void) {
  return bw_ui_runtime_protected_error(runtime);
}

uint32_t glue_processors(uint32_t count) {
  return bw_glue_state_processors(glue, count);
}
uint32_t glue_audio_output(uint32_t length) {
  if (length >= CAPACITY)
    return 1;
  input[length] = 0;
  return bw_glue_state_audio_output(glue, (char *)input);
}

uint32_t frame_texture_asset(uint32_t width, uint32_t height) {
  resource_name[511] = 0;
  return bw_ui_runtime_texture_asset(runtime, resource_name, width, height);
}

static BwGlueAudioRequest audio_request;
uint32_t glue_audio_asset(uint32_t channel, uint32_t id) {
  resource_name[511] = 0;
  return bw_glue_audio_asset(bw_glue_state_audio(glue), channel, resource_name, id);
}
uint32_t glue_audio_next(void) {
  return bw_glue_audio_next(bw_glue_state_audio(glue), &audio_request);
}
uint32_t glue_audio_token(void) {
  return audio_request.token;
}
uint32_t glue_audio_operation(void) {
  return audio_request.operation;
}
uint32_t glue_audio_channel(void) {
  return audio_request.channel;
}
uint32_t glue_audio_id(void) {
  return audio_request.asset;
}
float glue_audio_fade(void) {
  return audio_request.fade_seconds;
}
uint32_t glue_audio_ack(uint32_t token, uint32_t failure) {
  return bw_glue_audio_ack(bw_glue_state_audio(glue), token, failure);
}
uint32_t glue_audio_pending(void) {
  return bw_glue_audio_pending(bw_glue_state_audio(glue));
}
uint32_t glue_audio_failures(void) {
  return bw_glue_audio_failures(bw_glue_state_audio(glue));
}
uint32_t glue_audio_completed(void) {
  return bw_glue_audio_completed(bw_glue_state_audio(glue));
}

uint32_t glue_agreements(uint32_t accepted, uint32_t notices) {
  return bw_glue_state_agreements(glue, accepted, notices);
}

uint32_t glue_offline_realm(void) {
  resource_name[511] = 0;
  return bw_glue_state_offline_realm(glue, resource_name);
}

#include "draw_adapter.inc"
#ifdef BROWSER_SCREEN_BRIDGE
__attribute__((import_module("platform"),
               import_name("screen_wait"))) extern uint32_t host_screen_wait(void);
static uint32_t wait_screen(void *context) {
  (void)context;
  return host_screen_wait();
}
uint32_t glue_screen_backend(void) {
  return bw_glue_state_screen_backend(glue, wait_screen, 0);
}
#else
uint32_t glue_screen_backend(void) {
  return 1;
}
#endif
uint32_t glue_screen_state(void) {
  return bw_glue_state_screen(glue);
}

uint32_t glue_neutral_gamma(void) {
  return bw_glue_state_neutral_gamma(glue);
}

uint32_t glue_renderer_profile(void) {
  return bw_glue_state_renderer_profile(glue);
}

static uint32_t config_metrics[4];
uint32_t glue_config_load(uint32_t length) {
  static BwWtfConfig document;
  memset(config_metrics, 0, sizeof(config_metrics));
  if (length > CAPACITY)
    return 1;
  uint32_t result = bw_wtf_parse(&document, input, length);
  config_metrics[0] = document.count;
  config_metrics[1] = document.error_line;
  if (!result)
    result = bw_glue_state_apply_config(glue, &document, &config_metrics[2], &config_metrics[3]);
  memset(&document, 0, sizeof(document));
  memset(input, 0, length);
  return result;
}
uint32_t glue_config_metric(uint32_t field) {
  return field < 4 ? config_metrics[field] : 0;
}

static BwMovieRequest movie_request;
uint32_t frame_movie_register(void) {
  return bw_movie_register(bw_ui_runtime_movie(runtime), resource_name) == 1 ? 0 : 1;
}
uint32_t frame_movie_next(void) {
  return bw_movie_take(bw_ui_runtime_movie(runtime), &movie_request) ? 0 : 1;
}
uint32_t frame_movie_token(void) {
  return movie_request.token;
}
uint32_t frame_movie_operation(void) {
  return movie_request.operation;
}
uint32_t frame_movie_object(void) {
  return movie_request.object;
}
double frame_movie_volume(void) {
  return movie_request.volume;
}
const char *frame_movie_path(void) {
  return movie_request.path;
}
uint32_t frame_movie_complete(uint32_t token) {
  return bw_ui_runtime_movie_complete(runtime, token);
}
uint32_t frame_cursor_hidden(void) {
  return bw_ui_runtime_cursor_hidden(runtime);
}
uint32_t glue_movie_profile(void) {
  return bw_glue_state_movie_profile(glue);
}

uint32_t glue_startup_profile(void) {
  return bw_glue_state_startup_profile(glue);
}
uint32_t glue_startup_screen(uint32_t flag) {
  uint32_t screen = 0;
  return bw_glue_state_startup_screen(glue, flag, &screen) ? 0 : screen;
}
static uint32_t config_length;
uint32_t glue_config_save(void) {
  return bw_glue_state_write_config(glue, (char *)input, CAPACITY, &config_length);
}
uint32_t glue_config_length(void) {
  return config_length;
}

uint32_t frame_html_asset(uint32_t length) {
  resource_name[511] = 0;
  return length > CAPACITY ? 1 : bw_ui_runtime_html_asset(runtime, resource_name, input, length);
}

uint32_t frame_update_scroll(void) {
  return bw_ui_runtime_update_scroll(runtime);
}

uint32_t frame_input_action(uint32_t kind, double x, double y, int32_t delta) {
  return bw_ui_runtime_input_at(runtime, kind, x, y, delta);
}
uint32_t frame_input_target(void) {
  return bw_ui_runtime_input_target(runtime);
}

uint32_t glue_agreement_profile(uint32_t index, int32_t selector) {
  return bw_glue_state_agreement_profile(glue, index, selector);
}

uint32_t glue_scan_profile(void) {
  return bw_glue_state_scan_profile(glue);
}
uint32_t glue_scan_pending(void) {
  return bw_glue_state_scan_pending(glue);
}
uint32_t glue_scan_unavailable(void) {
  return bw_glue_state_scan_unavailable(glue);
}

uint32_t glue_system_profile(uint32_t flags) {
  return bw_glue_state_system_profile(glue, flags);
}

uint32_t frame_edit_action(uint32_t op, uint32_t length) {
  return bw_ui_runtime_edit_action(runtime, op, (const char *)frame_input_ptr(), length);
}
uint32_t frame_edit_pump(void) {
  return bw_ui_runtime_edit_pump(runtime);
}
uint32_t frame_edit_focus(void) {
  return bw_ui_runtime_edit_focus(runtime);
}

uint32_t glue_warning_profile(void) {
  return bw_glue_state_warning_profile(glue);
}

uint32_t glue_options_readonly_profile(void) {
  if (bw_glue_state_options_readonly(glue))
    return 1;
  return bw_ui_runtime_options_readonly(runtime);
}

uint32_t glue_auth_enable(void) {
  return bw_glue_state_auth_enable(glue);
}
uint32_t glue_auth_request(void) {
  return bw_glue_state_auth_request(glue);
}
uint32_t glue_auth_finish(uint32_t generation) {
  return bw_glue_state_auth_finish(glue, generation);
}

uint32_t glue_auth_credentials(void) {
  return bw_glue_state_auth_credentials(glue, input, CAPACITY);
}

uint32_t glue_realm_category(uint32_t id, uint32_t flags) {
  resource_name[511] = 0;
  return bw_glue_state_realm_category(glue, id, flags, resource_name);
}
uint32_t glue_realm_begin(void) {
  return bw_glue_state_realm_begin(glue);
}
uint32_t glue_realm_add(uint32_t length) {
  if (length < 46 || length > CAPACITY)
    return 1;
  BwRealm row = {0};
  uint32_t *fields[] = {&row.id,       &row.type,  &row.flags, &row.locked,   &row.characters,
                        &row.timezone, &row.major, &row.minor, &row.revision, &row.build};
  for (uint32_t i = 0; i < 10; i++)
    memcpy(fields[i], input + i * 4, 4);
  memcpy(&row.population, input + 40, 4);
  const uint8_t *name = input + 44, *end = memchr(name, 0, length - 44);
  if (!end || end - name >= 256)
    return 1;
  memcpy(row.name, name, (uint32_t)(end - name));
  const uint8_t *address = end + 1;
  end = memchr(address, 0, (input + length) - address);
  if (!end || end - address >= 256 || end != input + length - 1)
    return 1;
  memcpy(row.address, address, (uint32_t)(end - address));
  return bw_glue_state_realm_add(glue, &row);
}
uint32_t glue_realm_finalize(void) {
  return bw_glue_state_realm_finalize(glue);
}
uint32_t glue_realm_action(void) {
  return bw_glue_state_realm_action(glue);
}

uint32_t glue_world_begin(void) {
  return bw_glue_state_world_begin(glue);
}
uint32_t glue_world_busy(void) {
  return bw_glue_state_world_busy(glue);
}
void glue_world_finish(void) {
  bw_glue_state_world_finish(glue);
}

uint32_t glue_characters_received(uint32_t count) {
  return bw_glue_state_characters_received(glue, count);
}
uint32_t glue_character_import(uint32_t kind, uint32_t length) {
  return length > CAPACITY
             ? 1
             : bw_characters_import(bw_glue_state_characters(glue), kind, input, length);
}
uint32_t glue_character_roster_begin(void) {
  return bw_characters_roster_begin(bw_glue_state_characters(glue));
}
uint32_t glue_character_connected(uint32_t value) {
  return bw_glue_state_character_connected(glue, value);
}
uint32_t glue_character_metric(uint32_t field) {
  return bw_characters_metric(bw_glue_state_characters(glue), field);
}
uint32_t glue_character_actions(void) {
  return bw_characters_actions(bw_glue_state_characters(glue));
}
uint32_t glue_character_request(void) {
  return bw_characters_request(bw_glue_state_characters(glue), input, CAPACITY);
}
void glue_character_finish(void) {
  bw_characters_finish(bw_glue_state_characters(glue));
}
double glue_character_facing(uint32_t creation) {
  return bw_characters_facing(bw_glue_state_characters(glue), creation);
}
double frame_model_parameter(uint32_t role, uint32_t field, uint32_t index) {
  return bw_ui_runtime_model_parameter(runtime, role, field, index);
}
