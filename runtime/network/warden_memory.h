#ifndef BRWOWSER_WARDEN_MEMORY_H
#define BRWOWSER_WARDEN_MEMORY_H
#include <stdint.h>

enum {
  BW_MEMORY_READ = 1,
  BW_MEMORY_WRITE = 2,
  BW_MEMORY_EXECUTE = 4,
  BW_MEMORY_REGIONS = 8,
  BW_MEMORY_PAGE_SIZE = 4096
};
enum {
  BW_MEMORY_OK,
  BW_MEMORY_INVALID,
  BW_MEMORY_OVERFLOW,
  BW_MEMORY_UNMAPPED,
  BW_MEMORY_PROTECTED
};
typedef struct {
  uint32_t address, length, access, reason;
} BwWardenMemoryFault;
typedef struct {
  uint32_t base, size, permissions, type;
  uint8_t *bytes;
  const uint8_t *pages;
} BwWardenMemoryRegion;
typedef struct {
  BwWardenMemoryRegion regions[BW_MEMORY_REGIONS];
  uint32_t count;
  BwWardenMemoryFault fault;
} BwWardenMemory;
typedef struct BwWardenMemorySnapshot BwWardenMemorySnapshot;

/* Explicit legacy x86 MEMORY_BASIC_INFORMATION32 layout, not host ABI. */
typedef struct {
  uint32_t base, allocation, allocation_protection, size, state, protection, type;
} BwWardenMemoryInfo;
_Static_assert(sizeof(BwWardenMemoryInfo) == 28, "guest MBI32 layout");

/* Internal live VM mappings only. No API accepts pointers from the network.
 * The caller retains buffers and page flags until reset; neither is copied.
 * A page-flags array has ceil(size/4096) entries and requires aligned base. */
void bw_warden_memory_reset(BwWardenMemory *memory);
uint32_t bw_warden_memory_map(BwWardenMemory *memory, uint32_t base, uint8_t *bytes, uint32_t size,
                              uint32_t permissions, const uint8_t *pages);
uint32_t bw_warden_memory_probe(BwWardenMemory *memory, uint32_t address, uint32_t length,
                                uint32_t access);
uint32_t bw_warden_memory_query(BwWardenMemory *memory, uint32_t address, BwWardenMemoryInfo *info);
/* Internal fork of fixed owned mappings. No bytes leave WASM. Restore refuses
 * changed mappings/protections before restoring any bytes. */
BwWardenMemorySnapshot *bw_warden_memory_snapshot(BwWardenMemory *memory);
uint32_t bw_warden_memory_restore(BwWardenMemory *memory, BwWardenMemorySnapshot *snapshot);
void bw_warden_memory_snapshot_free(BwWardenMemorySnapshot *snapshot);
/* Validate the complete span before copying. On failure, output/mapped bytes
 * remain unchanged. No missing address is substituted with a zero byte. */
uint32_t bw_warden_memory_transfer(BwWardenMemory *memory, uint32_t address, uint8_t *buffer,
                                   uint32_t length, uint32_t access);
#endif
