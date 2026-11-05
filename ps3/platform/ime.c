/* PSL1GHT asynchronous OSK. UTF-16 buffers contain native-endian Java chars. */
#include <stdio.h>
#include <string.h>
#include <sys/memory.h>
#include <sysutil/osk.h>
#include <sysutil/sysutil.h>

#include "stsv_ps3_platform.h"
#include "stsvita_glue.h"

#define IME_MAX 128
#define OSK_CONTAINER_BYTES (8 * 1024 * 1024)
static uint16_t title[IME_MAX + 1];
static uint16_t initial[IME_MAX + 1];
static uint16_t result_text[IME_MAX + 1];
static oskCallbackReturnParam result;
static sys_mem_container_t container;
static int active;
static int completion;
static int unloading;

static void copy_u16(uint16_t *dst, const uint16_t *src, int count)
{
    int i = 0;
    if (src != NULL)
        while (i < count && src[i] != 0) {
            dst[i] = src[i];
            ++i;
        }
    dst[i] = 0;
}

int32_t stsv_ime_open(void *title_u16, void *initial_u16, int32_t max_len)
{
    oskParam parameters;
    oskInputFieldInfo field;
    if (active)
        return -1;
    if (max_len <= 0 || max_len > IME_MAX)
        max_len = IME_MAX;
    int rc = sysMemContainerCreate(&container, OSK_CONTAINER_BYTES);
    if (rc != 0) {
        fprintf(stderr, "[ps3 ime] memory container failed 0x%08x\n", (unsigned)rc);
        return rc;
    }
    copy_u16(title, title_u16, IME_MAX);
    copy_u16(initial, initial_u16, max_len);
    memset(result_text, 0, sizeof(result_text));
    memset(&result, 0, sizeof(result));
    result.res = OSK_CANCELED;
    result.len = max_len;
    result.str = result_text;
    memset(&parameters, 0, sizeof(parameters));
    parameters.allowedPanels = OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_NUMERAL;
    parameters.firstViewPanel = OSK_PANEL_TYPE_ALPHABET;
    parameters.prohibitFlags = OSK_PROHIBIT_RETURN;
    field.message = title;
    field.startText = initial;
    field.maxLength = max_len;
    oskSetInitialInputDevice(OSK_DEVICE_PAD);
    oskSetKeyLayoutOption(OSK_FULLKEY_PANEL);
    oskSetLayoutMode(OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER | OSK_LAYOUTMODE_VERTICAL_ALIGN_CENTER);
    rc = oskLoadAsync(container, &parameters, &field);
    if (rc != 0) {
        sysMemContainerDestroy(container);
        fprintf(stderr, "[ps3 ime] oskLoadAsync failed 0x%08x\n", (unsigned)rc);
        return rc;
    }
    active = 1;
    completion = 0;
    unloading = 0;
    fprintf(stderr, "[ps3 ime] OSK opened (max %d chars)\n", (int)max_len);
    return 0;
}

void stsv_ps3_ime_event(uint64_t status)
{
    if (!active)
        return;
    if (status == SYSUTIL_OSK_DONE && !unloading) {
        int rc = oskUnloadAsync(&result);
        if (rc != 0) {
            /* Do not free the container while the system may still use it. */
            fprintf(stderr, "[ps3 ime] oskUnloadAsync failed 0x%08x\n", (unsigned)rc);
            oskAbort();
        } else {
            unloading = 1;
        }
    } else if (status == SYSUTIL_OSK_UNLOADED) {
        result_text[IME_MAX] = 0;
        completion = result.res == OSK_OK ? 1 : 2;
        active = 0;
        unloading = 0;
        sysMemContainerDestroy(container);
        fprintf(stderr, "[ps3 ime] OSK closed (%s)\n", completion == 1 ? "entered" : "canceled");
    }
}

int32_t stsv_ime_poll(void)
{
    stsv_ps3_pump_callbacks();
    int status = completion;
    completion = 0;
    return status;
}

int32_t stsv_ime_result(void *out_u16, int32_t cap_chars)
{
    uint16_t *out = out_u16;
    int length = 0;
    if (out == NULL || cap_chars <= 0)
        return 0;
    while (length < cap_chars - 1 && length < IME_MAX && result_text[length] != 0) {
        out[length] = result_text[length];
        ++length;
    }
    out[length] = 0;
    return length;
}

int32_t stsv_ime_active(void)
{
    return active;
}
