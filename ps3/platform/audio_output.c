/* PS3 audio port output. The port takes 48 kHz float blocks and a missed block
 * clicks, so one thread only copies finished blocks into the port and a second
 * one mixes ahead into a small ring (same split as the Balatro PS3 port). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <audio/audio.h>
#include <sys/event_queue.h>
#include <sys/thread.h>

#include "audio_output.h"

#define CHANNELS 2
#define BLOCK_FRAMES AUDIO_BLOCK_SAMPLES
#define BLOCK_SAMPLES (BLOCK_FRAMES * CHANNELS)
#define BLOCK_BYTES (BLOCK_SAMPLES * sizeof(float))
#define PORT_BLOCKS 8
#define RING_BLOCKS 16
#define THREAD_STACK (256 * 1024)
#define FEEDER_PRIORITY 900
#define MIXER_PRIORITY 1000
#define WAIT_MICROSECONDS 20000

static float ring[RING_BLOCKS][BLOCK_SAMPLES] __attribute__((aligned(16)));
static volatile unsigned ring_read, ring_write;
static audioPortConfig port_config;
static sys_event_queue_t notify_queue;
static AudioMixFunction mix_block;
static unsigned underruns;

/* lwsync orders this thread's ring data against the index the other thread reads. */
#define BARRIER() __asm__ volatile("lwsync" ::: "memory")

static void clamp_block(float *block)
{
    for (int i = 0; i < BLOCK_SAMPLES; i++) {
        if (block[i] > 1.0f)
            block[i] = 1.0f;
        if (block[i] < -1.0f)
            block[i] = -1.0f;
    }
}

static void mixer_thread(void *argument)
{
    (void)argument;
    for (;;) {
        if (ring_write - ring_read >= RING_BLOCKS) {
            usleep(1000);
            continue;
        }
        BARRIER();
        float *block = ring[ring_write % RING_BLOCKS];
        memset(block, 0, BLOCK_BYTES);
        mix_block(block, BLOCK_FRAMES);
        clamp_block(block);
        BARRIER();
        ring_write++;
    }
}

/* Only copies: decoding here would miss the 5.33 ms block deadline. */
static void feeder_thread(void *argument)
{
    (void)argument;
    sys_event_t event;
    float *port = (float *)(unsigned long)port_config.audioDataStart;
    for (;;) {
        if (sysEventQueueReceive(notify_queue, &event, WAIT_MICROSECONDS) != 0)
            continue;
        unsigned long long playing = *(volatile unsigned long long *)(unsigned long)port_config.readIndex;
        float *target = port + ((playing + 1) % PORT_BLOCKS) * BLOCK_SAMPLES;
        if (ring_write == ring_read) {
            memset(target, 0, BLOCK_BYTES);
            if ((++underruns & 255) == 1)
                fprintf(stderr, "[ps3 audio] mixer underrun (%u so far)\n", underruns);
            continue;
        }
        BARRIER();
        memcpy(target, ring[ring_read % RING_BLOCKS], BLOCK_BYTES);
        BARRIER();
        ring_read++;
    }
}

int audio_output_start(AudioMixFunction mix)
{
    audioPortParam parameters;
    sys_ipc_key_t notify_key;
    sys_ppu_thread_t mixer_id, feeder_id;
    u32 port_number;

    mix_block = mix;
    if (audioInit() != 0)
        return -1;
    parameters.numChannels = AUDIO_PORT_2CH;
    parameters.numBlocks = AUDIO_BLOCK_8;
    parameters.attrib = 0;
    parameters.level = 1.0f;
    if (audioPortOpen(&parameters, &port_number) != 0)
        return -2;
    if (audioGetPortConfig(port_number, &port_config) != 0)
        return -3;
    if (audioCreateNotifyEventQueue(&notify_queue, &notify_key) != 0)
        return -4;
    if (audioSetNotifyEventQueue(notify_key) != 0)
        return -5;
    if (sysThreadCreate(&mixer_id, mixer_thread, NULL, MIXER_PRIORITY, THREAD_STACK, 0, "sts mixer") != 0)
        return -6;
    if (sysThreadCreate(&feeder_id, feeder_thread, NULL, FEEDER_PRIORITY, THREAD_STACK, 0, "sts feeder") != 0)
        return -7;
    if (audioPortStart(port_number) != 0)
        return -8;
    return 0;
}
