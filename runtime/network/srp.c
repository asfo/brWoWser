/* Bounded 3.3.5a SRP client. Wire integers are little endian; SHA-1 output is bytes.
 * Protocol reference: TrinityCore 3.3.5 SRP6 (see README), independently implemented.
 * Randomness is supplied by the host CSPRNG. No password or session key is logged. */
#include <stdint.h>
#include <string.h>
#include "warden_client_image.h"
typedef struct {
  uint32_t v[8];
} Integer;
static uint8_t io[65536], identity[20], credential[20], exponent[32], public_a[32], proof_m[20],
    key[40];
static uint32_t phase, warden_audit_mode, warden_local_mode;
static uint8_t account_name[17];
static uint32_t account_length;
void bw_world_clear(void);
static void clear_characters(void);
static const uint8_t modulus_bytes[32] = {
    0xb7, 0x9b, 0x3e, 0x2a, 0x87, 0x82, 0x3c, 0xab, 0x8f, 0x5e, 0xbf, 0xbf, 0x8e, 0xb1, 0x01, 0x08,
    0x53, 0x50, 0x06, 0x29, 0x8b, 0x5b, 0xad, 0xbd, 0x5b, 0x53, 0xe1, 0x89, 0x5e, 0x64, 0x4b, 0x89};
static void erase(void *p, uint32_t n) {
  volatile uint8_t *b = p;
  while (n--)
    *b++ = 0;
}
static uint32_t rol(uint32_t x, uint32_t n) {
  return (x << n) | (x >> (32 - n));
}
static void hash(const uint8_t *data, uint32_t length, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
  uint32_t total = ((length + 9 + 63) / 64) * 64;
  for (uint32_t offset = 0; offset < total; offset += 64) {
    uint32_t w[80] = {0};
    for (uint32_t j = 0; j < 64; j++) {
      uint32_t at = offset + j;
      uint8_t value = at < length ? data[at] : (at == length ? 128 : 0);
      if (at >= total - 4)
        value = (uint8_t)((length * 8) >> (8 * (total - 1 - at)));
      w[j / 4] |= (uint32_t)value << (24 - 8 * (j % 4));
    }
    for (uint32_t j = 16; j < 80; j++)
      w[j] = rol(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (uint32_t j = 0; j < 80; j++) {
      uint32_t f, k;
      if (j < 20) {
        f = (b & c) | (~b & d);
        k = 0x5a827999;
      } else if (j < 40) {
        f = b ^ c ^ d;
        k = 0x6ed9eba1;
      } else if (j < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8f1bbcdc;
      } else {
        f = b ^ c ^ d;
        k = 0xca62c1d6;
      }
      uint32_t t = rol(a, 5) + f + e + k + w[j];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    erase(w, sizeof(w));
  }
  for (uint32_t j = 0; j < 20; j++)
    out[j] = (uint8_t)(h[j / 4] >> (24 - 8 * (j % 4)));
  erase(h, sizeof(h));
}
static Integer from(const uint8_t *bytes, uint32_t n) {
  Integer r = {{0}};
  for (uint32_t i = 0; i < n; i++)
    r.v[i / 4] |= (uint32_t)bytes[i] << (8 * (i % 4));
  return r;
}
static void to(Integer a, uint8_t *out) {
  for (uint32_t i = 0; i < 32; i++)
    out[i] = (uint8_t)(a.v[i / 4] >> (8 * (i % 4)));
}
static int compare(Integer a, Integer b) {
  for (int i = 7; i >= 0; i--)
    if (a.v[i] != b.v[i])
      return a.v[i] > b.v[i] ? 1 : -1;
  return 0;
}
static Integer subtract(Integer a, Integer b) {
  uint64_t borrow = 0;
  for (uint32_t i = 0; i < 8; i++) {
    uint64_t t = (uint64_t)b.v[i] + borrow;
    uint32_t old = a.v[i];
    a.v[i] = (uint32_t)((uint64_t)old - t);
    borrow = (uint64_t)old < t;
  }
  return a;
}
static Integer addmod(Integer a, Integer b, Integer n) {
  uint64_t carry = 0;
  for (uint32_t i = 0; i < 8; i++) {
    uint64_t t = (uint64_t)a.v[i] + b.v[i] + carry;
    a.v[i] = (uint32_t)t;
    carry = t >> 32;
  }
  if (carry || compare(a, n) >= 0)
    a = subtract(a, n);
  return a;
}
static Integer multiply(Integer a, Integer b, Integer n) {
  Integer r = {{0}};
  for (uint32_t i = 0; i < 256; i++) {
    if ((b.v[i / 32] >> (i % 32)) & 1)
      r = addmod(r, a, n);
    a = addmod(a, a, n);
  }
  return r;
}
static Integer power(Integer a, const uint8_t *e, uint32_t len, Integer n) {
  Integer r = {{1}};
  for (uint32_t i = 0; i < len * 8; i++) {
    if ((e[i / 8] >> (i % 8)) & 1)
      r = multiply(r, a, n);
    a = multiply(a, a, n);
  }
  return r;
}
uint8_t *bw_auth_io(void) {
  return io;
}
void bw_auth_clear(void) {
  bw_world_clear();
  bw_client_image_clear();
  erase(account_name, sizeof(account_name));
  account_length = 0;
  erase(io, sizeof(io));
  erase(identity, sizeof(identity));
  erase(credential, sizeof(credential));
  erase(exponent, sizeof(exponent));
  erase(public_a, sizeof(public_a));
  erase(proof_m, sizeof(proof_m));
  erase(key, sizeof(key));
  phase = warden_audit_mode = warden_local_mode = 0;
}
/* io: username || password || 32 random bytes. ASCII-only initial implementation. */
uint32_t bw_auth_credentials(uint32_t u, uint32_t p) {
  if (!u || u > 16 || !p || p > 16 || u + p + 32 > sizeof(io))
    return 1;
  uint8_t joined[33];
  for (uint32_t i = 0; i < u + p; i++) {
    uint8_t c = io[i];
    if (c < 32 || c > 126) {
      bw_auth_clear();
      return 2;
    }
    io[i] = (c >= 'a' && c <= 'z') ? c - 32 : c;
  }
  memcpy(account_name, io, u);
  account_length = u;
  hash(io, u, identity);
  memcpy(joined, io, u);
  joined[u] = ':';
  memcpy(joined + u + 1, io + u, p);
  hash(joined, u + p + 1, credential);
  memcpy(exponent, io + u + p, 32);
  erase(joined, sizeof(joined));
  erase(io, sizeof(io));
  uint8_t nonzero = 0;
  for (uint32_t i = 0; i < 32; i++)
    nonzero |= exponent[i];
  if (!nonzero) {
    bw_auth_clear();
    return 3;
  }
  phase = 1;
  return 0;
}
/* io: complete server challenge; success replaces it with the 75-byte proof. */
uint32_t bw_auth_challenge(uint32_t length) {
  if (phase != 1 || length < 119 || io[0] || io[1] || io[2] || io[35] != 1 || io[36] != 7 ||
      io[37] != 32 || memcmp(io + 38, modulus_bytes, 32))
    return 1;
  if (io[118])
    return 2; /* PIN/matrix/token require an explicit UI implementation. */
  Integer n = from(modulus_bytes, 32), b = from(io + 3, 32), g = {{7}}, zero = {{0}};
  if (compare(b, zero) == 0 || compare(b, n) >= 0)
    return 3;
  Integer a = power(g, exponent, 32, n);
  to(a, public_a);
  uint8_t temp[192], digest[20], xbytes[20], exp[53] = {0}, secret[32], half[16], h0[20], h1[20];
  memcpy(temp, io + 70, 32);
  memcpy(temp + 32, credential, 20);
  hash(temp, 52, xbytes);
  memcpy(temp, public_a, 32);
  memcpy(temp + 32, io + 3, 32);
  hash(temp, 64, digest);
  /* a + u*x is at most 321 bits; use base-256 multiplication without truncation. */
  for (uint32_t i = 0; i < 20; i++) {
    uint32_t carry = 0;
    for (uint32_t j = 0; j < 20; j++) {
      uint32_t t = exp[i + j] + digest[i] * xbytes[j] + carry;
      exp[i + j] = (uint8_t)t;
      carry = t >> 8;
    }
    uint32_t at = i + 20;
    while (carry) {
      uint32_t t = exp[at] + carry;
      exp[at++] = (uint8_t)t;
      carry = t >> 8;
    }
  }
  uint32_t carry = 0;
  for (uint32_t i = 0; i < sizeof(exp); i++) {
    uint32_t t = exp[i] + (i < 32 ? exponent[i] : 0) + carry;
    exp[i] = (uint8_t)t;
    carry = t >> 8;
  }
  Integer v = power(g, xbytes, 20, n), three = addmod(addmod(v, v, n), v, n);
  Integer base = compare(b, three) >= 0 ? subtract(b, three) : subtract(n, subtract(three, b));
  if (compare(base, zero) == 0)
    return 3;
  to(power(base, exp, sizeof(exp), n), secret);
  uint32_t skip = 0;
  while (skip < 32 && !secret[skip])
    skip++;
  if (skip & 1)
    skip++;
  for (uint32_t i = 0; i < 16; i++)
    half[i] = secret[i * 2];
  hash(half + skip / 2, 16 - skip / 2, h0);
  for (uint32_t i = 0; i < 16; i++)
    half[i] = secret[i * 2 + 1];
  hash(half + skip / 2, 16 - skip / 2, h1);
  for (uint32_t i = 0; i < 20; i++) {
    key[i * 2] = h0[i];
    key[i * 2 + 1] = h1[i];
  }
  hash(modulus_bytes, 32, h0);
  uint8_t generator = 7;
  hash(&generator, 1, h1);
  for (uint32_t i = 0; i < 20; i++)
    temp[i] = h0[i] ^ h1[i];
  memcpy(temp + 20, identity, 20);
  memcpy(temp + 40, io + 70, 32);
  memcpy(temp + 72, public_a, 32);
  memcpy(temp + 104, io + 3, 32);
  memcpy(temp + 136, key, 40);
  hash(temp, 176, proof_m);
  erase(io, sizeof(io));
  io[0] = 1;
  memcpy(io + 1, public_a, 32);
  memcpy(io + 33, proof_m,
         20); /* CRC proof remains zero: strict client-file verification is not implemented. */
  erase(temp, sizeof(temp));
  erase(digest, sizeof(digest));
  erase(xbytes, sizeof(xbytes));
  erase(exp, sizeof(exp));
  erase(secret, sizeof(secret));
  erase(half, sizeof(half));
  erase(h0, sizeof(h0));
  erase(h1, sizeof(h1));
  erase(credential, sizeof(credential));
  erase(exponent, sizeof(exponent));
  phase = 2;
  return 0;
}
uint32_t bw_auth_verify(uint32_t length) {
  if (phase != 2 || length < 2 || io[0] != 1)
    return 1;
  if (io[1])
    return 100 + io[1];
  if (length != 32)
    return 1;
  uint8_t material[92], expected[20];
  memcpy(material, public_a, 32);
  memcpy(material + 32, proof_m, 20);
  memcpy(material + 52, key, 40);
  hash(material, 92, expected);
  uint8_t different = 0;
  for (uint32_t i = 0; i < 20; i++)
    different |= expected[i] ^ io[i + 2];
  erase(material, sizeof(material));
  erase(expected, sizeof(expected));
  if (different) {
    bw_auth_clear();
    return 2;
  }
  erase(io, sizeof(io));
  phase = 3;
  return 0;
}

/* World packet authentication and ARC4-drop1024 header streams. Only headers are
 * encrypted; payload framing remains the host transport's responsibility. */
typedef struct {
  uint8_t s[256], i, j;
} HeaderCipher;
static HeaderCipher world_send, world_receive, warden_receive;
static uint32_t world_initialized;
static uint8_t cipher_byte(HeaderCipher *c) {
  c->i++;
  c->j = (uint8_t)(c->j + c->s[c->i]);
  uint8_t t = c->s[c->i];
  c->s[c->i] = c->s[c->j];
  c->s[c->j] = t;
  return c->s[(uint8_t)(c->s[c->i] + c->s[c->j])];
}
static void hmac_seed(const uint8_t seed[16], uint8_t out[20]) {
  uint8_t temp[104], inner[20];
  for (uint32_t i = 0; i < 64; i++)
    temp[i] = (i < 16 ? seed[i] : 0) ^ 0x36;
  memcpy(temp + 64, key, 40);
  hash(temp, 104, inner);
  for (uint32_t i = 0; i < 64; i++)
    temp[i] = (i < 16 ? seed[i] : 0) ^ 0x5c;
  memcpy(temp + 64, inner, 20);
  hash(temp, 84, out);
  erase(temp, sizeof(temp));
  erase(inner, sizeof(inner));
}
static void cipher_init(HeaderCipher *c, const uint8_t seed[16]) {
  uint8_t digest[20], j = 0;
  hmac_seed(seed, digest);
  for (uint32_t i = 0; i < 256; i++)
    c->s[i] = (uint8_t)i;
  for (uint32_t i = 0; i < 256; i++) {
    j = (uint8_t)(j + c->s[i] + digest[i % 20]);
    uint8_t t = c->s[i];
    c->s[i] = c->s[j];
    c->s[j] = t;
  }
  c->i = c->j = 0;
  for (uint32_t i = 0; i < 1024; i++)
    (void)cipher_byte(c);
  erase(digest, sizeof(digest));
}
static void warden_initial_ciphers(HeaderCipher *send, HeaderCipher *receive);
#include "warden_transfer.c"

/* Initial Warden receive stream is independent of world header encryption.
 * Only inspect the request; no module execution or successful check is claimed. */
static void warden_initial_ciphers(HeaderCipher *send, HeaderCipher *receive) {
  uint8_t material[60] = {0}, first[20], second[20], block[20], receive_key[16], send_key[16];
  hash(key, 20, first);
  hash(key + 20, 20, second);
  memcpy(material, first, 20);
  memcpy(material + 40, second, 20);
  hash(material, 60, block);
  memcpy(send_key, block, 16);
  memcpy(receive_key, block + 16, 4);
  memcpy(material + 20, block, 20);
  hash(material, 60, block);
  memcpy(receive_key + 4, block, 12);
  uint8_t j = 0;
  for (uint32_t i = 0; i < 256; i++)
    send->s[i] = (uint8_t)i;
  /* The first 16 generator bytes are client-to-server. */
  for (uint32_t i = 0; i < 256; i++) {
    j = (uint8_t)(j + send->s[i] + send_key[i % 16]);
    uint8_t t = send->s[i];
    send->s[i] = send->s[j];
    send->s[j] = t;
  }
  send->i = send->j = 0;
  j = 0;
  for (uint32_t i = 0; i < 256; i++)
    receive->s[i] = (uint8_t)i;
  for (uint32_t i = 0; i < 256; i++) {
    j = (uint8_t)(j + receive->s[i] + receive_key[i % 16]);
    uint8_t t = receive->s[i];
    receive->s[i] = receive->s[j];
    receive->s[j] = t;
  }
  receive->i = receive->j = 0;
  erase(material, sizeof(material));
  erase(first, sizeof(first));
  erase(second, sizeof(second));
  erase(block, sizeof(block));
  erase(receive_key, sizeof(receive_key));
  erase(send_key, sizeof(send_key));
}
static void warden_init(void) {
  warden_transfer_clear();
  warden_initial_ciphers(&warden_send, &warden_receive);
}
static void write32(uint8_t *out, uint32_t value) {
  for (uint32_t i = 0; i < 4; i++)
    out[i] = (uint8_t)(value >> (i * 8));
}
void bw_world_clear(void) {
  warden_transfer_clear();
  clear_characters();
  erase(&world_send, sizeof(world_send));
  erase(&world_receive, sizeof(world_receive));
  erase(&warden_receive, sizeof(warden_receive));
  world_initialized = 0;
}
/* Owner client image transfer occurs before opening authentication transport.
 * These APIs cannot replace an image while a world VM holds live mappings. */
/* Explicit local diagnostic: the signed handler may run, but its runtime
 * check replies are always discarded. No audit request reports a check pass. */
uint32_t bw_warden_audit(uint32_t enabled) {
  if (phase != 3 || world_initialized || enabled > 1)
    return 1;
  warden_audit_mode = enabled;
  return 0;
}
/* Simulated check replies are allowed only after the browser has confirmed
 * the realm address is on this machine. Closing the account clears the flag. */
uint32_t bw_warden_local(uint32_t enabled) {
  if (enabled > 1)
    return 1;
  warden_local_mode = enabled;
  return 0;
}
uint32_t bw_warden_client_begin(uint32_t size) {
  return phase == 1 && !world_initialized ? bw_client_image_begin(size) : 1;
}
uint32_t bw_warden_client_chunk(uint32_t size) {
  if (phase != 1 || world_initialized || size > sizeof(io))
    return 1;
  uint32_t status = bw_client_image_append(io, size);
  erase(io, sizeof(io));
  return status;
}
uint32_t bw_warden_client_finish(void) {
  return phase == 1 && !world_initialized ? bw_client_image_finish() : 1;
}
/* io before: server seed[4], CSPRNG client seed[4]. io after: AUTH_SESSION body. */
uint32_t bw_world_auth(uint32_t realm) {
  if (phase != 3 || !account_length)
    return 0;
  uint8_t message[68], digest[20], server[4], client[4];
  memcpy(server, io, 4);
  memcpy(client, io + 4, 4);
  memcpy(message, account_name, account_length);
  memset(message + account_length, 0, 4);
  memcpy(message + account_length + 4, client, 4);
  memcpy(message + account_length + 8, server, 4);
  memcpy(message + account_length + 12, key, 40);
  hash(message, account_length + 52, digest);
  erase(io, sizeof(io));
  write32(io, 12340);
  memcpy(io + 8, account_name, account_length);
  uint32_t at = 9 + account_length;
  at += 4;
  memcpy(io + at, client, 4);
  at += 4;
  at += 8;
  write32(io + at, realm);
  at += 4;
  at += 8;
  memcpy(io + at, digest, 20);
  at += 20;
  at += 4;
  static const uint8_t receive_seed[16] = {0xcc, 0x98, 0xae, 0x04, 0xe8, 0x97, 0xea, 0xca,
                                           0x12, 0xdd, 0xc0, 0x93, 0x42, 0x91, 0x53, 0x57};
  static const uint8_t send_seed[16] = {0xc2, 0xb3, 0x72, 0x3c, 0xc6, 0xae, 0xd9, 0xb5,
                                        0x34, 0x3c, 0x53, 0xee, 0x2f, 0x43, 0x67, 0xce};
  cipher_init(&world_send, send_seed);
  cipher_init(&world_receive, receive_seed);
  warden_init();
  world_initialized = 1;
  erase(message, sizeof(message));
  erase(digest, sizeof(digest));
  erase(server, sizeof(server));
  erase(client, sizeof(client));
  return at;
}
uint32_t bw_world_encrypt(uint32_t length) {
  if (!world_initialized || length != 6)
    return 1;
  for (uint32_t i = 0; i < length; i++)
    io[i] ^= cipher_byte(&world_send);
  return 0;
}
uint32_t bw_world_decrypt(uint32_t length) {
  if (!world_initialized || !length || length > 5)
    return 1;
  for (uint32_t i = 0; i < length; i++)
    io[i] ^= cipher_byte(&world_receive);
  return 0;
}
#define WORLD_CHARACTER_LIMIT 50
typedef struct {
  uint8_t guid[8], appearance[9], first;
  char name[49];
  uint32_t zone, map, guild, flags, custom, pet[3], equipment[23][3];
  float position[3];
} WorldCharacter;
static WorldCharacter characters[WORLD_CHARACTER_LIMIT];
static uint32_t character_count;
static void clear_characters(void) {
  erase(characters, sizeof(characters));
  character_count = 0;
}
static uint32_t read32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
uint32_t bw_world_characters(uint32_t length) {
  erase(characters, sizeof(characters));
  character_count = 0;
  if (!length || length > sizeof(io) || io[0] > WORLD_CHARACTER_LIMIT)
    return 1;
  uint32_t count = io[0], at = 1;
  for (uint32_t i = 0; i < count; i++) {
    WorldCharacter *c = &characters[i];
    if (at + 9 > length)
      goto invalid;
    memcpy(c->guid, io + at, 8);
    at += 8;
    uint32_t start = at;
    while (at < length && io[at])
      at++;
    if (at >= length || at - start > 48 || at == start)
      goto invalid;
    memcpy(c->name, io + start, at - start);
    at++;
    if (at + 9 + 8 + 12 + 12 + 1 + 12 + 207 > length)
      goto invalid;
    memcpy(c->appearance, io + at, 9);
    at += 9;
    c->zone = read32(io + at);
    at += 4;
    c->map = read32(io + at);
    at += 4;
    memcpy(c->position, io + at, 12);
    at += 12;
    c->guild = read32(io + at);
    at += 4;
    c->flags = read32(io + at);
    at += 4;
    c->custom = read32(io + at);
    at += 4;
    c->first = io[at++];
    for (uint32_t j = 0; j < 3; j++) {
      c->pet[j] = read32(io + at);
      at += 4;
    }
    for (uint32_t j = 0; j < 23; j++) {
      c->equipment[j][0] = read32(io + at);
      at += 4;
      c->equipment[j][1] = io[at++];
      c->equipment[j][2] = read32(io + at);
      at += 4;
    }
  }
  if (at != length)
    goto invalid;
  character_count = count;
  erase(io, sizeof(io));
  return 0;
invalid:
  erase(characters, sizeof(characters));
  erase(io, sizeof(io));
  return 1;
}
uint32_t bw_world_character_count(void) {
  return character_count;
}
const char *bw_world_character_name(uint32_t index) {
  return index < character_count ? characters[index].name : 0;
}
double bw_world_character_number(uint32_t index, uint32_t field) {
  if (index >= character_count)
    return -1;
  const WorldCharacter *c = &characters[index];
  if (field < 2)
    return read32(c->guid + field * 4);
  if (field < 11)
    return c->appearance[field - 2];
  if (field == 11)
    return c->zone;
  if (field == 12)
    return c->map;
  if (field < 16)
    return c->position[field - 13];
  if (field == 16)
    return c->guild;
  if (field == 17)
    return c->flags;
  if (field == 18)
    return c->custom;
  if (field == 19)
    return c->first;
  if (field < 23)
    return c->pet[field - 20];
  if (field < 92)
    return c->equipment[(field - 23) / 3][(field - 23) % 3];
  return -1;
}
