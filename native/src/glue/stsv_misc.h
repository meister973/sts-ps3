/* Misc glue: image decoding (stb_image) and raw memory ops.
 * Implemented in misc_glue.c; shared by host harness and Vita. */

#ifndef STSV_MISC_H
#define STSV_MISC_H

#include <stdint.h>

/* Decode PNG/JPG to RGBA8888. outDims = int32[2] receiving width,height.
 * Returns malloc'd pixel pointer (free with stsv_img_free) or NULL. */
void *stsv_img_load(const void *data, int32_t len, int32_t *outDims);
void stsv_img_free(void *pixels);

void stsv_memcpy(void *dst, const void *src, int32_t bytes);
void stsv_memset(void *dst, int32_t value, int32_t bytes);

#endif
