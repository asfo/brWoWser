#include "document.h"
#include <stdlib.h>
#include <string.h>
typedef struct Parser {
  const uint8_t *s;
  uint32_t length, at, depth;
  BwXmlDocument *doc;
} Parser;
static void fail(Parser *p, uint32_t code) {
  if (!p->doc->error) {
    p->doc->error = code;
    p->doc->position = p->at;
  }
}
static int white(uint8_t c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
static void space(Parser *p) {
  while (p->at < p->length && white(p->s[p->at]))
    p->at++;
}
static int starts(Parser *p, const char *s) {
  size_t n = strlen(s);
  return n <= p->length - p->at && !memcmp(p->s + p->at, s, n);
}
static uint32_t end_at(Parser *p, const char *s) {
  size_t n = strlen(s);
  for (uint32_t i = p->at; i + n <= p->length; i++)
    if (!memcmp(p->s + i, s, n))
      return i;
  fail(p, 2);
  return p->length;
}
static int valid_char(uint32_t c) {
  return c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 0xd7ff) || (c >= 0xe000 && c <= 0xfffd) ||
         (c >= 0x10000 && c <= 0x10ffff);
}
static int utf8(const uint8_t *s, uint32_t n) {
  for (uint32_t i = 0; i < n;) {
    uint32_t c = s[i++], k = 0, min = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      k = 1;
      c &= 31;
      min = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
      k = 2;
      c &= 15;
      min = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
      k = 3;
      c &= 7;
      min = 65536;
    } else if (c >= 128)
      return 0;
    if (k > n - i)
      return 0;
    for (uint32_t j = 0; j < k; j++) {
      uint8_t b = s[i++];
      if ((b & 192) != 128)
        return 0;
      c = (c << 6) | (b & 63);
    }
    if (c < min || !valid_char(c))
      return 0;
  }
  return 1;
}
static uint32_t emit(char *out, uint32_t c) {
  if (c < 128) {
    out[0] = (char)c;
    return 1;
  }
  if (c < 2048) {
    out[0] = (char)(192 | (c >> 6));
    out[1] = (char)(128 | (c & 63));
    return 2;
  }
  if (c < 65536) {
    out[0] = (char)(224 | (c >> 12));
    out[1] = (char)(128 | ((c >> 6) & 63));
    out[2] = (char)(128 | (c & 63));
    return 3;
  }
  out[0] = (char)(240 | (c >> 18));
  out[1] = (char)(128 | ((c >> 12) & 63));
  out[2] = (char)(128 | ((c >> 6) & 63));
  out[3] = (char)(128 | (c & 63));
  return 4;
}
static char *decode(Parser *p, uint32_t start, uint32_t end, int attribute, int entities) {
  char *out = (char *)malloc((size_t)(end - start) + 1);
  if (!out) {
    fail(p, 3);
    return NULL;
  }
  uint32_t n = 0;
  for (uint32_t i = start; i < end; i++) {
    uint8_t c = p->s[i];
    if (c == '\r') {
      if (i + 1 < end && p->s[i + 1] == '\n')
        i++;
      out[n++] = attribute ? ' ' : '\n';
    } else if (attribute && (c == '\n' || c == '\t'))
      out[n++] = ' ';
    else if (c == '&' && entities) {
      uint32_t j = i + 1;
      while (j < end && p->s[j] != ';' && j - i < 32)
        j++;
      if (j == end || p->s[j] != ';') {
        fail(p, 4);
        break;
      }
      uint32_t value = 0;
      const uint8_t *s = p->s + i + 1;
      uint32_t count = j - i - 1;
      if (count == 3 && !memcmp(s, "amp", 3))
        value = '&';
      else if (count == 2 && !memcmp(s, "lt", 2))
        value = '<';
      else if (count == 2 && !memcmp(s, "gt", 2))
        value = '>';
      else if (count == 4 && !memcmp(s, "apos", 4))
        value = '\'';
      else if (count == 4 && !memcmp(s, "quot", 4))
        value = '"';
      else if (count > 1 && s[0] == '#') {
        uint32_t base = 10, k = 1;
        if (s[k] == 'x') {
          base = 16;
          k++;
        }
        if (k == count) {
          fail(p, 4);
          break;
        }
        for (; k < count; k++) {
          uint32_t digit = s[k] >= '0' && s[k] <= '9'   ? s[k] - '0'
                           : s[k] >= 'a' && s[k] <= 'f' ? s[k] - 'a' + 10
                           : s[k] >= 'A' && s[k] <= 'F' ? s[k] - 'A' + 10
                                                        : 99;
          if (digit >= base || value > (0x10ffff - digit) / base) {
            fail(p, 4);
            break;
          }
          value = value * base + digit;
        }
      } else {
        fail(p, 4);
        break;
      }
      if (p->doc->error || !valid_char(value)) {
        fail(p, 4);
        break;
      }
      n += emit(out + n, value);
      i = j;
    } else
      out[n++] = (char)c;
  }
  out[n] = 0;
  if (p->doc->error) {
    free(out);
    return NULL;
  }
  return out;
}
static int first(uint8_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':';
}
static char *name(Parser *p) {
  uint32_t start = p->at;
  if (start >= p->length || !first(p->s[start])) {
    fail(p, 5);
    return NULL;
  }
  p->at++;
  while (p->at < p->length && (first(p->s[p->at]) || (p->s[p->at] >= '0' && p->s[p->at] <= '9') ||
                               p->s[p->at] == '-' || p->s[p->at] == '.'))
    p->at++;
  return decode(p, start, p->at, 0, 0);
}
static void release_node(BwXmlNode *n) {
  while (n) {
    BwXmlNode *next = n->next;
    release_node(n->child);
    BwXmlAttribute *a = n->attributes;
    while (a) {
      BwXmlAttribute *t = a->next;
      free(a->name);
      free(a->value);
      free(a);
      a = t;
    }
    free(n->name);
    free(n->text);
    free(n);
    n = next;
  }
}
void bw_xml_free(BwXmlDocument *d) {
  if (d) {
    release_node(d->root);
    free(d);
  }
}
static void comment(Parser *p) {
  p->at += 4;
  uint32_t end = end_at(p, "-->");
  for (uint32_t i = p->at; i + 1 < end; i++)
    if (p->s[i] == '-' && p->s[i + 1] == '-')
      fail(p, 6);
  p->at = p->doc->error ? p->length : end + 3;
}
static void pi(Parser *p) {
  p->at += 2;
  uint32_t end = end_at(p, "?>");
  p->at = p->doc->error ? p->length : end + 2;
}
static int append(Parser *p, BwXmlNode *n, char *part) {
  if (!part)
    return 0;
  size_t old = n->text ? strlen(n->text) : 0, add = strlen(part);
  if (old + add > 1048576) {
    free(part);
    fail(p, 7);
    return 0;
  }
  char *text = (char *)realloc(n->text, old + add + 1);
  if (!text) {
    free(part);
    fail(p, 3);
    return 0;
  }
  memcpy(text + old, part, add + 1);
  free(part);
  n->text = text;
  return 1;
}
static BwXmlNode *node(Parser *p) {
  if (++p->depth > 128 || ++p->doc->nodes > 16384) {
    fail(p, 7);
    p->depth--;
    return NULL;
  }
  BwXmlNode *n = (BwXmlNode *)calloc(1, sizeof(*n));
  if (!n) {
    fail(p, 3);
    p->depth--;
    return NULL;
  }
  p->at++;
  n->name = name(p);
  BwXmlAttribute **tail = &n->attributes;
  uint32_t attributes = 0;
  while (!p->doc->error) {
    uint32_t before = p->at;
    space(p);
    if (starts(p, "/>")) {
      p->at += 2;
      p->depth--;
      return n;
    }
    if (starts(p, ">")) {
      p->at++;
      break;
    }
    if (before == p->at || ++attributes > 128) {
      fail(p, 5);
      break;
    }
    BwXmlAttribute *a = (BwXmlAttribute *)calloc(1, sizeof(*a));
    if (!a) {
      fail(p, 3);
      break;
    }
    *tail = a;
    tail = &a->next;
    a->name = name(p);
    if (p->doc->error)
      break;
    for (BwXmlAttribute *t = n->attributes; t != a; t = t->next)
      if (!strcmp(t->name, a->name)) {
        fail(p, 8);
        break;
      }
    space(p);
    if (p->at >= p->length || p->s[p->at++] != '=') {
      fail(p, 5);
      break;
    }
    space(p);
    if (p->at >= p->length || (p->s[p->at] != '\'' && p->s[p->at] != '"')) {
      fail(p, 5);
      break;
    }
    uint8_t quote = p->s[p->at++];
    uint32_t start = p->at;
    while (p->at < p->length && p->s[p->at] != quote) {
      if (p->s[p->at] == '<')
        fail(p, 5);
      p->at++;
    }
    if (p->at == p->length) {
      fail(p, 2);
      break;
    }
    a->value = decode(p, start, p->at, 1, 1);
    p->at++;
  }
  BwXmlNode **children = &n->child;
  while (!p->doc->error) {
    if (p->at >= p->length) {
      fail(p, 2);
      break;
    }
    if (starts(p, "</")) {
      p->at += 2;
      char *close = name(p);
      space(p);
      if (!close || strcmp(n->name, close) || p->at >= p->length || p->s[p->at++] != '>')
        fail(p, 9);
      free(close);
      break;
    }
    if (starts(p, "<!--")) {
      comment(p);
      continue;
    }
    if (starts(p, "<?")) {
      pi(p);
      continue;
    }
    if (starts(p, "<![CDATA[")) {
      p->at += 9;
      uint32_t start = p->at, end = end_at(p, "]]>");
      if (p->doc->error)
        break;
      append(p, n, decode(p, start, end, 0, 0));
      p->at = end + 3;
      continue;
    }
    if (p->s[p->at] == '<') {
      if (starts(p, "<!")) {
        fail(p, 10);
        break;
      }
      BwXmlNode *c = node(p);
      if (!c)
        break;
      *children = c;
      children = &c->next;
      continue;
    }
    uint32_t start = p->at;
    while (p->at < p->length && p->s[p->at] != '<') {
      if (starts(p, "]]>")) {
        fail(p, 5);
        break;
      }
      p->at++;
    }
    if (!p->doc->error)
      append(p, n, decode(p, start, p->at, 0, 1));
  }
  p->depth--;
  if (p->doc->error) {
    release_node(n);
    return NULL;
  }
  return n;
}
BwXmlDocument *bw_xml_parse(const uint8_t *source, uint32_t length) {
  BwXmlDocument *d = (BwXmlDocument *)calloc(1, sizeof(*d));
  if (!d)
    return NULL;
  Parser p = {source, length, 0, 0, d};
  if (!source || !length || length > 1048576 || !utf8(source, length)) {
    fail(&p, 1);
    return d;
  }
  if (length >= 3 && !memcmp(source, "\xef\xbb\xbf", 3))
    p.at = 3;
  while (!d->error) {
    space(&p);
    if (starts(&p, "<!--")) {
      comment(&p);
      continue;
    }
    if (starts(&p, "<?")) {
      pi(&p);
      continue;
    }
    break;
  }
  if (!d->error) {
    if (p.at >= length || source[p.at] != '<' || starts(&p, "<!") || starts(&p, "</"))
      fail(&p, 5);
    else
      d->root = node(&p);
  }
  while (!d->error) {
    space(&p);
    if (starts(&p, "<!--")) {
      comment(&p);
      continue;
    }
    if (starts(&p, "<?")) {
      pi(&p);
      continue;
    }
    if (p.at != length)
      fail(&p, 5);
    break;
  }
  if (d->error) {
    release_node(d->root);
    d->root = NULL;
  }
  return d;
}
