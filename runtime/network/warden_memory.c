#include "warden_memory.h"
#include <string.h>
#include <stdlib.h>

void bw_warden_memory_reset(BwWardenMemory *memory) {
  if (memory)
    memset(memory, 0, sizeof(*memory));
}
uint32_t bw_warden_memory_map(BwWardenMemory *memory, uint32_t base, uint8_t *bytes, uint32_t size,
                              uint32_t permissions, const uint8_t *pages) {
  if (!memory || !bytes || !size || !permissions || (permissions & ~7u) ||
      memory->count >= BW_MEMORY_REGIONS || (pages && base % BW_MEMORY_PAGE_SIZE))
    return BW_MEMORY_INVALID;
  uint64_t end = (uint64_t)base + size;
  if (end > UINT64_C(0x100000000))
    return BW_MEMORY_OVERFLOW;
  for (uint32_t i = 0; i < memory->count; i++) {
    const BwWardenMemoryRegion *region = &memory->regions[i];
    if ((uint64_t)base < (uint64_t)region->base + region->size && region->base < end)
      return BW_MEMORY_INVALID;
  }
  memory->regions[memory->count++] =
      (BwWardenMemoryRegion){base, size, permissions, 0x20000, bytes, pages};
  return BW_MEMORY_OK;
}
static BwWardenMemoryRegion *region_at(BwWardenMemory *memory, uint32_t address) {
  for (uint32_t i = 0; i < memory->count; i++) {
    BwWardenMemoryRegion *region = &memory->regions[i];
    if (address >= region->base && address - region->base < region->size)
      return region;
  }
  return NULL;
}
static uint32_t protection(uint32_t permissions) {
  if ((permissions & BW_MEMORY_WRITE) && !(permissions & BW_MEMORY_READ))
    return 0; /* This generic mapping has no exact Win32 protection equivalent. */
  if (permissions & BW_MEMORY_EXECUTE)
    return permissions & BW_MEMORY_WRITE ? 0x40 : permissions & BW_MEMORY_READ ? 0x20 : 0x10;
  return permissions & BW_MEMORY_WRITE ? 4 : permissions & BW_MEMORY_READ ? 2 : 1;
}
uint32_t bw_warden_memory_query(BwWardenMemory *memory, uint32_t address,
                                BwWardenMemoryInfo *info) {
  const uint32_t limit = 0x7fff0000u;
  if (!memory || !info || address >= limit)
    return BW_MEMORY_INVALID;
  memset(info, 0, sizeof(*info));
  uint32_t base = address & ~(BW_MEMORY_PAGE_SIZE - 1);
  info->base = base;
  BwWardenMemoryRegion *region = region_at(memory, base);
  if (!region) {
    uint32_t end = limit;
    for (uint32_t i = 0; i < memory->count; i++)
      if (memory->regions[i].base > base && memory->regions[i].base < end)
        end = memory->regions[i].base;
    info->size = end - base;
    info->state = 0x10000; /* MEM_FREE: no backing bytes are returned. */
    return BW_MEMORY_OK;
  }
  if (region->base % BW_MEMORY_PAGE_SIZE || region->size % BW_MEMORY_PAGE_SIZE ||
      region->base >= limit || region->size > limit - region->base)
    return BW_MEMORY_INVALID;
  uint32_t offset = base - region->base, flags = region->permissions;
  if (region->pages)
    flags &= region->pages[offset / BW_MEMORY_PAGE_SIZE];
  uint32_t end = region->size;
  if (region->pages) {
    end = offset + BW_MEMORY_PAGE_SIZE;
    while (end < region->size &&
           (region->permissions & region->pages[end / BW_MEMORY_PAGE_SIZE]) == flags)
      end += BW_MEMORY_PAGE_SIZE;
  }
  info->allocation = region->base;
  info->allocation_protection = protection(region->permissions);
  info->size = end - offset;
  info->state = 0x1000; /* MEM_COMMIT, including mapped no-access pages. */
  info->protection = protection(flags);
  info->type = region->type;
  return info->protection && info->allocation_protection ? BW_MEMORY_OK : BW_MEMORY_INVALID;
}
static uint32_t fail(BwWardenMemory *memory, uint32_t address, uint32_t length, uint32_t access,
                     uint32_t reason) {
  memory->fault = (BwWardenMemoryFault){address, length, access, reason};
  return reason;
}
uint32_t bw_warden_memory_probe(BwWardenMemory *memory, uint32_t address, uint32_t length,
                                uint32_t access) {
  if (!memory)
    return BW_MEMORY_INVALID;
  memset(&memory->fault, 0, sizeof(memory->fault));
  if (!length ||
      (access != BW_MEMORY_READ && access != BW_MEMORY_WRITE && access != BW_MEMORY_EXECUTE))
    return fail(memory, address, length, access, BW_MEMORY_INVALID);
  if ((uint64_t)address + length > UINT64_C(0x100000000))
    return fail(memory, address, length, access, BW_MEMORY_OVERFLOW);
  uint32_t remaining = length;
  while (remaining) {
    BwWardenMemoryRegion *region = region_at(memory, address);
    if (!region)
      return fail(memory, address, remaining, access, BW_MEMORY_UNMAPPED);
    uint32_t offset = address - region->base, span = region->size - offset;
    if (region->pages) {
      uint32_t page_remaining = BW_MEMORY_PAGE_SIZE - offset % BW_MEMORY_PAGE_SIZE;
      if (span > page_remaining)
        span = page_remaining;
    }
    uint32_t permissions = region->permissions;
    if (region->pages)
      permissions &= region->pages[offset / BW_MEMORY_PAGE_SIZE];
    if (!(permissions & access))
      return fail(memory, address, remaining, access, BW_MEMORY_PROTECTED);
    if (span > remaining)
      span = remaining;
    remaining -= span;
    address += span;
  }
  return BW_MEMORY_OK;
}
uint32_t bw_warden_memory_transfer(BwWardenMemory *memory, uint32_t address, uint8_t *buffer,
                                   uint32_t length, uint32_t access) {
  if (!memory || !buffer)
    return BW_MEMORY_INVALID;
  uint32_t status = bw_warden_memory_probe(memory, address, length, access);
  if (status)
    return status;
  while (length) {
    BwWardenMemoryRegion *region = region_at(memory, address);
    uint32_t offset = address - region->base, span = region->size - offset;
    if (span > length)
      span = length;
    if (access == BW_MEMORY_WRITE)
      memmove(region->bytes + offset, buffer, span);
    else
      memmove(buffer, region->bytes + offset, span);
    address += span;
    buffer += span;
    length -= span;
  }
  return BW_MEMORY_OK;
}

struct BwWardenMemorySnapshot {
  uint32_t count;
  BwWardenMemoryFault fault;
  BwWardenMemoryRegion regions[BW_MEMORY_REGIONS];
  uint8_t *bytes[BW_MEMORY_REGIONS], *pages[BW_MEMORY_REGIONS];
};
static void snapshot_wipe(void *pointer, uint32_t size) {
  volatile uint8_t *bytes = pointer;
  while (size--)
    *bytes++ = 0;
}
void bw_warden_memory_snapshot_free(BwWardenMemorySnapshot *snapshot) {
  if (!snapshot)
    return;
  for (uint32_t i = 0; i < snapshot->count; i++) {
    uint32_t size = snapshot->regions[i].size;
    if (snapshot->bytes[i]) {
      snapshot_wipe(snapshot->bytes[i], size);
      free(snapshot->bytes[i]);
    }
    if (snapshot->pages[i]) {
      snapshot_wipe(snapshot->pages[i],
                    size / BW_MEMORY_PAGE_SIZE + !!(size % BW_MEMORY_PAGE_SIZE));
      free(snapshot->pages[i]);
    }
  }
  snapshot_wipe(snapshot, sizeof(*snapshot));
  free(snapshot);
}
BwWardenMemorySnapshot *bw_warden_memory_snapshot(BwWardenMemory *memory) {
  if (!memory || memory->count > BW_MEMORY_REGIONS)
    return NULL;
  BwWardenMemorySnapshot *snapshot = calloc(1, sizeof(*snapshot));
  if (!snapshot)
    return NULL;
  snapshot->count = memory->count;
  snapshot->fault = memory->fault;
  for (uint32_t i = 0; i < memory->count; i++) {
    BwWardenMemoryRegion *region = &memory->regions[i];
    snapshot->regions[i] = *region;
    snapshot->bytes[i] = malloc(region->size);
    if (!snapshot->bytes[i])
      goto fail;
    memcpy(snapshot->bytes[i], region->bytes, region->size);
    if (region->pages) {
      uint32_t count = region->size / BW_MEMORY_PAGE_SIZE + !!(region->size % BW_MEMORY_PAGE_SIZE);
      snapshot->pages[i] = malloc(count);
      if (!snapshot->pages[i])
        goto fail;
      memcpy(snapshot->pages[i], region->pages, count);
    }
  }
  return snapshot;
fail:
  bw_warden_memory_snapshot_free(snapshot);
  return NULL;
}
uint32_t bw_warden_memory_restore(BwWardenMemory *memory, BwWardenMemorySnapshot *snapshot) {
  if (!memory || !snapshot || memory->count != snapshot->count)
    return BW_MEMORY_INVALID;
  for (uint32_t i = 0; i < memory->count; i++) {
    const BwWardenMemoryRegion *a = &memory->regions[i], *b = &snapshot->regions[i];
    if (a->base != b->base || a->size != b->size || a->permissions != b->permissions ||
        a->type != b->type || a->bytes != b->bytes || a->pages != b->pages)
      return BW_MEMORY_INVALID;
    if (a->pages && memcmp(a->pages, snapshot->pages[i],
                           a->size / BW_MEMORY_PAGE_SIZE + !!(a->size % BW_MEMORY_PAGE_SIZE)))
      return BW_MEMORY_INVALID;
  }
  for (uint32_t i = 0; i < memory->count; i++)
    memcpy(memory->regions[i].bytes, snapshot->bytes[i], memory->regions[i].size);
  memory->fault = snapshot->fault;
  return BW_MEMORY_OK;
}
