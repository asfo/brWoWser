/* Included by srp.c: bounded Warden module acquisition, not module execution.
 * MD5 is the protocol's legacy transfer checksum, not a signature/trust check. */
#include "warden_memory.h"
#define WARDEN_MODULE_LIMIT 262144
static uint8_t warden_module[WARDEN_MODULE_LIMIT], warden_id[16], warden_module_key[16];
static uint32_t warden_size, warden_received, warden_stage, warden_unpacked_size, warden_init_mask;
static uint8_t warden_bindings[3][20];
static uint32_t warden_init_diagnostic[8];
static uint32_t warden_check_mask, warden_unknown_check, warden_missing_mask;
static uint32_t warden_memory_diagnostic[5], warden_memory_reads;
static uint32_t warden_check_diagnostic[4];
static uint32_t warden_reply_size, warden_native_diagnostic[7], warden_pending_checks;
static uint8_t warden_native_dependency[128];
static uint32_t warden_native_dependency_size, warden_page_diagnostic[3], warden_pc_diagnostic[2];
/* Numeric request positions only; never expose packet contents or cipher keys. */
static uint8_t warden_unpacked[WARDEN_MODULE_LIMIT];
#include "warden_inflate.c"
static HeaderCipher warden_send, warden_native_send, warden_native_receive;
_Static_assert(sizeof(HeaderCipher) == 258,
               "ARC4 handoff layout must match the wire-independent native state");

static uint32_t warden_read32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
#include "warden_signature.c"
#include "warden_image.c"

static void warden_md5(const uint8_t *data, uint32_t length, uint8_t out[16]) {
  static const uint32_t constants[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
      0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
      0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
      0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
      0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
      0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
      0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
      0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
      0xeb86d391};
  static const uint8_t shifts[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
  uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  uint32_t total = (length + 72) / 64 * 64;
  for (uint32_t offset = 0; offset < total; offset += 64) {
    uint32_t words[16] = {0};
    for (uint32_t j = 0; j < 64; j++) {
      uint32_t at = offset + j;
      uint8_t byte = at < length ? data[at] : at == length ? 0x80 : 0;
      if (at >= total - 8)
        byte = (uint8_t)((uint64_t)length * 8 >> (8 * (at - (total - 8))));
      words[j / 4] |= (uint32_t)byte << (8 * (j % 4));
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (uint32_t j = 0; j < 64; j++) {
      uint32_t f, index;
      if (j < 16) {
        f = (b & c) | (~b & d);
        index = j;
      } else if (j < 32) {
        f = (d & b) | (~d & c);
        index = (5 * j + 1) % 16;
      } else if (j < 48) {
        f = b ^ c ^ d;
        index = (3 * j + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        index = (7 * j) % 16;
      }
      uint32_t next = b + rol(a + f + constants[j] + words[index], shifts[(j / 16) * 4 + j % 4]);
      a = d;
      d = c;
      c = b;
      b = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    erase(words, sizeof(words));
  }
  for (uint32_t i = 0; i < 16; i++)
    out[i] = (uint8_t)(state[i / 4] >> (8 * (i % 4)));
  erase(state, sizeof(state));
}
static void warden_transfer_clear(void) {
  warden_image_clear();
  erase(warden_native_diagnostic, sizeof(warden_native_diagnostic));
  erase(warden_native_dependency, sizeof(warden_native_dependency));
  warden_native_dependency_size = 0;
  erase(warden_page_diagnostic, sizeof(warden_page_diagnostic));
  erase(warden_pc_diagnostic, sizeof(warden_pc_diagnostic));
  erase(warden_memory_diagnostic, sizeof(warden_memory_diagnostic));
  warden_memory_reads = warden_pending_checks = 0;
  erase(warden_module, sizeof(warden_module));
  erase(warden_unpacked, sizeof(warden_unpacked));
  erase(warden_id, sizeof(warden_id));
  erase(warden_bindings, sizeof(warden_bindings));
  erase(warden_init_diagnostic, sizeof(warden_init_diagnostic));
  warden_init_mask = warden_check_mask = warden_unknown_check = warden_missing_mask =
      warden_reply_size = 0;
  erase(warden_check_diagnostic, sizeof(warden_check_diagnostic));
  erase(warden_module_key, sizeof(warden_module_key));
  erase(&warden_send, sizeof(warden_send));
  erase(&warden_native_send, sizeof(warden_native_send));
  erase(&warden_native_receive, sizeof(warden_native_receive));
  warden_size = warden_received = warden_stage = warden_unpacked_size = 0;
}
/* The container carries length + zlib data + NGIS + 256 signature bytes.
 * Verify the original signing key and full signature block before unpacking. */
static uint32_t warden_unpack(void) {
  HeaderCipher cipher;
  uint8_t j = 0;
  for (uint32_t i = 0; i < 256; i++)
    cipher.s[i] = (uint8_t)i;
  for (uint32_t i = 0; i < 256; i++) {
    j = (uint8_t)(j + cipher.s[i] + warden_module_key[i % 16]);
    uint8_t t = cipher.s[i];
    cipher.s[i] = cipher.s[j];
    cipher.s[j] = t;
  }
  cipher.i = cipher.j = 0;
  for (uint32_t i = 0; i < warden_size; i++)
    warden_module[i] ^= cipher_byte(&cipher);
  erase(&cipher, sizeof(cipher));
  erase(warden_module_key, sizeof(warden_module_key));
  if (warden_size < 270 || memcmp(warden_module + warden_size - 260, "NGIS", 4))
    return 105;
  if (!warden_signature(warden_module, warden_size))
    return 107;
  uint32_t expected = warden_read32(warden_module);
  if (!expected || expected > WARDEN_MODULE_LIMIT ||
      warden_inflate(warden_module + 4, warden_size - 264, warden_unpacked, WARDEN_MODULE_LIMIT,
                     expected))
    return 106;
  warden_unpacked_size = expected;
  if (!warden_image_prepare(warden_unpacked, expected))
    return 108;
  warden_image_run();
  if (warden_execution == 2)
    warden_native_dependency_size =
        bw_warden_cpu_import_name(warden_native_dependency, sizeof(warden_native_dependency));
  warden_image_key(key);
  if (warden_keyed)
    warden_initial_ciphers(&warden_native_send, &warden_native_receive);
  return 3;
}
uint32_t bw_warden_progress(uint32_t field) {
  if (field == 46 || field == 47)
    return warden_pc_diagnostic[field - 46];
  if (field >= 42 && field < 45)
    return warden_page_diagnostic[field - 42];
  if (field == 41)
    return warden_memory_reads;
  if (field == 35)
    return warden_missing_mask;
  if (field >= 36 && field < 41)
    return warden_memory_diagnostic[field - 36];
  if (field >= 28 && field < 35)
    return warden_native_diagnostic[field - 28];
  if (field >= 24 && field < 28)
    return warden_check_diagnostic[field - 24];
  if (field >= 16 && field < 24)
    return warden_init_diagnostic[field - 16];
  switch (field) {
  case 0:
    return warden_received;
  case 1:
    return warden_size;
  case 2:
    return warden_stage;
  case 3:
    return warden_unpacked_size;
  case 4:
    return warden_image_size;
  case 5:
    return warden_relocations;
  case 6:
    return warden_imports;
  case 7:
    return warden_execution;
  case 8:
    return bw_warden_cpu_metric(0);
  case 9:
    return warden_keyed;
  case 10:
    return bw_warden_cpu_metric(4);
  case 11:
    return warden_init_mask;
  case 12:
    return warden_check_mask;
  case 13:
    return warden_unknown_check;
  case 14:
    return bw_warden_cpu_metric(5);
  case 15:
    return bw_warden_cpu_metric(6);
  default:
    return 0;
  }
}
/* Execute the signed module's hash handler. Its own packet streams start at
 * zero after GenerateRC4Keys, independent of the ongoing negotiation streams.
 * The response and replacement states come from the executed module. */
static uint32_t warden_hash_reply(void) {
  uint8_t request[17], zero[520] = {0}, reply[21], states[520];
  memcpy(request, io, sizeof(request));
  erase(io, sizeof(io));
  for (uint32_t i = 0; i < sizeof(request); i++)
    request[i] ^= cipher_byte(&warden_native_receive);
  uint32_t args[3] = {bw_warden_cpu_scratch(request, sizeof(request), 0), sizeof(request),
                      bw_warden_cpu_scratch(zero, 4, 128)};
  warden_execution = bw_warden_cpu_call(warden_methods[2], warden_object, args, 3);
  bw_warden_cpu_scratch_clear();
  erase(request, sizeof(request));
  if (warden_execution != 1 || bw_warden_cpu_output(reply, sizeof(reply)) != sizeof(reply))
    return 112;
  for (uint32_t i = 0; i < sizeof(reply); i++)
    reply[i] ^= cipher_byte(&warden_native_send);
  if (reply[0] != 4) {
    erase(reply, sizeof(reply));
    return 112;
  }
  if (!bw_warden_cpu_checkpoint()) {
    erase(reply, sizeof(reply));
    erase(states, sizeof(states));
    return 113;
  }
  uint32_t argument = bw_warden_cpu_scratch(zero, sizeof(zero), 0);
  warden_execution = bw_warden_cpu_call(warden_methods[1], warden_object, &argument, 1);
  bw_warden_cpu_scratch_clear();
  uint32_t size = bw_warden_cpu_cipher(states, sizeof(states));
  uint32_t execution = warden_execution, restored = bw_warden_cpu_restore_checkpoint();
  warden_execution = execution;
  if (!restored || execution != 1 || (size != 516 && size != 520)) {
    erase(reply, sizeof(reply));
    erase(states, sizeof(states));
    return 113;
  }
  for (uint32_t stream = 0; stream < 2; stream++) {
    uint8_t seen[256] = {0};
    for (uint32_t i = 0; i < 256; i++) {
      uint8_t value = states[stream * 258 + i];
      if (seen[value]) {
        erase(reply, sizeof(reply));
        erase(states, sizeof(states));
        return 113;
      }
      seen[value] = 1;
    }
  }
  for (uint32_t i = 0; i < sizeof(reply); i++)
    io[i] = reply[i] ^ cipher_byte(&warden_send);
  memcpy(&warden_send, states, 258);
  memcpy(&warden_receive, states + 258, 258);
  memcpy(&warden_native_send, states, 258);
  memcpy(&warden_native_receive, states + 258, 258);
  erase(reply, sizeof(reply));
  erase(states, sizeof(states));
  warden_stage = 3;
  return 5;
}
/* Capture only bytes actually emitted by the signed module. */
static uint32_t warden_native_output(uint32_t length) {
  uint8_t reply[4096];
  if (warden_execution == 2)
    warden_native_dependency_size =
        bw_warden_cpu_import_name(warden_native_dependency, sizeof(warden_native_dependency));
  warden_page_diagnostic[0] += bw_warden_cpu_metric(28) + bw_warden_cpu_metric(29);
  warden_page_diagnostic[1] += bw_warden_cpu_metric(30);
  warden_page_diagnostic[2] += bw_warden_cpu_metric(34);
  warden_pc_diagnostic[0] = bw_warden_cpu_metric(11);
  warden_pc_diagnostic[1] = bw_warden_cpu_metric(12);
  warden_native_diagnostic[0] = warden_execution;
  warden_native_diagnostic[1] = bw_warden_cpu_metric(1);
  warden_native_diagnostic[2] = bw_warden_cpu_metric(0);
  warden_native_diagnostic[3] = bw_warden_cpu_metric(5);
  warden_native_diagnostic[4] = bw_warden_cpu_metric(6);
  warden_native_diagnostic[5] = bw_warden_cpu_metric(3);
  warden_native_diagnostic[6] = length;
  erase(io, sizeof(io));
  if (warden_execution != 1 || warden_native_diagnostic[5] > sizeof(reply)) {
    if (warden_execution == 4)
      for (uint32_t i = 0; i < 4; i++)
        warden_memory_diagnostic[i] = bw_warden_cpu_memory_fault(i);
    return 119;
  }
  uint32_t size = bw_warden_cpu_output(reply, sizeof(reply));
  for (uint32_t i = 0; i < size; i++) {
    uint8_t plaintext = reply[i] ^ cipher_byte(&warden_native_send);
    io[i] = plaintext ^ cipher_byte(&warden_send);
  }
  erase(reply, sizeof(reply));
  warden_reply_size = size;
  return size ? 7 : 6;
}
/* Forward validated plaintext to the module's real receive method (+8).
 * Processing may be deferred until its update method (+12) runs. */
static uint32_t warden_native_request(uint32_t length) {
  uint8_t request[4096], zero[4] = {0};
  warden_reply_size = 0;
  erase(warden_native_diagnostic, sizeof(warden_native_diagnostic));
  erase(warden_page_diagnostic, sizeof(warden_page_diagnostic));
  if (!length || length > sizeof(request) - sizeof(zero) || warden_execution != 1)
    return 119;
  for (uint32_t i = 0; i < length; i++)
    request[i] = io[i] ^ cipher_byte(&warden_native_receive);
  uint32_t args[3] = {bw_warden_cpu_scratch(request, length, 0), length,
                      bw_warden_cpu_scratch(zero, sizeof(zero), sizeof(request) - sizeof(zero))};
  warden_execution = bw_warden_cpu_call(warden_methods[2], warden_object, args, 3);
  bw_warden_cpu_scratch_clear();
  erase(request, sizeof(request));
  return warden_native_output(length);
}
static uint32_t warden_check_output(uint32_t result) {
  if (result == 7) {
    warden_pending_checks = 0;
    if (warden_audit_mode) {
      erase(io, sizeof(io));
      warden_reply_size = 0;
      return 120;
    }
    return 7;
  }
  if (result == 6)
    return warden_audit_mode ? 121 : 8;
  warden_pending_checks = 0;
  return result;
}
uint32_t bw_warden_tick(uint32_t elapsed_ms) {
  if (warden_stage != 3 || !warden_pending_checks)
    return 0;
  warden_reply_size = 0;
  if (elapsed_ms > 60000)
    return 119;
  warden_execution = bw_warden_cpu_call(warden_methods[3], warden_object, &elapsed_ms, 1);
  return warden_check_output(warden_native_output(0));
}
uint32_t bw_warden_reply_length(void) {
  return warden_reply_size;
}
/* Register bounded native callback descriptors as data. Callback execution is
 * dispatched separately by check type; these addresses are never host pointers.
 * Initialization carries no positive check result or client success response. */
static uint32_t warden_initialize(uint32_t length) {
  if (length > 4096)
    return 114;
  uint32_t at = 0, records = 0;
  while (at < length) {
    if (++records > 16 || length - at < 7 || io[at] != 3)
      return 114;
    uint32_t size = (uint32_t)io[at + 1] | (uint32_t)io[at + 2] << 8;
    if ((size != 8 && size != 20) || size > length - at - 7)
      return 114;
    uint8_t digest[20];
    hash(io + at + 7, size, digest);
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < 5; i++)
      checksum ^= warden_read32(digest + i * 4);
    erase(digest, sizeof(digest));
    if (checksum != warden_read32(io + at + 3))
      return 114;
    const uint8_t *body = io + at + 7;
    warden_init_diagnostic[0] = records;
    warden_init_diagnostic[1] = size;
    warden_init_diagnostic[2] = body[0];
    warden_init_diagnostic[3] = body[1];
    warden_init_diagnostic[4] = body[2];
    warden_init_diagnostic[5] = body[size - 1];
    warden_init_diagnostic[6] = 1;
    warden_init_diagnostic[7] = 0;
    uint32_t binding;
    /* Both observed file-callback variants keep their original selector in the
     * descriptor. Registration does not execute any supplied address. */
    if (size == 20 && body[0] == 1 && !body[1] && (body[2] == 1 || body[2] == 2) && !body[3])
      binding = 0;
    else if (size == 8 && body[0] == 4 && !body[1] && !body[2] && body[7] == 1)
      binding = 1;
    else if (size == 8 && body[0] == 1 && body[1] == 1 && !body[2] && body[7] == 1)
      binding = 2;
    else
      return 115;
    /* Re-registering a callback replaces its previous descriptor. */
    uint32_t offset = size == 20 ? 4 : 3, count = size == 20 ? 4 : 1;
    for (uint32_t i = 0; i < count; i++) {
      uint32_t address = warden_read32(body + offset + i * 4);
      /* Callback addresses can belong to a DLL outside the executable image.
       * They remain registration data and are never host function pointers. */
      if (!address) {
        warden_init_diagnostic[6] = 3;
        warden_init_diagnostic[7] = i;
        return 115;
      }
    }
    if (binding == 1 || binding == 2)
      bw_warden_cpu_bind_client(binding, warden_read32(body + 3));
    memcpy(warden_bindings[binding], body, size);
    warden_init_mask |= 1u << binding;
    at += 7 + size;
  }
  return records ? 6 : 114;
}
#include "warden_checks.c"

/* Returns 1: encrypted MODULE_MISSING in io[0]; 2: pending transfer;
 * 3: checksum verified and container unpacked; signature verified; native loader unavailable; >=100: error.
 * Never returns MODULE_OK. Loopback check replies are built in warden_checks.c. */
uint32_t bw_warden_receive(uint32_t length) {
  uint32_t result = 100;
  if (!world_initialized || !length || length > sizeof(io)) {
    erase(io, sizeof(io));
    return result;
  }
  for (uint32_t i = 0; i < length; i++)
    io[i] ^= cipher_byte(&warden_receive);
  if (io[0] == 5 && length == 17 && warden_stage == 2 && warden_keyed) {
    result = warden_hash_reply();
    if (result == 5)
      return result;
    erase(io, sizeof(io));
    warden_transfer_clear();
    warden_stage = 4;
    return result;
  }
  if (warden_stage == 3) {
    if (io[0] == 3) {
      result = warden_initialize(length);
      if (result == 6)
        result = warden_native_request(length);
    } else
      result = io[0] == 2 || io[0] == 4 ? warden_checks(length) : 111;
    if (result == 7)
      return result;
    erase(io, sizeof(io));
    if (result == 6 || result == 8 || result == 121)
      return result;
    uint32_t checks = warden_check_mask, unknown = warden_unknown_check,
             missing = warden_missing_mask, memory_reads = warden_memory_reads;
    uint32_t memory_detail[5];
    memcpy(memory_detail, warden_memory_diagnostic, sizeof(memory_detail));
    uint32_t pc_detail[2];
    memcpy(pc_detail, warden_pc_diagnostic, sizeof(pc_detail));
    uint32_t page_detail[3];
    memcpy(page_detail, warden_page_diagnostic, sizeof(page_detail));
    uint8_t native_dependency[128];
    memcpy(native_dependency, warden_native_dependency, sizeof(native_dependency));
    uint32_t dependency_size = warden_native_dependency_size;
    uint32_t native_detail[7];
    memcpy(native_detail, warden_native_diagnostic, sizeof(native_detail));
    uint32_t check_detail[4];
    memcpy(check_detail, warden_check_diagnostic, sizeof(check_detail));
    uint32_t initialization[8];
    memcpy(initialization, warden_init_diagnostic, sizeof(initialization));
    warden_transfer_clear();
    memcpy(warden_pc_diagnostic, pc_detail, sizeof(pc_detail));
    erase(pc_detail, sizeof(pc_detail));
    memcpy(warden_page_diagnostic, page_detail, sizeof(page_detail));
    erase(page_detail, sizeof(page_detail));
    memcpy(warden_native_dependency, native_dependency, sizeof(native_dependency));
    erase(native_dependency, sizeof(native_dependency));
    warden_native_dependency_size = dependency_size;
    memcpy(warden_memory_diagnostic, memory_detail, sizeof(memory_detail));
    erase(memory_detail, sizeof(memory_detail));
    warden_missing_mask = missing;
    warden_memory_reads = memory_reads;
    memcpy(warden_native_diagnostic, native_detail, sizeof(native_detail));
    erase(native_detail, sizeof(native_detail));
    memcpy(warden_check_diagnostic, check_detail, sizeof(check_detail));
    erase(check_detail, sizeof(check_detail));
    memcpy(warden_init_diagnostic, initialization, sizeof(initialization));
    erase(initialization, sizeof(initialization));
    warden_check_mask = checks;
    warden_unknown_check = unknown;
    warden_stage = 4;
    return result;
  }
  if (io[0] == 0 && length == 37 && warden_stage == 0) {
    uint32_t size = warden_read32(io + 33);
    if (size && size <= WARDEN_MODULE_LIMIT) {
      memcpy(warden_id, io + 1, 16);
      memcpy(warden_module_key, io + 17, 16);
      warden_size = size;
      warden_stage = 1;
      result = 1;
    } else
      result = 101;
  } else if (io[0] == 1 && length >= 3 && warden_stage == 1) {
    uint32_t count = (uint32_t)io[1] | (uint32_t)io[2] << 8;
    if (count && count <= 500 && length == count + 3 && count <= warden_size - warden_received) {
      memcpy(warden_module + warden_received, io + 3, count);
      warden_received += count;
      result = 2;
      if (warden_received == warden_size) {
        uint8_t digest[16];
        warden_md5(warden_module, warden_size, digest);
        uint8_t mismatch = 0;
        for (uint32_t i = 0; i < 16; i++)
          mismatch |= digest[i] ^ warden_id[i];
        erase(digest, sizeof(digest));
        if (mismatch)
          result = 102;
        else {
          result = warden_unpack();
          if (result == 3) {
            warden_stage = 2;
            if (warden_keyed)
              result = 4;
          }
        }
      }
    } else
      result = 103;
  } else
    result = 104;
  erase(io, sizeof(io));
  if (result == 1)
    io[0] = cipher_byte(&warden_send); /* MODULE_MISSING = 0 */
  if (result == 4)
    io[0] = 1 ^ cipher_byte(&warden_send);
  if (result >= 100) {
    warden_transfer_clear();
    warden_stage = 4;
  }
  return result;
}

/* Diagnostic dependency label only; no code, session material or guest memory. */
uint32_t bw_warden_dependency(void) {
  if (warden_native_dependency_size) {
    erase(io, sizeof(io));
    memcpy(io, warden_native_dependency, warden_native_dependency_size);
    return warden_native_dependency_size;
  }
  static const char *services[7] = {"SendPacket", "CheckModule", "LoadModule", "AllocateMemory",
                                    "FreeMemory", "SetRC4Data",  "GetRC4Data"};
  uint32_t index = bw_warden_cpu_metric(1);
  erase(io, sizeof(io));
  if (warden_execution == 2 && index < warden_imports) {
    WardenImport *record = &warden_import_table[index];
    uint32_t length = (uint32_t)strlen(record->library);
    memcpy(io, record->library, length);
    io[length++] = '!';
    if (!record->name[0])
      return 0;
    uint32_t size = (uint32_t)strlen(record->name);
    memcpy(io + length, record->name, size);
    return length + size;
  }
  if (warden_execution == 3 && index < 7) {
    uint32_t size = (uint32_t)strlen(services[index]);
    memcpy(io, services[index], size);
    return size;
  }
  return 0;
}
