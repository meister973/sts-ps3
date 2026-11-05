#ifndef STSV_PS3_PLATFORM_H
#define STSV_PS3_PLATFORM_H

#include <stdint.h>
#include <time.h>

#define STSV_PS3_USRDIR "/dev_hdd0/game/STSP00001/USRDIR"
#define STSV_PS3_LOG_PATH "/dev_hdd0/tmp/stsps3.log"

void stsv_ps3_system_init(void);
void stsv_ps3_pump_callbacks(void);
int stsv_ps3_exit_requested(void);
void stsv_input_init(void);
void stsv_ps3_input_poll(void);
void stsv_ps3_input_next_frame(void);
int stsv_ps3_pad_script_active(void);
void stsv_ps3_log_memory(const char *tag);
void stsv_audio_init(void);
void stsv_ps3_ime_event(uint64_t status);
int64_t stsv_ps3_monotonic_us(void);
int64_t stsv_ps3_nanotime(void);

/* TeaVM's PSP runtime API names, implemented using PSL1GHT. */
int sceKernelDelayThread(unsigned int microseconds);
time_t sceKernelLibcTime(time_t *result);
uint64_t sceKernelGetProcessTimeWide(void);

#endif
