#ifndef BRWOWSER_XML_DOCUMENT_H
#define BRWOWSER_XML_DOCUMENT_H
#include <stdint.h>
typedef struct BwXmlAttribute {
  char *name, *value;
  struct BwXmlAttribute *next;
} BwXmlAttribute;
typedef struct BwXmlNode {
  char *name, *text;
  BwXmlAttribute *attributes;
  struct BwXmlNode *child, *next;
} BwXmlNode;
typedef struct BwXmlDocument {
  BwXmlNode *root;
  uint32_t error, position, nodes;
} BwXmlDocument;
/* Selected UTF-8 XML syntax; no DTD, external entities or namespace resolver. */
BwXmlDocument *bw_xml_parse(const uint8_t *source, uint32_t length);
void bw_xml_free(BwXmlDocument *document);
#endif
