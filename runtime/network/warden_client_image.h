#ifndef BRWOWSER_WARDEN_CLIENT_IMAGE_H
#define BRWOWSER_WARDEN_CLIENT_IMAGE_H
#include "warden_memory.h"

/* A loaded guest image, not a snapshot of a separate Windows/Wine process.
 * Mapping does not execute the PE entry point or finish Windows imports/TLS. */
void bw_client_image_clear(void);
uint32_t bw_client_image_begin(uint32_t size);
uint32_t bw_client_image_append(const uint8_t *bytes, uint32_t size);
uint32_t bw_client_image_finish(void);
uint32_t bw_client_image_map(BwWardenMemory *memory);
uint32_t bw_client_image_base(void);
#endif
