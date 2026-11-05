#ifndef RSX_RING_H
#define RSX_RING_H

#include <rsx/rsx.h>

/*
 * Replaces the firmware's command buffer handling with a single ring inside the
 * IO-mapped host memory. Librsx cannot wrap safely in the middle of a command, so callers
 * make room between commands: call rsx_ring_make_room before each group of them.
 */
s32 rsx_ring_attach(gcmContextData *context, void *memory, u32 size_bytes);
void rsx_ring_make_room(gcmContextData *context);

#endif
