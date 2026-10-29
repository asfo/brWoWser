#include "loader.h"
#include "script_vm.h"
#include "lauxlib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define SOURCES 128
#define NAME_SIZE 512
#define MAX_BYTES (16 * 1024 * 1024)
typedef struct Source {
  char name[NAME_SIZE];
  uint8_t *bytes;
  uint32_t length, active;
  BwXmlDocument *document;
} Source;
struct BwUiLoader {
  lua_State *L;
  BwUiRuntime *runtime;
  Source sources[SOURCES];
  uint32_t count, bytes, depth, sealed;
  const char *path;
  char error[2048];
};
static int fail(BwUiLoader *l, const char *name, const char *error) {
  snprintf(l->error, sizeof(l->error), "%s: %.1400s", name ? name : "resource",
           error ? error : "load failed");
  return 1;
}
static const char *attribute(const BwXmlNode *n, const char *s) {
  for (BwXmlAttribute *a = n->attributes; a; a = a->next)
    if (!strcmp(a->name, s))
      return a->value;
  return NULL;
}
static int normalize(const char *name, const char *base, char *out) {
  char input[NAME_SIZE];
  if (!name || !*name)
    return 1;
  size_t n = strlen(name), prefix = 0;
  if (n >= NAME_SIZE)
    return 1;
  /* Full archive paths begin Interface; all other includes are relative. */
  int full = n >= 10;
  const char *root = "interface";
  for (int i = 0; i < 9 && full; i++) {
    char c = name[i];
    if (c >= 'A' && c <= 'Z')
      c += 32;
    if (c != root[i])
      full = 0;
  }
  if (full && name[9] != '/' && name[9] != '\\')
    full = 0;
  if (base && !full) {
    const char *slash = strrchr(base, '/');
    if (slash)
      prefix = (size_t)(slash - base) + 1;
  }
  if (prefix + n >= sizeof(input))
    return 1;
  if (prefix)
    memcpy(input, base, prefix);
  memcpy(input + prefix, name, n + 1);
  uint32_t starts[128], parts = 0;
  size_t used = 0;
  char *p = input;
  while (*p) {
    while (*p == '/' || *p == '\\')
      p++;
    char *end = p;
    while (*end && *end != '/' && *end != '\\')
      end++;
    size_t length = (size_t)(end - p);
    if (!length)
      break;
    if (length == 1 && p[0] == '.') {
    } else if (length == 2 && p[0] == '.' && p[1] == '.') {
      if (!parts)
        return 1;
      used = starts[--parts];
    } else {
      if (parts >= 128 || used + length + 1 >= NAME_SIZE)
        return 1;
      starts[parts++] = (uint32_t)used;
      if (used)
        out[used++] = '/';
      for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 32 || c == 127)
          return 1;
        if (c >= 'A' && c <= 'Z')
          c += 32;
        out[used++] = (char)c;
      }
    }
    p = end;
  }
  out[used] = 0;
  return !used;
}
static uint32_t execute(BwUiLoader *l, const uint8_t *bytes, uint32_t length, const char *name) {
  int top = lua_gettop(l->L);
  int status = luaL_loadbuffer(l->L, (const char *)bytes, length, name);
  if (!status)
    status = lua_pcall(l->L, 0, 0, 0);
  if (status)
    fail(l, name, lua_tostring(l->L, -1));
  lua_settop(l->L, top);
  return status ? 1 : 0;
}
static uint32_t dispatch(void *context, const BwXmlNode *n) {
  BwUiLoader *l = context;
  const char *file = attribute(n, "file");
  if (file)
    return bw_ui_loader_load(l, file);
  const char *local = strrchr(n->name, ':');
  local = local ? local + 1 : n->name;
  if (!strcmp(local, "Include"))
    return fail(l, l->path, "include missing file");
  const char *body = n->text ? n->text : "";
  return execute(l, (const uint8_t *)body, (uint32_t)strlen(body), l->path);
}
BwUiLoader *bw_ui_loader_new(lua_State *L, BwUiRuntime *r) {
  if (!L || !r)
    return NULL;
  BwUiLoader *l = calloc(1, sizeof(*l));
  if (l) {
    l->L = L;
    l->runtime = r;
    bw_ui_runtime_dispatch(r, dispatch, l);
    bw_ui_runtime_auto_load(r, 1);
  }
  return l;
}
uint32_t bw_ui_loader_add(BwUiLoader *l, const char *name, const uint8_t *bytes, uint32_t length) {
  if (!l || l->sealed || !bytes || length > 1048576 || l->count >= SOURCES ||
      l->bytes + length > MAX_BYTES)
    return 1;
  char normalized[NAME_SIZE];
  if (normalize(name, NULL, normalized))
    return fail(l, name, "invalid resource name");
  for (uint32_t i = 0; i < l->count; i++)
    if (!strcmp(normalized, l->sources[i].name))
      return fail(l, name, "duplicate resource");
  Source *s = &l->sources[l->count];
  s->bytes = malloc(length ? length : 1);
  if (!s->bytes)
    return fail(l, name, "source allocation");
  if (length)
    memcpy(s->bytes, bytes, length);
  strcpy(s->name, normalized);
  s->length = length;
  l->count++;
  l->bytes += length;
  return 0;
}
uint32_t bw_ui_loader_load(BwUiLoader *l, const char *name) {
  if (!l)
    return 1;
  if (!l->depth)
    l->error[0] = 0;
  char normalized[NAME_SIZE];
  if (normalize(name, l->path, normalized))
    return fail(l, name, "invalid resource path");
  if (l->depth >= 64)
    return fail(l, normalized, "resource depth");
  Source *s = NULL;
  for (uint32_t i = 0; i < l->count; i++)
    if (!strcmp(l->sources[i].name, normalized)) {
      s = &l->sources[i];
      break;
    }
  if (!s)
    return fail(l, normalized, "local resource not supplied");
  if (s->active)
    return fail(l, normalized, "resource include cycle");
  const char *previous = l->path;
  l->path = s->name;
  l->depth++;
  s->active = 1;
  uint32_t status;
  uint32_t old_context = bw_ui_execution_context(l->L);
  if (l->sealed)
    bw_ui_execution_swap(l->L, 0);
  size_t n = strlen(s->name);
  if (n >= 4 && !strcmp(s->name + n - 4, ".xml")) {
    if (!s->document)
      s->document = bw_xml_parse(s->bytes, s->length);
    if (!s->document || s->document->error)
      status = fail(l, s->name, "invalid XML");
    else {
      status = bw_ui_runtime_document(l->runtime, s->document->root);
      if (status && !l->error[0])
        fail(l, s->name, bw_ui_runtime_error(l->runtime));
    }
  } else if (n >= 4 && !strcmp(s->name + n - 4, ".lua"))
    status = execute(l, s->bytes, s->length, s->name);
  else
    status = fail(l, s->name, "unsupported resource extension");
  s->active = 0;
  l->depth--;
  l->path = previous;
  bw_ui_execution_swap(l->L, old_context);
  return status;
}
const char *bw_ui_loader_error(const BwUiLoader *l) {
  return l ? l->error : "no loader";
}
void bw_ui_loader_free(BwUiLoader *l) {
  if (!l)
    return;
  bw_ui_runtime_dispatch(l->runtime, NULL, NULL);
  for (uint32_t i = 0; i < l->count; i++) {
    free(l->sources[i].bytes);
    bw_xml_free(l->sources[i].document);
  }
  free(l);
}

uint32_t bw_ui_loader_seal_verified(BwUiLoader *l) {
  if (!l || !l->count || l->depth || l->sealed)
    return 1;
  for (uint32_t i = 0; i < l->count; i++)
    if (l->sources[i].active)
      return 1;
  l->sealed = 1;
  return 0;
}
