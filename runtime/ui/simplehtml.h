#ifndef BRWOWSER_SIMPLEHTML_H
#define BRWOWSER_SIMPLEHTML_H
#include <stdint.h>
typedef struct BwHtmlParagraph {
  char *text;
  uint32_t heading;
  char align[8];
  int align_explicit, break_line;
} BwHtmlParagraph;
typedef struct BwHtmlDocument {
  BwHtmlParagraph *paragraphs;
  uint32_t count;
  int literal_fallback;
} BwHtmlDocument;
/* Selected native XML-like SimpleHTML grammar. Not browser HTML parsing.
 * Own input policy: 1MiB, 2048 paragraphs; unsupported valid tags reject.
 * Malformed XML follows the observed whole-input literal FontString branch. */
int bw_html_parse(const char *source, BwHtmlDocument *out);
void bw_html_free(BwHtmlDocument *document);
#endif
