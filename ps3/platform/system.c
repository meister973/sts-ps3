#include <stdio.h>
#include <stdlib.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>

#include "stsv_ps3_platform.h"

static int exit_requested;
static int initialized;

static void system_event(u64 status, u64 parameter, void *user)
{
    (void)parameter;
    (void)user;
    if (status == SYSUTIL_EXIT_GAME)
        exit_requested = 1;
    stsv_ps3_ime_event(status);
}

void stsv_ps3_system_init(void)
{
    if (initialized)
        return;
    int result = sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, system_event, NULL);
    fprintf(stderr, "[ps3] sysutil callback: 0x%08x\n", (unsigned)result);
    initialized = result == 0;
}

int stsv_ps3_exit_requested(void)
{
    return exit_requested;
}

void stsv_ps3_pump_callbacks(void)
{
    sysUtilCheckCallback();
    if (exit_requested) {
        fprintf(stderr, "[ps3] XMB exit requested\n");
        fflush(NULL);
        exit(0);
    }
}

int64_t stsv_ps3_monotonic_us(void)
{
    return (int64_t)sysGetSystemTime();
}

int64_t stsv_ps3_nanotime(void)
{
    return stsv_ps3_monotonic_us() * INT64_C(1000);
}

int sceKernelDelayThread(unsigned int microseconds)
{
    return sysUsleep(microseconds);
}

uint64_t sceKernelGetProcessTimeWide(void)
{
    return (uint64_t)stsv_ps3_monotonic_us();
}

time_t sceKernelLibcTime(time_t *result)
{
    return time(result);
}
