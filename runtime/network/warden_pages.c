/* Included by warden_cpu.c: legacy Win32 page services for the owned guest VM.
 * No API queries or reads the browser host's process address space. */
static void page_error(uint32_t value) {
  for (uint32_t i = 0; i < 4; i++)
    teb[0x34 + i] = (uint8_t)(value >> (i * 8));
}
static uint32_t guest_word(uint32_t address, uint32_t value) {
  uint8_t bytes[4];
  for (uint32_t i = 0; i < 4; i++)
    bytes[i] = (uint8_t)(value >> (i * 8));
  return bw_warden_memory_transfer(&memory, address, bytes, sizeof(bytes), BW_MEMORY_WRITE);
}
static int page_service(x86emu_t *context, uint32_t id, uint32_t *result, uint32_t *arguments) {
  if (id < 12 || id > 19)
    return 0;
  if (!refresh_shared_clock()) {
    outcome = 4;
    x86emu_stop(context);
    return -1;
  }
  static const uint8_t counts[] = {3, 4, 5, 0, 0, 1, 2, 1};
  uint32_t a[5] = {0};
  *arguments = counts[id - 12];
  for (uint32_t i = 0; i < *arguments; i++)
    if (memory_access(context, context->x86.R_ESP + 4 + i * 4, &a[i],
                      X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
  if (id == 15) { /* GetCurrentProcess */
    *result = 0xffffffffu;
  } else if (id == 16) { /* GetLastError */
    *result = (uint32_t)teb[0x34] | (uint32_t)teb[0x35] << 8 | (uint32_t)teb[0x36] << 16 |
              (uint32_t)teb[0x37] << 24;
  } else if (id == 17) { /* SetLastError */
    page_error(a[0]);
  } else if (id == 18) { /* IsBadReadPtr: do not touch inaccessible bytes. */
    *result = a[1] && bw_warden_memory_probe(&memory, a[0], a[1], BW_MEMORY_READ);
  } else if (id == 19) { /* QueryPerformanceFrequency, matching microsecond QPC. */
    if (bw_warden_memory_probe(&memory, a[0], 8, BW_MEMORY_WRITE))
      page_error(998);
    else {
      guest_word(a[0], 1000000);
      guest_word(a[0] + 4, 0);
      *result = 1;
    }
  } else if (id == 12 || id == 13) { /* VirtualQuery / VirtualQueryEx */
    uint32_t start = id == 13 ? 1 : 0;
    if (id == 13 && a[0] != 0xffffffffu) {
      page_error(6);
      return 1;
    }
    BwWardenMemoryInfo info;
    if (a[start + 2] < sizeof(info) || bw_warden_memory_query(&memory, a[start], &info)) {
      page_error(87);
      return 1;
    }
    uint32_t words[7] = {info.base, info.allocation, info.allocation_protection,
                         info.size, info.state,      info.protection,
                         info.type};
    uint8_t bytes[28];
    for (uint32_t i = 0; i < 7; i++)
      for (uint32_t j = 0; j < 4; j++)
        bytes[i * 4 + j] = (uint8_t)(words[i] >> (j * 8));
    if (bw_warden_memory_transfer(&memory, a[start + 1], bytes, sizeof(bytes), BW_MEMORY_WRITE))
      page_error(998);
    else
      *result = sizeof(bytes);
  } else { /* ReadProcessMemory: validate both complete spans before copying. */
    if (a[0] != 0xffffffffu) {
      page_error(6);
      return 1;
    }
    if (a[4] && guest_word(a[4], 0)) {
      page_error(998);
      return 1;
    }
    if (!a[3]) {
      *result = 1;
      return 1;
    }
    if (bw_warden_memory_probe(&memory, a[1], a[3], BW_MEMORY_READ) ||
        bw_warden_memory_probe(&memory, a[2], a[3], BW_MEMORY_WRITE)) {
      page_error(299);
      return 1;
    }
    if (a[3] > 1000000 - accesses) {
      outcome = 5;
      x86emu_stop(context);
      return -1;
    }
    accesses += a[3];
    uint8_t *bytes = malloc(a[3]);
    if (!bytes) {
      page_error(8);
      return 1;
    }
    uint32_t status = bw_warden_memory_transfer(&memory, a[1], bytes, a[3], BW_MEMORY_READ);
    if (!status)
      status = bw_warden_memory_transfer(&memory, a[2], bytes, a[3], BW_MEMORY_WRITE);
    scrub(bytes, a[3]);
    free(bytes);
    if (status)
      page_error(299);
    else {
      if (a[4])
        guest_word(a[4], a[3]);
      *result = 1;
    }
  }
  return 1;
}
