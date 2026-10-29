/* Bounded BLP2/BC2+BC3 (DXT3+DXT5) base-mip decoder; not original client code. */
typedef unsigned char u8;
typedef unsigned int u32;
static u8 input[1048576], rgba[512 * 512 * 4];
static u32 width, height;
u8 *input_ptr(void) {
  return input;
}
u8 *rgba_ptr(void) {
  return rgba;
}
u32 image_width(void) {
  return width;
}
u32 image_height(void) {
  return height;
}
static u32 le(u32 at, u32 n) {
  u32 v = 0;
  for (u32 i = 0; i < n; i++)
    v |= (u32)input[at + i] << (i * 8);
  return v;
}
static void rgb565(u32 v, u32 *rgb) {
  u32 r = v >> 11, g = (v >> 5) & 63, b = v & 31;
  rgb[0] = (r << 3) | (r >> 2);
  rgb[1] = (g << 2) | (g >> 4);
  rgb[2] = (b << 3) | (b >> 2);
}
u32 decode_blp(u32 n) {
  width = height = 0;
  if (n < 148 || n > 1048576)
    return 1;
  if (le(0, 4) != 0x32504c42 || le(4, 4) != 1 || input[8] != 2 ||
      !((input[9] == 0 && input[10] == 0) ||
        (input[9] == 8 && (input[10] == 1 || input[10] == 7))) ||
      input[11] > 1)
    return 2;
  u32 w = le(12, 4), h = le(16, 4), off = le(20, 4), size = le(84, 4);
  if (!w || !h || w > 512 || h > 512 || (w & (w - 1)) || (h & (h - 1)) || w % 4 || h % 4)
    return 3;
  u32 step = input[9] == 0 ? 8 : 16;
  if (size != (w / 4) * (h / 4) * step || off < 148 || off > n || size > n - off)
    return 4;
  for (u32 by = 0; by < h / 4; by++)
    for (u32 bx = 0; bx < w / 4; bx++) {
      u32 at = off + (by * (w / 4) + bx) * step, colorAt = at + (step == 16 ? 8 : 0), color[4][3];
      rgb565(le(colorAt, 2), color[0]);
      rgb565(le(colorAt + 2, 2), color[1]);
      for (u32 c = 0; c < 3; c++) {
        if (step == 8 && le(colorAt, 2) <= le(colorAt + 2, 2)) {
          color[2][c] = (color[0][c] + color[1][c]) / 2;
          color[3][c] = 0;
        } else {
          color[2][c] = (2 * color[0][c] + color[1][c]) / 3;
          color[3][c] = (color[0][c] + 2 * color[1][c]) / 3;
        }
      }
      u32 codes = le(colorAt + 4, 4), alpha[8] = {0};
      unsigned long long alphaCodes = 0;
      if (input[10] == 7) {
        alpha[0] = input[at];
        alpha[1] = input[at + 1];
        if (alpha[0] > alpha[1]) {
          for (u32 k = 2; k < 8; k++)
            alpha[k] = ((8 - k) * alpha[0] + (k - 1) * alpha[1]) / 7;
        } else {
          for (u32 k = 2; k < 6; k++)
            alpha[k] = ((6 - k) * alpha[0] + (k - 1) * alpha[1]) / 5;
          alpha[6] = 0;
          alpha[7] = 255;
        }
        for (u32 k = 0; k < 6; k++)
          alphaCodes |= (unsigned long long)input[at + 2 + k] << (8 * k);
      }
      for (u32 p = 0; p < 16; p++) {
        u32 dst = ((by * 4 + p / 4) * w + bx * 4 + p % 4) * 4, which = (codes >> (p * 2)) & 3;
        for (u32 c = 0; c < 3; c++)
          rgba[dst + c] = (u8)color[which][c];
        rgba[dst + 3] = step == 8 ? (which == 3 && le(colorAt, 2) <= le(colorAt + 2, 2) ? 0 : 255)
                        : input[10] == 1 ? (u8)(((input[at + p / 2] >> ((p % 2) * 4)) & 15) * 17)
                                         : (u8)alpha[(alphaCodes >> (p * 3)) & 7];
      }
    }
  width = w;
  height = h;
  return 0;
}
