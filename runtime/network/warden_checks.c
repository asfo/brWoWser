/* Bounded request decoding. A loopback session may answer a batch the guest
 * cannot measure natively: memory bytes come from the mapped image, and page,
 * module and driver checks use the pass byte current AzerothCore accepts.
 * Archive replies are limited to stock 3.3.5a digests. Other sessions still
 * send nothing for a batch with a missing provider. */
__attribute__((import_module("env"), import_name("bw_clock_monotonic_ms"))) extern double
bw_clock_monotonic_ms(void);
extern uint32_t bw_warden_cpu_memory_copy(uint32_t address, uint8_t *buffer, uint32_t length);

struct WardenFileDigest {
  const char *path;
  uint8_t hash[20];
};
static const struct WardenFileDigest warden_file_digests[] = {
    {"world\\lordaeron\\stratholme\\activedoodads\\doors\\nox_door_plague.m2",
     {0xb4, 0x45, 0x2b, 0x6d, 0x95, 0xc9, 0x8b, 0x18, 0x6a, 0x70,
      0xb0, 0x08, 0xfa, 0x07, 0xbb, 0xae, 0xf3, 0x0d, 0xf7, 0xa2}},
    {"world\\kalimdor\\onyxiaslair\\doors\\onyxiasgate01.m2",
     {0x75, 0x19, 0x5e, 0x4a, 0xed, 0xa0, 0xbc, 0xaf, 0x04, 0x8c,
      0xa0, 0xe3, 0x4d, 0x95, 0xa7, 0x0d, 0x4f, 0x53, 0xc7, 0x46}},
    {"world\\generic\\human\\activedoodads\\doors\\deadminedoor02.m2",
     {0x3d, 0xff, 0x01, 0x1b, 0x9a, 0xb1, 0x34, 0xf3, 0x7f, 0x88,
      0x50, 0x97, 0xe6, 0x95, 0x35, 0x1b, 0x91, 0x95, 0x35, 0x64}},
    {"world\\kalimdor\\silithus\\activedoodads\\ahnqirajdoor\\ahnqirajdoor02.m2",
     {0xdb, 0xd4, 0xf4, 0x07, 0xc4, 0x68, 0xcc, 0x36, 0x13, 0x4e,
      0x62, 0x1d, 0x16, 0x01, 0x78, 0xfd, 0xa4, 0xd0, 0xd2, 0x49}},
    {"world\\kalimdor\\diremaul\\activedoodads\\doors\\diremaulsmallinstancedoor.m2",
     {0x0d, 0xc8, 0xdb, 0x46, 0xc8, 0x55, 0x49, 0xc0, 0xff, 0x1a,
      0x60, 0x0f, 0x6c, 0x23, 0x63, 0x57, 0xc3, 0x05, 0x78, 0x1a}},
};
static int warden_path_eq(const uint8_t *text, uint32_t length, const char *path) {
  uint32_t i = 0;
  for (; i < length && path[i]; i++) {
    uint8_t left = text[i] == '/' ? '\\' : text[i];
    uint8_t right = path[i] == '/' ? '\\' : (uint8_t)path[i];
    if (left >= 'A' && left <= 'Z')
      left = (uint8_t)(left + 32);
    if (right >= 'A' && right <= 'Z')
      right = (uint8_t)(right + 32);
    if (left != right)
      return 0;
  }
  return i == length && !path[i];
}
static const uint8_t *warden_file_digest(const uint8_t *text, uint32_t length) {
  for (uint32_t i = 0; i < sizeof(warden_file_digests) / sizeof(warden_file_digests[0]); i++)
    if (warden_path_eq(text, length, warden_file_digests[i].path))
      return warden_file_digests[i].hash;
  return 0;
}
static uint32_t warden_local_fail(uint8_t *request, uint8_t *body, uint8_t *scratch) {
  erase(request, 4096);
  erase(body, 4096);
  erase(scratch, 255);
  return 110;
}
/* Plaintext result, then the transport cipher. The send cipher advances only
 * after every result in the batch has been built. */
static uint32_t warden_local_reply(uint32_t length) {
  uint8_t request[4096], body[4096], scratch[255];
  if (length > sizeof(request))
    return 110;
  memcpy(request, io, length);
  uint32_t at = 1, strings = 0, offsets[64], sizes[64];
  while (at < length - 1 && request[at]) {
    uint32_t size = request[at++];
    if (strings >= 64 || size > length - 1 - at)
      return warden_local_fail(request, body, scratch);
    offsets[strings] = at;
    sizes[strings++] = size;
    at += size;
  }
  if (at >= length - 1 || request[at++])
    return warden_local_fail(request, body, scratch);
  uint8_t mask = request[length - 1];
  uint32_t n = 0;
  while (at < length - 1) {
    uint32_t type = request[at++] ^ mask;
    if (type == 0x57) {
      double clock = bw_clock_monotonic_ms();
      if (!(clock >= 0) || clock > 9007199254740991.0 || n + 5 > sizeof(body))
        return warden_local_fail(request, body, scratch);
      uint32_t ticks = (uint32_t)(uint64_t)clock;
      body[n++] = 1;
      for (uint32_t i = 0; i < 4; i++)
        body[n++] = (uint8_t)(ticks >> (8 * i));
    } else if (type == 0xf3) {
      uint32_t address = warden_read32(request + at + 1), span = request[at + 5];
      if (request[at] || !span || span > sizeof(scratch) ||
          bw_warden_cpu_memory_copy(address, scratch, span) || n + 1 + span > sizeof(body))
        return warden_local_fail(request, body, scratch);
      body[n++] = 0;
      memcpy(body + n, scratch, span);
      n += span;
      erase(scratch, span);
      at += 6;
    } else if (type == 0xb2 || type == 0xbf || type == 0x71 || type == 0xd9) {
      if (n >= sizeof(body))
        return warden_local_fail(request, body, scratch);
      body[n++] = 0xe9;
      at += type == 0x71 ? 25 : type == 0xd9 ? 24 : 29;
    } else if (type == 0x8b) {
      if (n >= sizeof(body))
        return warden_local_fail(request, body, scratch);
      body[n++] = 1;
      at += 1;
    } else if (type == 0x98) {
      uint32_t index = request[at++];
      const uint8_t *digest =
          index && index <= strings
              ? warden_file_digest(request + offsets[index - 1], sizes[index - 1])
              : 0;
      if (!digest || n + 21 > sizeof(body))
        return warden_local_fail(request, body, scratch);
      body[n++] = 0;
      memcpy(body + n, digest, 20);
      n += 20;
    } else
      return warden_local_fail(request, body, scratch);
  }
  if (!n || n > 4089)
    return warden_local_fail(request, body, scratch);
  uint8_t digest[20];
  hash(body, n, digest);
  uint32_t checksum = 0;
  for (uint32_t i = 0; i < 5; i++)
    checksum ^= warden_read32(digest + i * 4);
  erase(digest, sizeof(digest));
  uint8_t packet[4096];
  uint32_t total = 7 + n;
  packet[0] = 2;
  packet[1] = (uint8_t)n;
  packet[2] = (uint8_t)(n >> 8);
  for (uint32_t i = 0; i < 4; i++)
    packet[3 + i] = (uint8_t)(checksum >> (8 * i));
  memcpy(packet + 7, body, n);
  erase(io, sizeof(io));
  for (uint32_t i = 0; i < total; i++)
    io[i] = packet[i] ^ cipher_byte(&warden_send);
  erase(packet, sizeof(packet));
  warden_reply_size = total;
  warden_pending_checks = 0;
  erase(request, sizeof(request));
  erase(body, sizeof(body));
  erase(scratch, sizeof(scratch));
  return 7;
}
static uint32_t warden_check_invalid(uint32_t length, uint32_t strings, uint32_t reason,
                                     uint32_t offset) {
  warden_check_diagnostic[0] = length;
  warden_check_diagnostic[1] = strings;
  warden_check_diagnostic[2] = reason;
  warden_check_diagnostic[3] = offset;
  return 116;
}
static uint32_t warden_checks(uint32_t length) {
  warden_check_mask = warden_unknown_check = warden_missing_mask = warden_memory_reads = 0;
  erase(warden_memory_diagnostic, sizeof(warden_memory_diagnostic));
  erase(warden_check_diagnostic, sizeof(warden_check_diagnostic));
  if (io[0] == 4) {
    warden_check_mask = warden_missing_mask = 1u << 9;
    return 110;
  }
  if (length < 4 || length > 4096)
    return warden_check_invalid(length, 0, 1, 0);
  uint32_t at = 1, strings = 0;
  while (at < length - 1 && io[at]) {
    uint32_t size = io[at++];
    if (++strings > 64 || size > length - 1 - at)
      return warden_check_invalid(length, strings, 2, at - 1);
    at += size;
  }
  if (at >= length - 1 || io[at++])
    return warden_check_invalid(length, strings, 3, at);
  uint8_t mask = io[length - 1];
  uint32_t checks = 0;
  while (at < length - 1) {
    if (++checks > 128)
      return warden_check_invalid(length, strings, 4, at);
    uint32_t type = io[at++] ^ mask, size = 0, bit = 0, index_offset = 0;
    switch (type) {
    case 0x57:
      bit = 0;
      break;
    case 0xf3:
      bit = 1;
      size = 6;
      break;
    case 0xb2:
    case 0xbf:
      bit = 2;
      size = 29;
      break;
    case 0x98:
      bit = 3;
      size = 1;
      index_offset = 1;
      break;
    case 0x8b:
      bit = 4;
      size = 1;
      index_offset = 1;
      break;
    case 0x71:
      bit = 5;
      size = 25;
      index_offset = 25;
      break;
    case 0xd9:
      bit = 6;
      size = 24;
      break;
    case 0x7e:
      bit = 7;
      size = 31;
      break;
    default:
      warden_unknown_check = type;
      warden_check_diagnostic[0] = length;
      warden_check_diagnostic[1] = strings;
      warden_check_diagnostic[2] = checks;
      warden_check_diagnostic[3] = at - 1;
      warden_check_mask |= 1u << 8;
      warden_missing_mask |= 1u << 8;
      return 117;
    }
    if (size > length - 1 - at)
      return warden_check_invalid(length, strings, 5, at - 1);
    if (index_offset && (!io[at + index_offset - 1] || io[at + index_offset - 1] > strings))
      return warden_check_invalid(length, strings, 6, at + index_offset - 1);
    if (type == 0xf3 && (io[at] > strings || !io[at + 5]))
      return warden_check_invalid(length, strings, 7, at);
    if (bit == 2 && (!io[at + 28] ||
                     (uint64_t)warden_read32(io + at + 24) + io[at + 28] > UINT64_C(0x100000000)))
      return warden_check_invalid(length, strings, 9, at);
    if (bit == 1) {
      uint32_t address = warden_read32(io + at + 1), span = io[at + 5];
      /* Zero selects the current process. Named-module resolution is separate
       * and must not be replaced with a read from an unrelated image. */
      uint32_t status = io[at] ? BW_MEMORY_UNMAPPED : bw_warden_cpu_memory_probe(address, span);
      if (!status)
        warden_memory_reads++;
      else {
        warden_missing_mask |= 1u << 1;
        if (!warden_memory_diagnostic[3]) {
          warden_memory_diagnostic[0] = address;
          warden_memory_diagnostic[1] = span;
          warden_memory_diagnostic[2] = BW_MEMORY_READ;
          warden_memory_diagnostic[3] = status;
          warden_memory_diagnostic[4] = io[at];
        }
      }
    } else if (bit != 0 && bit != 2 && bit != 4)
      warden_missing_mask |= 1u << bit;
    warden_check_mask |= 1u << bit;
    at += size;
  }
  if (at != length - 1 || !checks)
    return warden_check_invalid(length, strings, 8, at);
  /* Page and Lua checks are not a missing-provider signal, so a loopback
   * batch of only those types was still dispatched to the signed module.
   * Its update then read unmapped guest memory and sent nothing. Answer
   * every valid loopback batch here instead of entering that interpreter. */
  if (warden_local_mode && !warden_audit_mode)
    return warden_local_reply(length);
  if (warden_missing_mask && !warden_audit_mode)
    return 110;
  warden_pending_checks = 1;
  return warden_check_output(warden_native_request(length));
}
