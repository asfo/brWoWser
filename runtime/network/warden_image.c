extern void bw_warden_cpu_clear(void);
extern uint32_t bw_warden_cpu_checkpoint(void);
extern uint32_t bw_warden_cpu_restore_checkpoint(void);
extern uint32_t bw_warden_cpu_import_name(uint8_t *out, uint32_t capacity);
extern uint32_t bw_warden_cpu_memory_probe(uint32_t address, uint32_t length);
extern uint32_t bw_warden_cpu_memory_fault(uint32_t field);
extern void bw_warden_cpu_bind_client(uint32_t kind, uint32_t rva);
extern uint32_t bw_warden_cpu_run(uint8_t *bytes, uint32_t size, uint32_t entry,
                                  const uint32_t *imports, uint32_t count, const uint8_t *sections,
                                  uint32_t section_count);
extern uint32_t bw_warden_cpu_import_id(const char *library, const char *name);
extern uint32_t bw_warden_cpu_metric(uint32_t field);
extern int bw_warden_cpu_read32(uint32_t address, uint32_t *value);
extern uint32_t bw_warden_cpu_scratch(const uint8_t *data, uint32_t size, uint32_t offset);
extern void bw_warden_cpu_scratch_clear(void);
extern uint32_t bw_warden_cpu_call(uint32_t entry, uint32_t object, const uint32_t *arguments,
                                   uint32_t count);
extern uint32_t bw_warden_cpu_output(uint8_t *out, uint32_t capacity);
extern uint32_t bw_warden_cpu_cipher(uint8_t *out, uint32_t capacity);
static uint32_t warden_execution, warden_object, warden_methods[4], warden_keyed;
/* Reconstruct a signed Warden x86 image as data, never host executable memory.
 * Format corroborated against original 12340 loader at 0x872350.
 * Guest addresses are separate from both host and WASM addresses. */
#define WARDEN_IMAGE_LIMIT 131072
#define WARDEN_GUEST_BASE 0x10000000u
#define WARDEN_IMPORT_LIMIT 128
static uint8_t warden_image[WARDEN_IMAGE_LIMIT];
static uint32_t warden_image_size, warden_entry, warden_relocations, warden_imports;
typedef struct {
  uint32_t slot, ordinal;
  char library[64], name[128];
} WardenImport;
static WardenImport warden_import_table[WARDEN_IMPORT_LIMIT];
static void warden_image_clear(void) {
  bw_warden_cpu_clear();
  warden_execution = warden_object = warden_keyed = 0;
  erase(warden_methods, sizeof(warden_methods));
  erase(warden_image, sizeof(warden_image));
  erase(warden_import_table, sizeof(warden_import_table));
  warden_image_size = warden_entry = warden_relocations = warden_imports = 0;
}
static int warden_image_range(uint32_t offset, uint32_t length) {
  return offset <= warden_image_size && length <= warden_image_size - offset;
}
static int warden_image_string(uint32_t offset, char *out, uint32_t capacity) {
  for (uint32_t i = 0; i < capacity; i++) {
    if (!warden_image_range(offset, 1))
      return 0;
    uint8_t byte = warden_image[offset++];
    if (!byte) {
      out[i] = 0;
      return i != 0;
    }
    if (byte < 32 || byte > 126 || i == capacity - 1)
      return 0;
    out[i] = (char)byte;
  }
  return 0;
}
static void warden_image_write32(uint32_t at, uint32_t value) {
  for (uint32_t i = 0; i < 4; i++)
    warden_image[at + i] = (uint8_t)(value >> (8 * i));
}
static int warden_image_executable(const uint8_t *source, uint32_t sections, uint32_t rva) {
  for (uint32_t i = 0; i < sections; i++) {
    const uint8_t *section = source + 40 + i * 12;
    uint32_t start = warden_read32(section), size = warden_read32(section + 4);
    if (rva >= start && rva - start < size && (warden_read32(section + 8) & 0xf0))
      return 1;
  }
  return 0;
}
/* Returns 1 only for a reconstructed image with validated table bounds.
 * Imports are recorded as unresolved dependencies, not replaced with success stubs. */
static int warden_image_prepare(const uint8_t *source, uint32_t length) {
  warden_image_clear();
  if (length < 52)
    return 0;
  uint32_t size = warden_read32(source), sections = warden_read32(source + 36);
  if (size < 40 || size > WARDEN_IMAGE_LIMIT || !sections || sections > 32 ||
      sections > (length - 40) / 12)
    return 0;
  warden_image_size = size;
  for (uint32_t i = 0; i < sections; i++) {
    const uint8_t *section = source + 40 + i * 12;
    uint32_t start = warden_read32(section), count = warden_read32(section + 4);
    if (start < 40 || !count || !warden_image_range(start, count))
      goto invalid;
    for (uint32_t j = 0; j < i; j++) {
      uint32_t other = warden_read32(source + 40 + j * 12);
      uint32_t other_size = warden_read32(source + 44 + j * 12);
      if (start < other + other_size && other < start + count)
        goto invalid;
    }
  }
  memcpy(warden_image, source, 40);
  uint32_t input = 40 + sections * 12, output = warden_read32(source + 40), copy = 1;
  while (output < size) {
    if (input > length || length - input < 2)
      goto invalid;
    uint32_t span = (uint32_t)source[input] | (uint32_t)source[input + 1] << 8;
    input += 2;
    if (!span || !warden_image_range(output, span))
      goto invalid;
    if (copy) {
      if (span > length - input)
        goto invalid;
      memcpy(warden_image + output, source + input, span);
      input += span;
    }
    output += span;
    copy ^= 1;
  }
  if (input != length)
    goto invalid;
  uint32_t reloc = warden_read32(source + 8), reloc_count = warden_read32(source + 12), target = 0;
  if (reloc_count > size / 2)
    goto invalid;
  for (uint32_t i = 0; i < reloc_count; i++) {
    if (!warden_image_range(reloc, 2))
      goto invalid;
    uint32_t first = warden_image[reloc];
    if (first & 0x80) {
      if (!warden_image_range(reloc, 4))
        goto invalid;
      target = (first & 0x7f) << 24 | (uint32_t)warden_image[reloc + 1] << 16 |
               (uint32_t)warden_image[reloc + 2] << 8 | warden_image[reloc + 3];
      reloc += 4;
    } else {
      uint32_t delta = first << 8 | warden_image[reloc + 1];
      if (delta > size || target > size - delta)
        goto invalid;
      target += delta;
      reloc += 2;
    }
    if (!warden_image_range(target, 4))
      goto invalid;
    uint32_t value = warden_read32(warden_image + target);
    if (!warden_image_range(value, 1))
      goto invalid;
    warden_image_write32(target, value + WARDEN_GUEST_BASE);
  }
  uint32_t exports = warden_read32(source + 16), export_count = warden_read32(source + 20);
  if (!export_count || export_count > 128 || !warden_image_range(exports, export_count * 4))
    goto invalid;
  for (uint32_t i = 0; i < export_count; i++) {
    uint32_t rva = warden_read32(warden_image + exports + i * 4);
    if (!warden_image_executable(source, sections, rva))
      goto invalid;
    if (i == 0)
      warden_entry = WARDEN_GUEST_BASE + rva;
  }
  uint32_t libraries = warden_read32(source + 28), library_count = warden_read32(source + 32);
  if (library_count > 32 || !warden_image_range(libraries, library_count * 8))
    goto invalid;
  for (uint32_t i = 0; i < library_count; i++) {
    uint32_t at = libraries + i * 8;
    uint32_t name = warden_read32(warden_image + at), slot = warden_read32(warden_image + at + 4);
    char library[64] = {0};
    if (!warden_image_string(name, library, sizeof(library)))
      goto invalid;
    for (;;) {
      if (!warden_image_range(slot, 4))
        goto invalid;
      uint32_t function = warden_read32(warden_image + slot);
      if (!function)
        break;
      if (warden_imports == WARDEN_IMPORT_LIMIT)
        goto invalid;
      WardenImport *record = &warden_import_table[warden_imports++];
      memcpy(record->library, library, sizeof(library));
      record->slot = WARDEN_GUEST_BASE + slot;
      if (function & 0x80000000)
        record->ordinal = function & 0x7fffffff;
      else if (!warden_image_string(function, record->name, sizeof(record->name)))
        goto invalid;
      slot += 4;
    }
  }
  warden_relocations = reloc_count;
  return 1;
invalid:
  warden_image_clear();
  return 0;
}

static void warden_image_run(void) {
  uint32_t types[WARDEN_IMPORT_LIMIT] = {0};
  for (uint32_t i = 0; i < warden_imports; i++) {
    types[i] = bw_warden_cpu_import_id(warden_import_table[i].library, warden_import_table[i].name);
    warden_image_write32(warden_import_table[i].slot - WARDEN_GUEST_BASE, 0x30000000u + i * 16);
  }
  warden_execution =
      bw_warden_cpu_run(warden_image, warden_image_size, warden_entry, types, warden_imports,
                        warden_unpacked + 40, warden_read32(warden_unpacked + 36));
  erase(types, sizeof(types));
}

static void warden_image_key(const uint8_t *session_key) {
  if (warden_execution != 1)
    return;
  uint32_t table = 0;
  warden_object = bw_warden_cpu_metric(2);
  if (!bw_warden_cpu_read32(warden_object, &table)) {
    warden_execution = 4;
    return;
  }
  for (uint32_t i = 0; i < 4; i++) {
    uint32_t address = 0;
    if (table > 0xffffffffu - i * 4 || !bw_warden_cpu_read32(table + i * 4, &address) ||
        address < WARDEN_GUEST_BASE ||
        !warden_image_executable(warden_unpacked, warden_read32(warden_unpacked + 36),
                                 address - WARDEN_GUEST_BASE)) {
      warden_execution = 4;
      return;
    }
    warden_methods[i] = address;
  }
  uint32_t pointer = bw_warden_cpu_scratch(session_key, 40, 0), arguments[2] = {pointer, 40};
  if (!pointer) {
    warden_execution = 4;
    return;
  }
  warden_execution = bw_warden_cpu_call(warden_methods[0], warden_object, arguments, 2);
  bw_warden_cpu_scratch_clear();
  warden_keyed = warden_execution == 1;
}
