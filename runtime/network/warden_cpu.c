/* Bounded x86 interpreter adapter. No guest address is a host/WASM pointer.
 * Upstream instruction engine: pinned libx86emu, see x86-toolchain.json.
 * Windows imports and client service calls stop at an explicit boundary. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "x86/include/x86emu.h"
#include "warden_memory.h"
#include "warden_client_image.h"
__attribute__((import_module("env"), import_name("bw_clock_unix_ms"))) extern double
bw_clock_unix_ms(void);
__attribute__((import_module("env"), import_name("bw_clock_monotonic_ms"))) extern double
bw_clock_monotonic_ms(void);
__attribute__((import_module("env"), import_name("bw_warden_lua_execute"))) extern uint32_t
bw_warden_lua_execute(uint32_t source, uint32_t length);
#define GUEST_IMAGE 0x10000000u
#define GUEST_STACK 0x20000000u
#define GUEST_IMPORT 0x30000000u
#define GUEST_SERVICE 0x31000000u
#define GUEST_TEB 0x40000000u
#define GUEST_HEAP 0x50000000u
#define GUEST_RETURN 0x32000000u
#define GUEST_SHARED 0x7ffe0000u
static x86emu_t cpu;
static BwWardenMemory memory;
static uint64_t msrs[X86EMU_MSRS], mmx[8];
static uint8_t msr_permissions[X86EMU_MSRS];
static uint8_t stack[65536], teb[4096], heap[131072], shared_clock[4096];
static int refresh_shared_clock(void);
static uint8_t sent_packet[65536], cipher_data[4096];
static uint32_t sent_size, cipher_size, heap_end, client_lua, client_counter, lua_calls;
typedef struct {
  uint32_t offset, capacity, active;
} GuestAllocation;
static GuestAllocation allocations[128];
static uint32_t import_types[128], import_count, api_calls[20];
static char unavailable_import[128];
static uint8_t image_permissions[32];
static uint8_t *image;
static uint32_t image_size, outcome, dependency, steps, accesses, fault_opcode, fault_vector;
/* The engine's optional wall-time limit is disabled. Its only clock use here
 * is internal bookkeeping; execution is bounded by an instruction budget. */
time_t time(time_t *out) {
  if (out)
    *out = 0;
  return 0;
}
unsigned x86emu_clear_log(x86emu_t *context, int flush) {
  (void)context;
  (void)flush;
  return 0;
}
/* The engine's private debugging pseudo-operations are not guest services. */
void x86emu_dump(x86emu_t *context, int flags) {
  (void)flags;
  outcome = 6;
  x86emu_stop(context);
}
void x86emu_reset_access_stats(x86emu_t *context) {
  outcome = 6;
  x86emu_stop(context);
}
static void scrub(void *memory, uint32_t size) {
  volatile uint8_t *bytes = memory;
  while (size--)
    *bytes++ = 0;
}
/* The module's unload method exports its ciphers and frees its object.
 * Fork that call privately; the active signed instance must stay alive.
 * Allowed only before translated Lua/timer callbacks are registered. */
typedef struct {
  BwWardenMemorySnapshot *memory;
  x86emu_t cpu;
  uint64_t msrs[X86EMU_MSRS], mmx[8];
  uint8_t msr_permissions[X86EMU_MSRS];
  GuestAllocation allocations[128];
  uint8_t sent_packet[65536], cipher_data[4096], image_permissions[32];
  uint32_t import_types[128], api_calls[20];
  char unavailable_import[128];
  uint32_t sent_size, cipher_size, heap_end, import_count, client_lua, client_counter, lua_calls,
      outcome, dependency, steps, accesses, fault_opcode, fault_vector;
} CpuCheckpoint;
static CpuCheckpoint *warden_checkpoint;
void bw_warden_cpu_discard_checkpoint(void) {
  if (!warden_checkpoint)
    return;
  bw_warden_memory_snapshot_free(warden_checkpoint->memory);
  scrub(warden_checkpoint, sizeof(*warden_checkpoint));
  free(warden_checkpoint);
  warden_checkpoint = NULL;
}
uint32_t bw_warden_cpu_checkpoint(void) {
  if (warden_checkpoint || !memory.count || client_lua || client_counter)
    return 0;
  warden_checkpoint = calloc(1, sizeof(*warden_checkpoint));
  if (!warden_checkpoint)
    return 0;
  warden_checkpoint->memory = bw_warden_memory_snapshot(&memory);
  if (!warden_checkpoint->memory) {
    bw_warden_cpu_discard_checkpoint();
    return 0;
  }
  memcpy(&warden_checkpoint->cpu, &cpu, sizeof(cpu));
  memcpy(warden_checkpoint->msrs, msrs, sizeof(msrs));
  memcpy(warden_checkpoint->mmx, mmx, sizeof(mmx));
  memcpy(warden_checkpoint->msr_permissions, msr_permissions, sizeof(msr_permissions));
  memcpy(warden_checkpoint->allocations, allocations, sizeof(allocations));
  memcpy(warden_checkpoint->sent_packet, sent_packet, sizeof(sent_packet));
  memcpy(warden_checkpoint->cipher_data, cipher_data, sizeof(cipher_data));
  memcpy(warden_checkpoint->import_types, import_types, sizeof(import_types));
  memcpy(warden_checkpoint->api_calls, api_calls, sizeof(api_calls));
  memcpy(warden_checkpoint->image_permissions, image_permissions, sizeof(image_permissions));
  memcpy(warden_checkpoint->unavailable_import, unavailable_import, sizeof(unavailable_import));
  warden_checkpoint->sent_size = sent_size;
  warden_checkpoint->cipher_size = cipher_size;
  warden_checkpoint->heap_end = heap_end;
  warden_checkpoint->import_count = import_count;
  warden_checkpoint->client_lua = client_lua;
  warden_checkpoint->client_counter = client_counter;
  warden_checkpoint->lua_calls = lua_calls;
  warden_checkpoint->outcome = outcome;
  warden_checkpoint->dependency = dependency;
  warden_checkpoint->steps = steps;
  warden_checkpoint->accesses = accesses;
  warden_checkpoint->fault_opcode = fault_opcode;
  warden_checkpoint->fault_vector = fault_vector;
  return 1;
}
uint32_t bw_warden_cpu_restore_checkpoint(void) {
  if (!warden_checkpoint)
    return 0;
  if (bw_warden_memory_restore(&memory, warden_checkpoint->memory)) {
    bw_warden_cpu_discard_checkpoint();
    return 0;
  }
  memcpy(&cpu, &warden_checkpoint->cpu, sizeof(cpu));
  memcpy(msrs, warden_checkpoint->msrs, sizeof(msrs));
  memcpy(mmx, warden_checkpoint->mmx, sizeof(mmx));
  memcpy(msr_permissions, warden_checkpoint->msr_permissions, sizeof(msr_permissions));
  memcpy(allocations, warden_checkpoint->allocations, sizeof(allocations));
  memcpy(sent_packet, warden_checkpoint->sent_packet, sizeof(sent_packet));
  memcpy(cipher_data, warden_checkpoint->cipher_data, sizeof(cipher_data));
  memcpy(import_types, warden_checkpoint->import_types, sizeof(import_types));
  memcpy(api_calls, warden_checkpoint->api_calls, sizeof(api_calls));
  memcpy(image_permissions, warden_checkpoint->image_permissions, sizeof(image_permissions));
  memcpy(unavailable_import, warden_checkpoint->unavailable_import, sizeof(unavailable_import));
  sent_size = warden_checkpoint->sent_size;
  cipher_size = warden_checkpoint->cipher_size;
  heap_end = warden_checkpoint->heap_end;
  import_count = warden_checkpoint->import_count;
  client_lua = warden_checkpoint->client_lua;
  client_counter = warden_checkpoint->client_counter;
  lua_calls = warden_checkpoint->lua_calls;
  outcome = warden_checkpoint->outcome;
  dependency = warden_checkpoint->dependency;
  steps = warden_checkpoint->steps;
  accesses = warden_checkpoint->accesses;
  fault_opcode = warden_checkpoint->fault_opcode;
  fault_vector = warden_checkpoint->fault_vector;
  bw_warden_cpu_discard_checkpoint();
  return 1;
}
void bw_warden_cpu_clear(void) {
  bw_warden_cpu_discard_checkpoint();
  bw_warden_memory_reset(&memory);
  scrub(&cpu, sizeof(cpu));
  scrub(msrs, sizeof(msrs));
  scrub(mmx, sizeof(mmx));
  scrub(msr_permissions, sizeof(msr_permissions));
  scrub(stack, sizeof(stack));
  scrub(teb, sizeof(teb));
  scrub(shared_clock, sizeof(shared_clock));
  scrub(heap, sizeof(heap));
  scrub(allocations, sizeof(allocations));
  scrub(sent_packet, sizeof(sent_packet));
  scrub(cipher_data, sizeof(cipher_data));
  sent_size = cipher_size = heap_end = client_lua = client_counter = 0;
  scrub(import_types, sizeof(import_types));
  scrub(api_calls, sizeof(api_calls));
  scrub(unavailable_import, sizeof(unavailable_import));
  import_count = 0;
  scrub(image_permissions, sizeof(image_permissions));
  image = 0;
  image_size = outcome = dependency = steps = accesses = fault_opcode = 0;
  fault_vector = 0xffffffffu;
}
static unsigned memory_access(x86emu_t *context, uint32_t address, uint32_t *value, unsigned type) {
  if (++accesses > 1000000) {
    outcome = 5;
    context->x86.R_ECX = 0;
    x86emu_stop(context);
    return 1;
  }
  unsigned width = type & 255, operation = type & ~255u;
  uint32_t count = width == X86EMU_MEMIO_8 || width == X86EMU_MEMIO_8_NOPERM ? 1
                   : width == X86EMU_MEMIO_16                                ? 2
                   : width == X86EMU_MEMIO_32                                ? 4
                                                                             : 0;
  uint32_t access = operation == X86EMU_MEMIO_X   ? BW_MEMORY_EXECUTE
                    : operation == X86EMU_MEMIO_W ? BW_MEMORY_WRITE
                    : operation == X86EMU_MEMIO_R ? BW_MEMORY_READ
                                                  : 0;
  if (access != BW_MEMORY_WRITE && address >= GUEST_SHARED &&
      address - GUEST_SHARED < sizeof(shared_clock) && !refresh_shared_clock()) {
    outcome = 4;
    x86emu_stop(context);
    return 1;
  }
  uint8_t bytes[4] = {0};
  if (access == BW_MEMORY_WRITE)
    for (uint32_t i = 0; i < count; i++)
      bytes[i] = (uint8_t)(*value >> (i * 8));
  if (bw_warden_memory_transfer(&memory, address, bytes, count, access)) {
    outcome = 4;
    x86emu_stop(context);
    return 1;
  }
  if (access != BW_MEMORY_WRITE) {
    *value = 0;
    for (uint32_t i = 0; i < count; i++)
      *value |= (uint32_t)bytes[i] << (i * 8);
  }
  return 0;
}
unsigned x86emu_read_byte_noperm(x86emu_t *context, unsigned address) {
  uint32_t value = 0;
  memory_access(context, address, &value, X86EMU_MEMIO_R | X86EMU_MEMIO_8_NOPERM);
  return value;
}
static uint32_t *guest_register(x86emu_t *context, uint32_t index) {
  switch (index & 7) {
  case 0:
    return &context->x86.R_EAX;
  case 1:
    return &context->x86.R_ECX;
  case 2:
    return &context->x86.R_EDX;
  case 3:
    return &context->x86.R_EBX;
  case 4:
    return &context->x86.R_ESP;
  case 5:
    return &context->x86.R_EBP;
  case 6:
    return &context->x86.R_ESI;
  default:
    return &context->x86.R_EDI;
  }
}
static int operand_address(x86emu_t *context, uint32_t modrm, uint32_t *at, uint32_t *address) {
  uint32_t mod = modrm >> 6, rm = modrm & 7, displacement = 0;
  *address = 0;
  if (mod == 3)
    return 0;
  if (rm == 4) {
    if (*at == image_size) {
      outcome = 4;
      return 0;
    }
    uint32_t sib = image[(*at)++], base = sib & 7, index = (sib >> 3) & 7;
    if (index != 4)
      *address = *guest_register(context, index) << (sib >> 6);
    if (base == 5 && mod == 0)
      displacement = 4;
    else
      *address += *guest_register(context, base);
  } else if (rm == 5 && mod == 0)
    displacement = 4;
  else
    *address = *guest_register(context, rm);
  if (mod == 1)
    displacement = 1;
  else if (mod == 2)
    displacement = 4;
  if (*at > image_size || displacement > image_size - *at) {
    outcome = 4;
    return 0;
  }
  if (displacement == 1)
    *address += (uint32_t)(int32_t)(int8_t)image[(*at)++];
  else if (displacement == 4)
    for (uint32_t i = 0; i < 4; i++)
      *address += (uint32_t)image[(*at)++] << (i * 8);
  return 1;
}
#include "warden_cpu_mmx.c"

/* libx86emu does not cover CMPXCHG/XADD. Implement the 32-bit instructions here,
 * including comparison flags and the complete 32-bit ModRM/SIB addressing form.
 * Guest execution is single-threaded, so LOCK needs no host atomic operation. */
static int atomic_instruction(x86emu_t *context, uint32_t pc) {
  if (pc < GUEST_IMAGE || pc - GUEST_IMAGE >= image_size)
    return 0;
  uint32_t at = pc - GUEST_IMAGE, lock = image[at] == 0xf0;
  at += lock;
  if (at > image_size || image_size - at < 3 || image[at] != 0x0f ||
      (image[at + 1] != 0xb1 && image[at + 1] != 0xc1))
    return 0;
  uint32_t exchange_add = image[at + 1] == 0xc1;
  at += 2;
  uint32_t modrm = image[at++], mod = modrm >> 6, rm = modrm & 7;
  uint32_t address = 0, displacement = 0, target = 0;
  if (mod == 3) {
    if (lock) {
      outcome = 6;
      return -1;
    }
    target = *guest_register(context, rm);
  } else {
    if (rm == 4) {
      if (at == image_size) {
        outcome = 4;
        return -1;
      }
      uint32_t sib = image[at++], base = sib & 7, index = (sib >> 3) & 7;
      if (index != 4)
        address = *guest_register(context, index) << (sib >> 6);
      if (base == 5 && mod == 0)
        displacement = 4;
      else
        address += *guest_register(context, base);
    } else if (rm == 5 && mod == 0)
      displacement = 4;
    else
      address = *guest_register(context, rm);
    if (mod == 1)
      displacement = 1;
    else if (mod == 2)
      displacement = 4;
    if (at > image_size || displacement > image_size - at) {
      outcome = 4;
      return -1;
    }
    if (displacement == 1)
      address += (uint32_t)(int32_t)(int8_t)image[at++];
    else if (displacement == 4) {
      for (uint32_t i = 0; i < 4; i++)
        address += (uint32_t)image[at++] << (i * 8);
    }
    if (memory_access(context, address, &target, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
  }
  uint32_t source = *guest_register(context, (modrm >> 3) & 7);
  uint32_t a = exchange_add ? target : context->x86.R_EAX;
  uint32_t b = exchange_add ? source : target;
  uint32_t difference = exchange_add ? a + b : a - b;
  uint32_t carry = exchange_add ? ((uint64_t)a + b) >> 32 : a < b;
  uint32_t overflow = exchange_add ? (~(a ^ b) & (a ^ difference)) : ((a ^ b) & (a ^ difference));
  uint32_t flags = carry | (!difference ? 1u << 6 : 0) | (difference & 0x80000000 ? 1u << 7 : 0) |
                   ((a ^ b ^ difference) & 0x10) | (overflow & 0x80000000 ? 1u << 11 : 0);
  uint32_t parity = difference & 255;
  parity ^= parity >> 4;
  parity ^= parity >> 2;
  parity ^= parity >> 1;
  if (!(parity & 1))
    flags |= 4;
  context->x86.R_EFLG = (context->x86.R_EFLG & ~0x8d5u) | flags;
  if (exchange_add || a == target) {
    uint32_t replacement = exchange_add ? difference : source;
    if (exchange_add)
      *guest_register(context, (modrm >> 3) & 7) = target;
    if (mod == 3)
      *guest_register(context, rm) = replacement;
    else if (memory_access(context, address, &replacement, X86EMU_MEMIO_W | X86EMU_MEMIO_32))
      return -1;
  } else
    context->x86.R_EAX = target;
  context->x86.R_EIP = context->x86.saved_eip = GUEST_IMAGE + at;
  context->x86.R_TSC++;
  return 1;
}
/* These are guest process services, not claims about a native Windows host. */
uint32_t bw_warden_cpu_import_id(const char *library, const char *name) {
  if (strcmp(library, "KERNEL32.dll") && strcmp(library, "kernel32.dll"))
    return 0;
  static const char *names[] = {"GetSystemTimeAsFileTime",
                                "QueryPerformanceCounter",
                                "GetTickCount",
                                "GetCurrentProcessId",
                                "GetCurrentThreadId",
                                "GetModuleHandleA",
                                "GetProcAddress",
                                "GetSystemInfo",
                                "LoadLibraryA",
                                "FreeLibrary",
                                "GetVersionExA",
                                "VirtualQuery",
                                "VirtualQueryEx",
                                "ReadProcessMemory",
                                "GetCurrentProcess",
                                "GetLastError",
                                "SetLastError",
                                "IsBadReadPtr",
                                "QueryPerformanceFrequency"};
  for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if (!strcmp(name, names[i]))
      return i + 1;
  return 0;
}
static int guest_string(x86emu_t *context, uint32_t address, char out[128]) {
  for (uint32_t i = 0; i < 128; i++) {
    uint32_t value = 0;
    if (address > 0xffffffffu - i ||
        memory_access(context, address + i, &value, X86EMU_MEMIO_R | X86EMU_MEMIO_8))
      return 0;
    if (!value) {
      out[i] = 0;
      return 1;
    }
    if (value < 32 || value > 126 || i == 127) {
      outcome = 4;
      x86emu_stop(context);
      return 0;
    }
    out[i] = (char)value;
  }
  return 0;
}
#include "warden_pages.c"
static uint32_t dynamic_import(uint32_t type) {
  if (!type)
    return 0;
  for (uint32_t i = 0; i < import_count; i++)
    if (import_types[i] == type)
      return GUEST_IMPORT + i * 16;
  if (!type || import_count == 128)
    return 0;
  uint32_t slot = import_count++;
  import_types[slot] = type;
  return GUEST_IMPORT + slot * 16;
}
static int process_service(x86emu_t *context, uint32_t id) {
  if (id < 20)
    api_calls[id]++;
  uint32_t result = 0, arguments = id <= 2 || id == 6 || id == 8 || id == 9 || id == 10 || id == 11
                                       ? 1
                                   : id == 7 ? 2
                                             : 0;
  int handled = page_service(context, id, &result, &arguments);
  if (handled < 0)
    return -1;
  if (handled) {
    /* The shared epilogue applies the actual stdcall argument count. */
  } else if (id == 11) {
    uint32_t destination = 0, size = 0;
    if (memory_access(context, context->x86.R_ESP + 4, &destination,
                      X86EMU_MEMIO_R | X86EMU_MEMIO_32) ||
        memory_access(context, destination, &size, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
    /* Declared guest ABI baseline: Win32 NT 5.1 SP3. This is virtual OS
     * compatibility data, never the browser host's OS version or integrity. */
    if (size == 148 || size == 156) {
      if (destination > 0xffffffffu - size) {
        outcome = 4;
        x86emu_stop(context);
        return -1;
      }
      uint32_t fields[39] = {0};
      fields[0] = size;
      fields[1] = 5;
      fields[2] = 1;
      fields[3] = 2600;
      fields[4] = 2;
      memcpy((uint8_t *)fields + 20, "Service Pack 3", 15);
      if (size == 156) {
        fields[37] = 3;
        fields[38] = 0x00010000;
      }
      for (uint32_t i = 0; i < size / 4; i++)
        if (memory_access(context, destination + i * 4, &fields[i],
                          X86EMU_MEMIO_W | X86EMU_MEMIO_32))
          return -1;
      result = 1;
    }
  } else if (id == 10) {
    uint32_t handle = 0;
    if (memory_access(context, context->x86.R_ESP + 4, &handle, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
    /* Imported core libraries retain their import reference for the session. */
    result = handle == 0x60000000 || handle == 0x61000000;
  } else if (id == 8) {
    uint32_t destination = 0;
    if (memory_access(context, context->x86.R_ESP + 4, &destination,
                      X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
    /* One virtual x86 CPU, 4 KiB pages, 64 KiB virtual mapping granularity. */
    uint32_t fields[9] = {0, 4096, 0x10000, 0x7ffeffff, 1, 1, 586, 65536, 5};
    if (destination > 0xffffffffu - sizeof(fields)) {
      outcome = 4;
      x86emu_stop(context);
      return -1;
    }
    for (uint32_t i = 0; i < 9; i++)
      if (memory_access(context, destination + i * 4, &fields[i], X86EMU_MEMIO_W | X86EMU_MEMIO_32))
        return -1;
  } else if (id == 6 || id == 7 || id == 9) {
    uint32_t first = 0, second = 0;
    if (memory_access(context, context->x86.R_ESP + 4, &first, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
    if (id == 7 &&
        memory_access(context, context->x86.R_ESP + 8, &second, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
    if (id == 6 && !first) {
      /* Main-module handles exist only after a real guest PE image was loaded.
       * Mapping the image does not initialize its Windows imports or TLS. */
      if (bw_client_image_base())
        result = bw_client_image_base();
      else {
        outcome = 2;
        x86emu_stop(context);
        return -1;
      }
    }
    char name[128];
    if ((id == 6 && first) || id == 9 || second > 65535) {
      if (!guest_string(context, id != 7 ? first : second, name))
        return -1;
      if (id != 7) {
        for (uint32_t i = 0; name[i]; i++)
          if (name[i] >= 'A' && name[i] <= 'Z')
            name[i] += 'a' - 'A';
        if (!strcmp(name, "kernel32.dll") || !strcmp(name, "kernel32"))
          result = 0x60000000;
        else if (!strcmp(name, "user32.dll") || !strcmp(name, "user32"))
          result = 0x61000000;
      } else if (first == 0x60000000) {
        uint32_t requested = bw_warden_cpu_import_id("KERNEL32.dll", name);
        if (requested) {
          result = dynamic_import(requested);
          if (!result) {
            memcpy(unavailable_import, name, strlen(name) + 1);
            outcome = 2;
            dependency = 0xffffffffu;
            x86emu_stop(context);
            return -1;
          }
        } else
          page_error(127); /* ERROR_PROC_NOT_FOUND in this virtual export table. */
      }
    }
  } else if (id <= 2) {
    uint32_t destination = 0;
    if (memory_access(context, context->x86.R_ESP + 4, &destination,
                      X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
    double clock = id == 1 ? bw_clock_unix_ms() : bw_clock_monotonic_ms();
    if (!(clock >= 0) || clock > 9007199254740991.0) {
      outcome = 4;
      return -1;
    }
    uint64_t ticks = id == 1 ? (uint64_t)clock * 10000u + UINT64_C(116444736000000000)
                             : (uint64_t)(clock * 1000);
    uint32_t low = (uint32_t)ticks, high = (uint32_t)(ticks >> 32);
    if (destination > 0xfffffffbu ||
        memory_access(context, destination, &low, X86EMU_MEMIO_W | X86EMU_MEMIO_32) ||
        memory_access(context, destination + 4, &high, X86EMU_MEMIO_W | X86EMU_MEMIO_32))
      return -1;
    result = id == 2 ? 1 : 0;
  } else if (id == 3)
    result = (uint32_t)(uint64_t)bw_clock_monotonic_ms();
  else
    result = 1; /* The virtual process and its single thread each have ID 1. */
  uint32_t return_address = 0;
  if (memory_access(context, context->x86.R_ESP, &return_address, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
    return -1;
  context->x86.R_EAX = result;
  context->x86.R_EIP = context->x86.saved_eip = return_address;
  context->x86.R_ESP += 4 + arguments * 4;
  return 1;
}
static uint32_t allocate_guest(uint32_t size) {
  if (!size || size > sizeof(heap) - 15)
    return 0;
  uint32_t capacity = (size + 15) & ~15u;
  for (uint32_t i = 0; i < 128; i++) {
    GuestAllocation *block = &allocations[i];
    if (!block->active && block->capacity >= capacity) {
      block->active = 1;
      return GUEST_HEAP + block->offset;
    }
  }
  if (capacity > sizeof(heap) - heap_end)
    return 0;
  for (uint32_t i = 0; i < 128; i++) {
    GuestAllocation *block = &allocations[i];
    if (!block->capacity) {
      block->offset = heap_end;
      block->capacity = capacity;
      block->active = 1;
      heap_end += capacity;
      return GUEST_HEAP + block->offset;
    }
  }
  return 0;
}
/* Bind only callbacks independently identified in the selected 12340 image.
 * Guest call addresses dispatch to the actual translated runtime services;
 * these bindings do not provide or attest native executable memory. */
void bw_warden_cpu_bind_client(uint32_t kind, uint32_t rva) {
  if (kind == 1 && rva == 0x00419210)
    client_lua = 0x00400000u + rva;
  if (kind == 2 && rva == 0x0046ae20)
    client_counter = 0x00400000u + rva;
}
static int translated_client_service(x86emu_t *context, uint32_t address) {
  if (address == client_lua && client_lua) {
    if (++lua_calls > 16) {
      outcome = 5;
      x86emu_stop(context);
      return -1;
    }
    uint32_t pointer = 0;
    uint8_t source[4096];
    if (memory_access(context, context->x86.R_ESP + 4, &pointer, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
    uint32_t length = 0;
    while (length < sizeof(source)) {
      uint32_t value = 0;
      if (pointer > 0xffffffffu - length ||
          memory_access(context, pointer + length, &value, X86EMU_MEMIO_R | X86EMU_MEMIO_8)) {
        scrub(source, sizeof(source));
        return -1;
      }
      source[length] = (uint8_t)value;
      if (!value)
        break;
      length++;
    }
    uint32_t status =
        length < sizeof(source) ? bw_warden_lua_execute((uint32_t)(uintptr_t)source, length) : 1;
    scrub(source, sizeof(source));
    if (status) {
      outcome = 3;
      dependency = 7;
      x86emu_stop(context);
      return -1;
    }
    context->x86.R_EAX = 0; /* FrameScript::Execute nesting depth after returning. */
  } else if (address == client_counter && client_counter) {
    double clock = bw_clock_monotonic_ms();
    if (!(clock >= 0) || clock > 9007199254740991.0) {
      outcome = 4;
      x86emu_stop(context);
      return -1;
    }
    context->x86.R_EAX = (uint32_t)(uint64_t)clock;
  } else
    return 0;
  uint32_t return_address = 0;
  if (memory_access(context, context->x86.R_ESP, &return_address, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
    return -1;
  context->x86.R_EIP = context->x86.saved_eip = return_address;
  context->x86.R_ESP += 4; /* Both callbacks use caller-owned argument cleanup. */
  return 1;
}
static int client_service(x86emu_t *context, uint32_t service) {
  static const uint8_t counts[7] = {2, 2, 3, 1, 1, 2, 2};
  if (service == 1 || service == 2) {
    outcome = 3;
    x86emu_stop(context);
    return -1;
  }
  uint32_t a[3] = {0}, result = 0;
  for (uint32_t i = 0; i < counts[service]; i++)
    if (memory_access(context, context->x86.R_ESP + 4 + i * 4, &a[i],
                      X86EMU_MEMIO_R | X86EMU_MEMIO_32))
      return -1;
  if (service == 3)
    result = allocate_guest(a[0]);
  else if (service == 4 && a[0]) {
    uint32_t found = 0;
    for (uint32_t i = 0; i < 128; i++) {
      GuestAllocation *block = &allocations[i];
      if (block->active && GUEST_HEAP + block->offset == a[0]) {
        scrub(heap + block->offset, block->capacity);
        block->active = 0;
        found = 1;
        break;
      }
    }
    if (!found) {
      outcome = 4;
      x86emu_stop(context);
      return -1;
    }
  } else if (service == 0 || service == 5) {
    uint32_t capacity = service == 0 ? sizeof(sent_packet) : sizeof(cipher_data);
    uint8_t *destination = service == 0 ? sent_packet : cipher_data;
    if (!a[1] || a[1] > capacity || (service == 0 && sent_size)) {
      outcome = 4;
      x86emu_stop(context);
      return -1;
    }
    for (uint32_t i = 0; i < a[1]; i++) {
      uint32_t value = 0;
      if (a[0] > 0xffffffffu - i ||
          memory_access(context, a[0] + i, &value, X86EMU_MEMIO_R | X86EMU_MEMIO_8))
        return -1;
      destination[i] = (uint8_t)value;
    }
    if (service == 0)
      sent_size = a[1];
    else
      cipher_size = a[1];
  } else if (service == 6 && cipher_size) {
    for (uint32_t i = 0; i < cipher_size; i++) {
      uint32_t value = cipher_data[i];
      if (a[0] > 0xffffffffu - i ||
          memory_access(context, a[0] + i, &value, X86EMU_MEMIO_W | X86EMU_MEMIO_8))
        return -1;
    }
    if (a[1] && memory_access(context, a[1], &cipher_size, X86EMU_MEMIO_W | X86EMU_MEMIO_32))
      return -1;
    result = 1;
  }
  uint32_t return_address = 0;
  if (memory_access(context, context->x86.R_ESP, &return_address, X86EMU_MEMIO_R | X86EMU_MEMIO_32))
    return -1;
  context->x86.R_EAX = result;
  context->x86.R_EIP = context->x86.saved_eip = return_address;
  context->x86.R_ESP += 4 + counts[service] * 4;
  return 1;
}
/* ARPL adjusts a 16-bit selector's RPL and changes only ZF. The observed
 * Warden form uses registers; memory/address-override forms remain unsupported. */
static int adjust_privilege(x86emu_t *context) {
  uint32_t pc = context->x86.R_EIP;
  if (pc < GUEST_IMAGE || pc - GUEST_IMAGE >= image_size || image_size - (pc - GUEST_IMAGE) < 2)
    return 0;
  uint32_t at = pc - GUEST_IMAGE;
  if (image[at] != 0x63 || (image[at + 1] >> 6) != 3)
    return 0;
  if (!(context->x86.R_CR0 & 1) || (context->x86.R_EFLG & (1u << 17))) {
    outcome = 6;
    return -1;
  }
  uint32_t modrm = image[at + 1], *destination = guest_register(context, modrm & 7);
  uint32_t source = *guest_register(context, (modrm >> 3) & 7) & 3;
  if ((*destination & 3) < source) {
    *destination = (*destination & ~3u) | source;
    context->x86.R_EFLG |= 64;
  } else
    context->x86.R_EFLG &= ~64u;
  context->x86.R_EIP = context->x86.saved_eip = pc + 2;
  context->x86.R_TSC++;
  return 1;
}
static int instruction(x86emu_t *context) {
  uint32_t pc;
  for (;;) {
    pc = context->x86.R_EIP;
    if (pc == GUEST_RETURN) {
      outcome = 1;
      return 1;
    }
    if ((client_lua && pc == client_lua) || (client_counter && pc == client_counter)) {
      if (translated_client_service(context, pc) < 0)
        return 1;
    } else if (pc >= GUEST_IMPORT && pc < GUEST_IMPORT + 128 * 16 && !(pc & 15)) {
      dependency = (pc - GUEST_IMPORT) / 16;
      if (!import_types[dependency]) {
        outcome = 2;
        return 1;
      }
      if (process_service(context, import_types[dependency]) < 0)
        return 1;
    } else if (pc >= GUEST_SERVICE && pc < GUEST_SERVICE + 7 * 16 && !(pc & 15)) {
      dependency = (pc - GUEST_SERVICE) / 16;
      if (client_service(context, dependency) < 0)
        return 1;
    } else
      break;
    if (++steps > 100000) {
      outcome = 5;
      return 1;
    }
  }
  for (;;) {
    uint32_t before = context->x86.R_EIP;
    if (before >= GUEST_IMAGE && before - GUEST_IMAGE < image_size &&
        !(image_permissions[(before - GUEST_IMAGE) / 4096] & 4)) {
      outcome = 4;
      return 1;
    }
    int exchanged = atomic_instruction(context, context->x86.R_EIP);
    if (!exchanged)
      exchanged = adjust_privilege(context);
    if (!exchanged)
      exchanged = mmx_instruction(context);
    if (exchanged < 0)
      return 1;
    if (!exchanged)
      break;
    pc = context->x86.R_EIP;
    if (pc >= GUEST_IMAGE && pc - GUEST_IMAGE < image_size) {
      uint32_t at = pc - GUEST_IMAGE, prefixes = 0, repeated = 0, address16 = 0;
      while (at < image_size && prefixes++ < 15) {
        uint8_t byte = image[at++];
        if (byte == 0xf2 || byte == 0xf3)
          repeated = 1;
        if (byte == 0x67)
          address16 = 1;
        if (repeated && !address16 &&
            ((byte >= 0x6c && byte <= 0x6f) || (byte >= 0xa4 && byte <= 0xa7) ||
             (byte >= 0xaa && byte <= 0xaf)) &&
            context->x86.R_ECX > 65536) {
          outcome = 5;
          return 1;
        }
        if (byte != 0xf2 && byte != 0xf3 && byte != 0x66 && byte != 0x67 && byte != 0xf0 &&
            byte != 0x26 && byte != 0x2e && byte != 0x36 && byte != 0x3e && byte != 0x64 &&
            byte != 0x65)
          break;
      }
      if (prefixes >= 15) {
        outcome = 6;
        return 1;
      }
    }
    if (++steps > 100000) {
      outcome = 5;
      return 1;
    }
  }
  if (++steps > 100000) {
    outcome = 5;
    return 1;
  }
  return 0;
}
static uint32_t opcode_at(uint32_t pc) {
  if (pc < GUEST_IMAGE || pc - GUEST_IMAGE >= image_size)
    return 0;
  uint32_t at = pc - GUEST_IMAGE;
  for (uint32_t i = 0; i < 15 && at < image_size; i++) {
    uint8_t byte = image[at++];
    if (byte == 0xf0 || byte == 0xf2 || byte == 0xf3 || byte == 0x66 || byte == 0x67 ||
        byte == 0x26 || byte == 0x2e || byte == 0x36 || byte == 0x3e || byte == 0x64 ||
        byte == 0x65)
      continue;
    return byte == 0x0f && at < image_size ? 0x0f00u | image[at] : byte;
  }
  return 0;
}
static int interrupt(x86emu_t *context, uint8_t number, unsigned type) {
  (void)type;
  fault_vector = number;
  fault_opcode = opcode_at(context->x86.saved_eip);
  outcome = 6;
  x86emu_stop(context);
  return 1;
}
static void put32(uint8_t *out, uint32_t value) {
  for (uint32_t i = 0; i < 4; i++)
    out[i] = (uint8_t)(value >> (8 * i));
}
/* Guest NT clock page. Values come from actual host clocks, not integrity
 * expectations; unrelated Windows kernel state is not modeled by this page. */
static void shared_time(uint32_t offset, uint64_t value) {
  put32(shared_clock + offset, (uint32_t)value);
  put32(shared_clock + offset + 4, (uint32_t)(value >> 32));
  put32(shared_clock + offset + 8, (uint32_t)(value >> 32));
}
static int refresh_shared_clock(void) {
  double monotonic = bw_clock_monotonic_ms(), utc = bw_clock_unix_ms();
  if (!(monotonic >= 0) || monotonic > 9007199254740991.0 || !(utc >= 0) ||
      utc > 9007199254740991.0)
    return 0;
  put32(shared_clock, (uint32_t)(uint64_t)monotonic);
  put32(shared_clock + 4, 1u << 24);
  shared_time(8, (uint64_t)(monotonic * 10000));
  shared_time(0x14, (uint64_t)utc * 10000u + UINT64_C(116444736000000000));
  shared_time(0x320, (uint64_t)monotonic);
  put32(shared_clock + 0x264, 1); /* Declared NT 5.1 workstation guest ABI. */
  shared_clock[0x268] = 1;
  put32(shared_clock + 0x26c, 5);
  put32(shared_clock + 0x270, 1);
  return 1;
}
/* Calls the first signed export using a guest thiscall services object.
 * 1=returned, 2=Windows import, 3=client service, 4=memory/IO violation,
 * 5=instruction budget, 6=unsupported instruction/interrupt. None means Warden passed. */
uint32_t bw_warden_cpu_run(uint8_t *bytes, uint32_t size, uint32_t entry, const uint32_t *imports,
                           uint32_t count, const uint8_t *sections, uint32_t section_count) {
  bw_warden_cpu_clear();
  if (count > 128 || (count && !imports))
    return 4;
  if (count)
    memcpy(import_types, imports, count * sizeof(uint32_t));
  import_count = count;
  image = bytes;
  image_size = size;
  if (!size || size > 131072 || entry < GUEST_IMAGE || entry - GUEST_IMAGE >= size)
    return 4;
  if (!sections || !section_count || section_count > 32)
    return 4;
  memset(image_permissions, 3, sizeof(image_permissions));
  for (uint32_t i = 0; i < section_count; i++) {
    uint32_t start = 0, length = 0, protection = 0;
    for (uint32_t j = 0; j < 4; j++) {
      start |= (uint32_t)sections[i * 12 + j] << (j * 8);
      length |= (uint32_t)sections[i * 12 + 4 + j] << (j * 8);
      protection |= (uint32_t)sections[i * 12 + 8 + j] << (j * 8);
    }
    if (!length || start >= size || length > size - start)
      return 4;
    uint8_t permissions = 0;
    if (protection & (2 | 4 | 8 | 0x20 | 0x40 | 0x80))
      permissions |= 1;
    if (protection & (4 | 8 | 0x40 | 0x80))
      permissions |= 2;
    if (protection & 0xf0)
      permissions |= 4;
    if (protection & 0x100)
      permissions = 0;
    for (uint32_t page = start / 4096; page <= (start + length - 1) / 4096; page++)
      image_permissions[page] = permissions;
  }
  if (bw_warden_memory_map(&memory, GUEST_IMAGE, image,
                           (image_size + BW_MEMORY_PAGE_SIZE - 1) & ~(BW_MEMORY_PAGE_SIZE - 1), 7,
                           image_permissions) ||
      bw_warden_memory_map(&memory, GUEST_STACK, stack, sizeof(stack), 3, NULL) ||
      bw_warden_memory_map(&memory, GUEST_TEB, teb, sizeof(teb), 3, NULL) ||
      bw_warden_memory_map(&memory, GUEST_HEAP, heap, sizeof(heap), 3, NULL) ||
      bw_warden_memory_map(&memory, GUEST_SHARED, shared_clock, sizeof(shared_clock),
                           BW_MEMORY_READ, NULL) ||
      bw_client_image_map(&memory)) {
    outcome = 4;
    return outcome;
  }
  for (uint32_t i = 0; i < memory.count; i++)
    if (memory.regions[i].base == GUEST_SHARED)
      memory.regions[i].type = 0x40000; /* MEM_MAPPED, kernel-updated guest clock view. */
  if (!refresh_shared_clock()) {
    outcome = 4;
    return outcome;
  }
  cpu.x86.msr = msrs;
  cpu.x86.msr_perm = msr_permissions;
  cpu.memio = memory_access;
  cpu.code_check = instruction;
  cpu.intr = interrupt;
  /* Flat 32-bit protected-mode user context, as required by native PE code. */
  cpu.x86.R_CR0 = 1;
  cpu.x86.R_CS = 0x1b;
  cpu.x86.R_DS = cpu.x86.R_ES = cpu.x86.R_SS = 0x23;
  cpu.x86.R_FS = 0x3b;
  cpu.x86.R_CS_ACC = 0xcfb;
  cpu.x86.R_DS_ACC = cpu.x86.R_ES_ACC = cpu.x86.R_SS_ACC = cpu.x86.R_FS_ACC = 0xcf3;
  cpu.x86.R_CS_LIMIT = cpu.x86.R_SS_LIMIT = cpu.x86.R_DS_LIMIT = cpu.x86.R_ES_LIMIT =
      cpu.x86.R_FS_LIMIT = cpu.x86.R_GS_LIMIT = 0xffffffff;
  cpu.x86.R_FS_BASE = GUEST_TEB;
  cpu.x86.R_EIP = entry;
  cpu.x86.R_EFLG = 2;
  cpu.x86.R_ESP = GUEST_STACK + sizeof(stack) - 16;
  cpu.x86.R_ECX = GUEST_STACK;
  put32(stack, GUEST_STACK + 16);
  for (uint32_t i = 0; i < 7; i++)
    put32(stack + 16 + i * 4, GUEST_SERVICE + i * 16);
  put32(teb, 0xffffffff);
  put32(teb + 24, GUEST_TEB);
  page_error(0);
  put32(stack + sizeof(stack) - 16, GUEST_RETURN);
  x86emu_run(&cpu, 0);
  if (!outcome)
    outcome = 6;
  if (outcome == 6 && !fault_opcode)
    fault_opcode = opcode_at(cpu.x86.saved_eip);
  return outcome;
}
/* Internal provider probe, not a raw-memory export to JavaScript. */
uint32_t bw_warden_cpu_memory_probe(uint32_t address, uint32_t length) {
  uint8_t bytes[255];
  if (!length || length > sizeof(bytes))
    return BW_MEMORY_INVALID;
  uint32_t status = bw_warden_memory_transfer(&memory, address, bytes, length, BW_MEMORY_READ);
  scrub(bytes, sizeof(bytes));
  return status;
}
/* Copies a mapped span for a local check reply. Not exported to JavaScript. */
uint32_t bw_warden_cpu_memory_copy(uint32_t address, uint8_t *buffer, uint32_t length) {
  if (!buffer || !length || length > 255)
    return BW_MEMORY_INVALID;
  return bw_warden_memory_transfer(&memory, address, buffer, length, BW_MEMORY_READ);
}
uint32_t bw_warden_cpu_memory_fault(uint32_t field) {
  switch (field) {
  case 0:
    return memory.fault.address;
  case 1:
    return memory.fault.length;
  case 2:
    return memory.fault.access;
  case 3:
    return memory.fault.reason;
  default:
    return 0;
  }
}
uint32_t bw_warden_cpu_import_name(uint8_t *out, uint32_t capacity) {
  uint32_t length = (uint32_t)strlen(unavailable_import);
  if (!length || length >= capacity)
    return 0;
  memcpy(out, unavailable_import, length);
  return length;
}
uint32_t bw_warden_cpu_metric(uint32_t field) {
  if (field == 11)
    return cpu.x86.R_EIP;
  if (field == 12)
    return cpu.x86.saved_eip;
  if (field >= 16 && field < 36)
    return api_calls[field - 16];
  if (field >= 7 && field < 11)
    return bw_warden_cpu_memory_fault(field - 7);
  switch (field) {
  case 0:
    return steps;
  case 1:
    return dependency;
  case 2:
    return cpu.x86.R_EAX;
  case 3:
    return sent_size;
  case 4:
    return cipher_size;
  case 5:
    return fault_opcode;
  case 6:
    return fault_vector;
  default:
    return 0;
  }
}

int bw_warden_cpu_read32(uint32_t address, uint32_t *value) {
  return memory_access(&cpu, address, value, X86EMU_MEMIO_R | X86EMU_MEMIO_32) == 0;
}
uint32_t bw_warden_cpu_scratch(const uint8_t *data, uint32_t size, uint32_t offset) {
  if (offset > 4096 || size > 4096 - offset)
    return 0;
  memcpy(stack + 4096 + offset, data, size);
  return GUEST_STACK + 4096 + offset;
}
void bw_warden_cpu_scratch_clear(void) {
  scrub(stack + 4096, 4096);
}
uint32_t bw_warden_cpu_call(uint32_t entry, uint32_t object, const uint32_t *arguments,
                            uint32_t count) {
  if (count > 3 || entry < GUEST_IMAGE || entry - GUEST_IMAGE >= image_size)
    return 4;
  outcome = dependency = steps = accesses = fault_opcode = lua_calls = 0;
  scrub(api_calls, sizeof(api_calls));
  fault_vector = 0xffffffffu;
  scrub(sent_packet, sizeof(sent_packet));
  sent_size = 0;
  cpu.x86.R_EIP = entry;
  cpu.x86.R_EAX = cpu.x86.R_EDX = cpu.x86.R_EBX = cpu.x86.R_ESI = cpu.x86.R_EDI = cpu.x86.R_EBP = 0;
  cpu.x86.R_ECX = object;
  cpu.x86.R_EFLG = 2;
  uint32_t at = sizeof(stack) - 16 - count * 4;
  cpu.x86.R_ESP = GUEST_STACK + at;
  put32(stack + at, GUEST_RETURN);
  for (uint32_t i = 0; i < count; i++)
    put32(stack + at + 4 + i * 4, arguments[i]);
  x86emu_run(&cpu, 0);
  if (!outcome)
    outcome = 6;
  if (outcome == 6 && !fault_opcode)
    fault_opcode = opcode_at(cpu.x86.saved_eip);
  return outcome;
}
uint32_t bw_warden_cpu_output(uint8_t *out, uint32_t capacity) {
  if (sent_size > capacity)
    return 0;
  memcpy(out, sent_packet, sent_size);
  return sent_size;
}
uint32_t bw_warden_cpu_cipher(uint8_t *out, uint32_t capacity) {
  if (cipher_size > capacity)
    return 0;
  memcpy(out, cipher_data, cipher_size);
  return cipher_size;
}
