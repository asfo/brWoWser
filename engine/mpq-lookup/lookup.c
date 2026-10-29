/* Independent bounded classic-MPQ hash/table lookup, not native client source. */
typedef unsigned char u8;
typedef unsigned int u32;
static u32 crypt[1280], ht[524288], bt[524288], result[8];
static u8 name[256];
static u32 hn, bn, ready;
u32 *hash_ptr(void) {
  return ht;
}
u32 *block_ptr(void) {
  return bt;
}
u8 *name_ptr(void) {
  return name;
}
u32 *result_ptr(void) {
  return result;
}
static void initcrypt(void) {
  u32 seed = 0x100001;
  for (u32 byte = 0; byte < 256; byte++)
    for (u32 kind = 0; kind < 5; kind++) {
      seed = (seed * 125 + 3) % 0x2aaaab;
      u32 hi = (seed & 65535) << 16;
      seed = (seed * 125 + 3) % 0x2aaaab;
      crypt[kind * 256 + byte] = hi | (seed & 65535);
    }
}
u32 hash_name(u32 len, u32 kind) {
  if (len > 255 || kind > 3)
    return 0;
  u32 a = 0x7fed7fed, b = 0xeeeeeeee;
  for (u32 i = 0; i < len; i++) {
    u32 c = name[i];
    if (c > 127 || !c)
      return 0;
    if (c == '/')
      c = '\\';
    if (c >= 'a' && c <= 'z')
      c -= 32;
    a = crypt[kind * 256 + c] ^ (a + b);
    b = c + a + b + (b << 5) + 3;
  }
  return a;
}
static void decrypt(u32 *data, u32 n, u32 key) {
  u32 seed = 0xeeeeeeee;
  for (u32 i = 0; i < n; i++) {
    seed += crypt[1024 + (key & 255)];
    u32 plain = data[i] ^ (key + seed);
    key = ((~key << 21) + 0x11111111) | (key >> 11);
    seed = plain + seed + (seed << 5) + 3;
    data[i] = plain;
  }
}
u32 load_tables(u32 hashes, u32 blocks) {
  ready = 0;
  if (!hashes || (hashes & (hashes - 1)) || hashes > 131072 || !blocks || blocks > 131072)
    return 0;
  initcrypt();
  hn = hashes;
  bn = blocks;
  decrypt(ht, hn * 4, 0xc3af3770);
  decrypt(bt, bn * 4, 0xec83b3a3);
  ready = 1;
  return 1;
}
/* Exact requested locale/platform only. No archive/locale fallback policy. */
u32 lookup(u32 len, u32 locale, u32 platform) {
  for (u32 i = 0; i < 8; i++)
    result[i] = 0;
  if (!ready || !len || len > 255 || locale > 65535 || platform > 65535)
    return 2;
  for (u32 i = 0; i < len; i++)
    if (!name[i] || name[i] > 127)
      return 2;
  u32 start = hash_name(len, 0) & (hn - 1), a = hash_name(len, 1), b = hash_name(len, 2);
  for (u32 step = 0; step < hn; step++) {
    u32 slot = (start + step) & (hn - 1), *h = ht + slot * 4, index = h[3];
    if (index == 0xffffffff)
      return 0;
    if (index == 0xfffffffe || h[0] != a || h[1] != b || (h[2] & 65535) != locale ||
        (h[2] >> 16) != platform)
      continue;
    if (index >= bn)
      return 2;
    u32 *block = bt + index * 4;
    for (u32 j = 0; j < 4; j++)
      result[j] = block[j];
    result[4] = slot;
    result[5] = index;
    result[6] = locale;
    result[7] = platform;
    return 1;
  }
  return 0;
}
