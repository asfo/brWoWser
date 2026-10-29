#include "simplehtml.h"
#include "document.h"
#include <stdlib.h>
#include <string.h>
static int equal_align(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char x = *a;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (x != *b)
      return 0;
  }
  return !*a && !*b;
}
static int white(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
static int blank(const char *s) {
  if (s)
    for (; *s; s++)
      if (!white(*s))
        return 0;
  return 1;
}
static char *copy(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}
void bw_html_free(BwHtmlDocument *d) {
  if (!d)
    return;
  for (uint32_t i = 0; i < d->count; i++)
    free(d->paragraphs[i].text);
  free(d->paragraphs);
  memset(d, 0, sizeof(*d));
}
static int append(BwHtmlDocument *d, const char *text, uint32_t heading, const char *align) {
  if (d->count == 2048)
    return 1;
  BwHtmlParagraph *p = realloc(d->paragraphs, (d->count + 1) * sizeof(*p));
  if (!p)
    return 1;
  d->paragraphs = p;
  char *s = copy(text ? text : "");
  if (!s)
    return 1;
  uint32_t start = 0;
  while (s[start] == ' ')
    start++;
  size_t n = strlen(s + start);
  while (n && s[start + n - 1] == ' ')
    n--;
  memmove(s, s + start, n);
  s[n] = 0;
  p = &d->paragraphs[d->count++];
  p->text = s;
  p->heading = heading;
  p->align_explicit = 0;
  p->break_line = 0;
  strcpy(p->align, align);
  return 0;
}
int bw_html_parse(const char *source, BwHtmlDocument *out) {
  if (!source || !out)
    return 1;
  memset(out, 0, sizeof(*out));
  size_t n = strlen(source);
  if (n > 1048576)
    return 1;
  /* Validate original syntax before rewriting breaks. An unclosed <br> takes
  * the native literal fallback, rather than browser HTML's repair behavior. */
  BwXmlDocument *doc = bw_xml_parse((const uint8_t *)source, (uint32_t)n);
  if (!doc)
    return 1;
  if (doc->error) {
    bw_xml_free(doc);
    out->literal_fallback = 1;
    if (append(out, source, 0, "LEFT")) {
      bw_html_free(out);
      return 1;
    }
    char *literal = copy(source);
    if (!literal) {
      bw_html_free(out);
      return 1;
    }
    free(out->paragraphs[0].text);
    out->paragraphs[0].text = literal;
    return 0;
  }
  bw_xml_free(doc);
  char *normalized = malloc(n * 2 + 1);
  if (!normalized)
    return 1;
  size_t at = 0;
  int in_block = 0;
  for (size_t i = 0; i < n;) {
    if (source[i] == '<') {
      size_t tag = i + 1;
      int closing = 0;
      if (tag < n && source[tag] == '/') {
        closing = 1;
        tag++;
      }
      if (tag < n &&
          ((source[tag] == 'p' &&
            (tag + 1 < n && (source[tag + 1] == '>' || white(source[tag + 1])))) ||
           (source[tag] == 'h' && tag + 2 < n && source[tag + 1] >= '1' && source[tag + 1] <= '3' &&
            (source[tag + 2] == '>' || white(source[tag + 2])))))
        in_block = !closing;
    }
    if (in_block && i + 5 <= n && !memcmp(source + i, "<br/>", 5)) {
      memcpy(normalized + at, "&#10;", 5);
      at += 5;
      i += 5;
    } else if (in_block && i + 6 <= n && !memcmp(source + i, "<br />", 6)) {
      memcpy(normalized + at, "&#10;", 5);
      at += 5;
      i += 6;
    } else if (white(source[i])) {
      if (!at || normalized[at - 1] != ' ')
        normalized[at++] = ' ';
      i++;
    } else
      normalized[at++] = source[i++];
  }
  normalized[at] = 0;
  doc = bw_xml_parse((const uint8_t *)normalized, (uint32_t)at);
  free(normalized);
  int status = 1;
  if (!doc || doc->error || !doc->root || strcmp(doc->root->name, "html") || doc->root->next)
    goto done;
  if (doc->root->attributes || !blank(doc->root->text))
    goto done;
  BwXmlNode *body = doc->root->child;
  if (!body || strcmp(body->name, "body") || body->next || body->attributes || !blank(body->text))
    goto done;
  for (BwXmlNode *p = body->child; p; p = p->next) {
    if (!strcmp(p->name, "br") && !p->child && !p->attributes && blank(p->text)) {
      if (append(out, "", 0, "LEFT"))
        goto done;
      out->paragraphs[out->count - 1].break_line = 1;
      continue;
    }
    uint32_t h = !strcmp(p->name, "h1")   ? 1
                 : !strcmp(p->name, "h2") ? 2
                 : !strcmp(p->name, "h3") ? 3
                                          : 0;
    if ((!h && strcmp(p->name, "p")) || p->child)
      goto done;
    const char *align = "LEFT";
    int align_explicit = 0;
    for (BwXmlAttribute *a = p->attributes; a; a = a->next) {
      if (strcmp(a->name, "align"))
        goto done;
      align_explicit = 1;
      if (equal_align(a->value, "center"))
        align = "CENTER";
      else if (equal_align(a->value, "right"))
        align = "RIGHT";
      else if (!equal_align(a->value, "left"))
        goto done;
    }
    if (append(out, p->text, h, align))
      goto done;
    out->paragraphs[out->count - 1].align_explicit = align_explicit;
  }
  status = 0;
done:
  bw_xml_free(doc);
  if (status)
    bw_html_free(out);
  return status;
}
