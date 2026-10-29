#include "warden_client_image.h"
#include <stdlib.h>
#include <string.h>

#define FILE_LIMIT (16u * 1024u * 1024u)
#define IMAGE_LIMIT (32u * 1024u * 1024u)
static uint8_t *source, *image, *pages;
static uint32_t source_size, source_written, image_size, image_base;
static uint32_t read16(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}
static uint32_t read32(const uint8_t *p) {
  return read16(p) | read16(p + 2) << 16;
}
static int span(uint32_t at, uint32_t length, uint32_t size) {
  return at <= size && length <= size - at;
}
static void wipe_free(uint8_t **buffer, uint32_t size) {
  if (*buffer) {
    volatile uint8_t *p = *buffer;
    for (uint32_t i = 0; i < size; i++)
      p[i] = 0;
    free(*buffer);
    *buffer = NULL;
  }
}
void bw_client_image_clear(void) {
  wipe_free(&source, source_size);
  wipe_free(&image, image_size);
  wipe_free(&pages, (image_size + BW_MEMORY_PAGE_SIZE - 1) / BW_MEMORY_PAGE_SIZE);
  source_size = source_written = image_size = image_base = 0;
}
uint32_t bw_client_image_begin(uint32_t size) {
  bw_client_image_clear();
  if (size < 64 || size > FILE_LIMIT)
    return 1;
  source = malloc(size);
  if (!source)
    return 2;
  source_size = size;
  return 0;
}
uint32_t bw_client_image_append(const uint8_t *bytes, uint32_t size) {
  if (!source || !bytes || !size || size > 65536 || size > source_size - source_written)
    return 1;
  memcpy(source + source_written, bytes, size);
  source_written += size;
  return 0;
}
uint32_t bw_client_image_finish(void) {
  uint32_t status = 3;
  if (!source || source_written != source_size || read16(source) != 0x5a4d)
    goto invalid;
  uint32_t pe = read32(source + 60);
  if (!span(pe, 24, source_size) || read32(source + pe) != 0x4550 ||
      read16(source + pe + 4) != 0x14c)
    goto invalid;
  uint32_t count = read16(source + pe + 6), optional_size = read16(source + pe + 20),
           characteristics = read16(source + pe + 22), optional = pe + 24;
  if (!count || count > 96 || optional_size < 224 || !(characteristics & 2) ||
      (characteristics & 0x2000) || !span(optional, optional_size, source_size) ||
      read16(source + optional) != 0x10b)
    goto invalid;
  uint32_t table = optional + optional_size;
  if (!span(table, count * 40, source_size))
    goto invalid;
  uint32_t base = read32(source + optional + 28), size = read32(source + optional + 56),
           headers = read32(source + optional + 60), entry = read32(source + optional + 16),
           alignment = read32(source + optional + 32);
  /* Load at the authored preferred base. Never patch code or apply invented
   * relocations to make a server address appear present. */
  if (base != 0x00400000u || size < headers || size > IMAGE_LIMIT || !headers ||
      !span(0, headers, source_size) || !span(table, count * 40, headers) || entry >= size ||
      alignment < BW_MEMORY_PAGE_SIZE || (alignment & (alignment - 1)) || size % alignment)
    goto invalid;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t *section = source + table + i * 40;
    uint32_t address = read32(section + 12), virtual_size = read32(section + 8),
             raw_size = read32(section + 16), raw = read32(section + 20),
             extent = virtual_size > raw_size ? virtual_size : raw_size;
    if (!extent || address < headers || address % alignment || !span(address, extent, size) ||
        (raw_size && (!span(raw, raw_size, source_size) || raw < headers)))
      goto invalid;
    for (uint32_t j = 0; j < i; j++) {
      const uint8_t *other = source + table + j * 40;
      uint32_t start = read32(other + 12), v = read32(other + 8), r = read32(other + 16),
               length = v > r ? v : r;
      if (address < start + length && start < address + extent)
        goto invalid;
    }
  }
  image_size = size;
  image = calloc(1, size);
  pages = calloc((size + BW_MEMORY_PAGE_SIZE - 1) / BW_MEMORY_PAGE_SIZE, 1);
  if (!image || !pages) {
    status = 2;
    goto invalid;
  }
  memcpy(image, source, headers);
  for (uint32_t page = 0; page <= (headers - 1) / BW_MEMORY_PAGE_SIZE; page++)
    pages[page] = BW_MEMORY_READ;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t *section = source + table + i * 40;
    uint32_t address = read32(section + 12), v = read32(section + 8),
             raw_size = read32(section + 16), extent = v > raw_size ? v : raw_size,
             flags = read32(section + 36);
    uint8_t permission = ((flags & 0x40000000u) ? BW_MEMORY_READ : 0) |
                         ((flags & 0x80000000u) ? BW_MEMORY_WRITE : 0) |
                         ((flags & 0x20000000u) ? BW_MEMORY_EXECUTE : 0);
    if (raw_size)
      memcpy(image + address, source + read32(section + 20), raw_size);
    for (uint32_t page = address / BW_MEMORY_PAGE_SIZE;
         page <= (address + extent - 1) / BW_MEMORY_PAGE_SIZE; page++)
      pages[page] |= permission;
  }
  if (!(pages[entry / BW_MEMORY_PAGE_SIZE] & BW_MEMORY_EXECUTE))
    goto invalid;
  image_base = base;
  wipe_free(&source, source_size);
  source_size = source_written = 0;
  return 0;
invalid:
  bw_client_image_clear();
  return status;
}
uint32_t bw_client_image_map(BwWardenMemory *memory) {
  if (!image)
    return 0; /* No image was supplied: native address space stays unavailable. */
  uint32_t status = bw_warden_memory_map(memory, image_base, image, image_size, 7, pages);
  if (!status)
    memory->regions[memory->count - 1].type = 0x1000000; /* MEM_IMAGE */
  return status;
}
uint32_t bw_client_image_base(void) {
  return image_base;
}
