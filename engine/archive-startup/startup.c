/* Portable reconstruction of the observed local/non-streaming valid-archive
 * startup path. Inventory is input; captured priorities are never inputs. */
typedef unsigned int U;
static char files[128][256], locale[8];
static struct Mount {
  U file, priority, sequence, flags;
} mounts[128];
static U mounted;
#include "base-table.h"
char *files_ptr(void) {
  return &files[0][0];
}
char *locale_ptr(void) {
  return locale;
}
U *mounts_ptr(void) {
  return (U *)mounts;
}
U mount_count(void) {
  return mounted;
}
static char fold(char c) {
  if (c >= 'A' && c <= 'Z')
    c += 32;
  if (c == '/')
    c = '\\';
  return c;
}
static U len(const char *s) {
  U n = 0;
  while (n < 256 && s[n])
    n++;
  return n;
}
static int cmp(const char *a, const char *b) {
  while (*a && fold(*a) == fold(*b)) {
    a++;
    b++;
  }
  return (unsigned char)fold(*a) - (unsigned char)fold(*b);
}
static int match(const char *s, const char *p) {
  while (*s && *p) {
    if (*p == '?') {
      if (*s == '\\' || *s == '/')
        return 0;
    } else if (fold(*s) != fold(*p))
      return 0;
    s++;
    p++;
  }
  return !*s && !*p;
}
static int find(const char *s, U n) {
  for (U i = 0; i < n; i++)
    if (!cmp(files[i], s))
      return (int)i;
  return -1;
}
static U add(U index, U priority) {
  if (mounted >= 128)
    return 0;
  mounts[mounted].file = index;
  mounts[mounted].priority = priority;
  mounts[mounted].sequence = 0x80000000u + mounted;
  mounts[mounted].flags = 0xc00;
  mounted++;
  return 1;
}
static void localized(char *out, const char *name) {
  U at = 0;
  while (*name) {
    if (name[0] == '*' && name[1] == '*' && name[2] == '*' && name[3] == '*') {
      for (U j = 0; j < 4; j++)
        out[at++] = locale[j];
      name += 4;
    } else
      out[at++] = *name++;
  }
  out[at] = 0;
}
U plan(U n) {
  mounted = 0;
  if (n > 128)
    return 2;
  for (U j = 0; j < 4; j++)
    if (!((locale[j] >= 'A' && locale[j] <= 'Z') || (locale[j] >= 'a' && locale[j] <= 'z')))
      return 5;
  if (locale[4])
    return 5;
  for (U i = 0; i < n; i++) {
    U l = len(files[i]);
    if (!l || l >= 256 || files[i][0] == '/' || files[i][0] == '\\')
      return 3;
    for (U j = 0; j < l; j++)
      if ((unsigned char)files[i][j] < 32 || (unsigned char)files[i][j] > 126 ||
          files[i][j] == ':' || (files[i][j] == '.' && files[i][j + 1] == '.'))
        return 3;
    for (U j = 0; j < i; j++)
      if (!cmp(files[i], files[j]))
        return 3;
  }
  if (find("common.MPQ", n) < 0)
    return 4; /* Legacy no-common path is not reconstructed. */
  if (find("alternate.MPQ", n) >= 0)
    return 8; /* Special early category-3 mounting. */
  char localPattern[64], localBase[64];
  localized(localPattern, "****\\patch-****-?.MPQ");
  localized(localBase, "****\\patch-****.MPQ");
  U patches[128], count = 0;
  for (U i = 0; i < n; i++)
    if (match(files[i], "patch-?.MPQ") || match(files[i], localPattern))
      patches[count++] = i;
  /* Native comparator 0x401200 sorts phase-zero matches descending; the
  * opening loop walks the final vector backwards. */
  for (U i = 1; i < count; i++) {
    U v = patches[i], at = i;
    while (at && cmp(files[v], files[patches[at - 1]]) > 0) {
      patches[at] = patches[at - 1];
      at--;
    }
    patches[at] = v;
  }
  int root = find("patch.MPQ", n), local = find(localBase, n);
  if (root >= 0) {
    if (count >= 128)
      return 7;
    patches[count++] = (U)root;
  }
  if (local >= 0) {
    if (count >= 128)
      return 7;
    patches[count++] = (U)local;
  }
  U priority = 64;
  for (U i = count; i > 0; i--)
    if (!add(patches[i - 1], priority++))
      return 7;
  priority = 63;
  for (U i = 0; i < 28; i++) {
    const struct Base *b = &bases[i];
    if (b->category == 3 || b->category == 4)
      continue;
    if (b->mask & 2) {
      char path[256];
      localized(path, b->name);
      int index = find(path, n);
      if (index >= 0) {
        if (!add((U)index, priority))
          return 7;
      } else if (b->category == 1) {
        mounted = 0;
        return 6;
      }
    }
    priority--; /* Also consumed by filtered or absent base entries. */
  }
  return 1;
}
