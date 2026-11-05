/* Periodic memory headroom report for device and emulator logs. */
#include <malloc.h>
#include <stdio.h>
#include <ppu-lv2.h>
#include <lv2/syscalls.h>

#include "stsv_ps3_platform.h"

typedef struct {
    uint32_t total;
    uint32_t available;
} UserMemory;

/* Defined by the TeaVM runtime and its generated GC. */
extern int32_t sfld_otr_GC_freeMemory;
extern int64_t teavm_gc_availableBytes;

static int32_t query_user_memory(UserMemory *memory)
{
    lv2syscall1(SYSCALL_MEMORY_GET_USER_MEMORY_SIZE, (uint64_t)(uintptr_t)memory);
    return_to_user_prog(int32_t);
}

void stsv_ps3_log_memory(const char *tag)
{
    UserMemory system = { 0, 0 };
    struct mallinfo native = mallinfo();
    query_user_memory(&system);
    fprintf(stderr,
            "[ps3 mem] %s: system free %u/%u KiB, malloc used %u KiB (free in arena %u KiB),"
            " java free %d/%lld KiB\n",
            tag, system.available >> 10, system.total >> 10,
            (unsigned)native.uordblks >> 10, (unsigned)native.fordblks >> 10,
            sfld_otr_GC_freeMemory >> 10, (long long)(teavm_gc_availableBytes >> 10));
}
