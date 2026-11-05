#include "rsx_ring.h"

#include <stdio.h>
#include <unistd.h>

#include <ppu-asm.h>
#include <rsx/gcm_sys.h>

#define RSX_JUMP_METHOD 0x20000000u

/* More than any run of commands between two rsx_ring_make_room calls. */
#define RING_MARGIN_BYTES 0x10000

static u32 *ring_begin;
static u32 ring_begin_offset;

/* Drain first, so the RSX never reads queued commands with PUT behind GET. Then the jump
 * goes in and PUT moves to the ring start: GET takes the jump and stops where it equals
 * PUT, and only then is it safe to overwrite anything. */
static void jump_to_ring_start(gcmContextData *context)
{
    gcmControlRegister *control = gcmGetControlRegister();
    u32 current_offset;

    rsxAddressToOffset(context->current, &current_offset);
    __asm__ volatile("sync");
    control->put = current_offset;
    while (control->get != current_offset)
        usleep(30);

    *context->current = RSX_JUMP_METHOD | ring_begin_offset;
    __asm__ volatile("sync");
    control->put = ring_begin_offset;
    while (control->get != ring_begin_offset)
        usleep(30);

    context->current = ring_begin;
}

/* Reaching this means the margin was too small, and the commands around it are lost:
 * librsx's asm call has no memory clobber, so its caller keeps a stale `current`. */
static s32 on_ring_overrun(gcmContextData *context, u32 word_count)
{
    (void)context;
    printf("rsx_ring: FATAL overrun, %u words requested past the margin\n", word_count);
    fflush(stdout);
    return -1;
}

void rsx_ring_make_room(gcmContextData *context)
{
    if ((char *)context->end - (char *)context->current < RING_MARGIN_BYTES)
        jump_to_ring_start(context);
}

s32 rsx_ring_attach(gcmContextData *context, void *memory, u32 size_bytes)
{
    s32 status = rsxAddressToOffset(memory, &ring_begin_offset);

    if (status != 0)
        return status;

    ring_begin = memory;
    jump_to_ring_start(context);

    context->begin = ring_begin;
    context->current = ring_begin;
    context->end = (u32 *)((char *)memory + size_bytes - 16);
    context->callback = (gcmContextCallback)__get_opd32(on_ring_overrun);
    return 0;
}
