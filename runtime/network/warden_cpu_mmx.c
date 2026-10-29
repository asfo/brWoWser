/* Included by warden_cpu.c. Integer MMX forms required by the signed module.
 * No x87/SSE state or unsupported instruction is treated as a successful no-op. */
static uint64_t packed_shift(uint64_t value, uint64_t count, uint32_t bits, uint32_t kind) {
  uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1, result = 0;
  for (uint32_t at = 0; at < 64; at += bits) {
    uint64_t lane = (value >> at) & mask, shifted = 0;
    uint32_t negative = (lane >> (bits - 1)) != 0;
    if (count >= bits)
      shifted = kind == 4 && negative ? mask : 0;
    else if (kind == 6)
      shifted = (lane << count) & mask;
    else {
      shifted = lane >> count;
      if (kind == 4 && negative && count)
        shifted |= mask ^ (mask >> count);
    }
    result |= shifted << at;
  }
  return result;
}
static int mmx_instruction(x86emu_t *context) {
  uint32_t pc = context->x86.R_EIP;
  if (pc < GUEST_IMAGE || pc - GUEST_IMAGE >= image_size || image_size - (pc - GUEST_IMAGE) < 2)
    return 0;
  uint32_t at = pc - GUEST_IMAGE;
  if (image[at++] != 0x0f)
    return 0;
  uint32_t op = image[at++];
  if (op == 0x77)
    goto complete; /* EMMS marks x87 tags empty; register bits remain. */
  switch (op) {
  case 0x6e:
  case 0x7e:
  case 0x6f:
  case 0x7f:
  case 0x70:
  case 0x71:
  case 0x72:
  case 0x73:
  case 0xf5:
  case 0xd5:
  case 0xef:
  case 0xeb:
  case 0xdb:
  case 0xdf:
  case 0xfc:
  case 0xfd:
  case 0xfe:
  case 0xd4:
  case 0xf8:
  case 0xf9:
  case 0xfa:
  case 0xfb:
  case 0xd1:
  case 0xd2:
  case 0xd3:
  case 0xe1:
  case 0xe2:
  case 0xf1:
  case 0xf2:
  case 0xf3:
  case 0x60:
  case 0x61:
  case 0x62:
  case 0x68:
  case 0x69:
  case 0x6a:
  case 0x74:
  case 0x75:
  case 0x76:
  case 0x64:
  case 0x65:
  case 0x66:
    break;
  default:
    return 0;
  }
  if (at == image_size) {
    outcome = 4;
    return -1;
  }
  uint32_t modrm = image[at++], reg = (modrm >> 3) & 7, rm = modrm & 7, mod = modrm >> 6;
  uint32_t address = 0, low = 0, high = 0;
  if (mod != 3 && !operand_address(context, modrm, &at, &address))
    return -1;
  if (op >= 0x71 && op <= 0x73) {
    if (mod != 3 || (reg != 2 && reg != 4 && reg != 6) || (op == 0x73 && reg == 4)) {
      outcome = 6;
      return -1;
    }
    if (at == image_size) {
      outcome = 4;
      return -1;
    }
    mmx[rm] = packed_shift(mmx[rm], image[at++], op == 0x71 ? 16 : op == 0x72 ? 32 : 64, reg);
    goto complete;
  }
  if (op == 0x7e || op == 0x7f) {
    if (mod == 3) {
      if (op == 0x7e)
        *guest_register(context, rm) = (uint32_t)mmx[reg];
      else
        mmx[rm] = mmx[reg];
    } else {
      low = (uint32_t)mmx[reg];
      high = (uint32_t)(mmx[reg] >> 32);
      if (memory_access(context, address, &low, X86EMU_MEMIO_W | X86EMU_MEMIO_32))
        return -1;
      if (op == 0x7f && (address > 0xfffffffbu || memory_access(context, address + 4, &high,
                                                                X86EMU_MEMIO_W | X86EMU_MEMIO_32)))
        return -1;
    }
    goto complete;
  }
  uint64_t source = 0, destination = mmx[reg];
  if (mod == 3)
    source = op == 0x6e ? *guest_register(context, rm) : mmx[rm];
  else {
    if (memory_access(context, address, &low, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
    if (op != 0x6e && (address > 0xfffffffbu || memory_access(context, address + 4, &high,
                                                              X86EMU_MEMIO_R | X86EMU_MEMIO_32)))
      return -1;
    source = (uint64_t)low | (uint64_t)high << 32;
  }
  uint64_t result = 0;
  if (op == 0x6e || op == 0x6f)
    result = source;
  else if (op == 0xef)
    result = destination ^ source;
  else if (op == 0xeb)
    result = destination | source;
  else if (op == 0xdb)
    result = destination & source;
  else if (op == 0xdf)
    result = ~destination & source;
  else if (op == 0xf5) {
    for (uint32_t lane = 0; lane < 2; lane++) {
      uint32_t shift = lane * 32;
      int64_t a = (int16_t)(destination >> shift), b = (int16_t)(source >> shift);
      int64_t c = (int16_t)(destination >> (shift + 16)), d = (int16_t)(source >> (shift + 16));
      result |= (uint64_t)(uint32_t)(a * b + c * d) << shift;
    }
  } else if (op == 0x70) {
    if (at == image_size) {
      outcome = 4;
      return -1;
    }
    uint32_t order = image[at++];
    for (uint32_t lane = 0; lane < 4; lane++)
      result |= ((source >> (((order >> (lane * 2)) & 3) * 16)) & 65535) << (lane * 16);
  } else if ((op >= 0xd1 && op <= 0xd3) || op == 0xe1 || op == 0xe2 || (op >= 0xf1 && op <= 0xf3)) {
    uint32_t kind = op >= 0xf1 ? 6 : op >= 0xe1 ? 4 : 2;
    uint32_t bits = (op & 15) == 1 ? 16 : (op & 15) == 2 ? 32 : 64;
    result = packed_shift(destination, source, bits, kind);
  } else if ((op >= 0x60 && op <= 0x62) || (op >= 0x68 && op <= 0x6a)) {
    uint32_t bits = 8u << (op & 3), half = 32 / bits, start = op >= 0x68 ? half : 0;
    uint64_t mask = (UINT64_C(1) << bits) - 1;
    for (uint32_t lane = 0; lane < half; lane++) {
      result |= ((destination >> ((start + lane) * bits)) & mask) << (lane * 2 * bits);
      result |= ((source >> ((start + lane) * bits)) & mask) << ((lane * 2 + 1) * bits);
    }
  } else if ((op >= 0x74 && op <= 0x76) || (op >= 0x64 && op <= 0x66)) {
    uint32_t bits = 8u << ((op & 15) - 4);
    uint64_t mask = (UINT64_C(1) << bits) - 1;
    for (uint32_t lane = 0; lane < 64; lane += bits) {
      uint64_t a = (destination >> lane) & mask, b = (source >> lane) & mask;
      uint32_t match = op >= 0x74   ? a == b
                       : bits == 8  ? (int8_t)a > (int8_t)b
                       : bits == 16 ? (int16_t)a > (int16_t)b
                                    : (int32_t)a > (int32_t)b;
      if (match)
        result |= mask << lane;
    }
  } else {
    uint32_t bits = op == 0xd5                 ? 16
                    : op == 0xd4 || op == 0xfb ? 64
                    : op == 0xfc || op == 0xf8 ? 8
                    : op == 0xfd || op == 0xf9 ? 16
                                               : 32;
    uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    uint32_t subtract = op >= 0xf8 && op <= 0xfb;
    for (uint32_t lane = 0; lane < 64; lane += bits) {
      uint64_t a = (destination >> lane) & mask, b = (source >> lane) & mask;
      uint64_t value = op == 0xd5 ? a * b : subtract ? a - b : a + b;
      result |= (value & mask) << lane;
    }
  }
  mmx[reg] = result;
complete:
  context->x86.R_EIP = context->x86.saved_eip = GUEST_IMAGE + at;
  context->x86.R_TSC++;
  return 1;
}
