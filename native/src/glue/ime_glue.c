/* sceImeDialog on-screen keyboard (Vita only; host_glue.c stubs these).
 *
 * Flow (all driven from Java, VitaInput):
 *   stsv_ime_open()  - starts the dialog over the running GL frame loop
 *   stsv_ime_poll()  - once per frame; 0=running 1=entered 2=canceled/error
 *   stsv_ime_result() - fetch the entered UTF-16 text after poll()==1
 *   stsv_ime_active() - 1 while the dialog is up; stsv_swap_buffers must
 *                       call vglSwapBuffers(GL_TRUE) so the common dialog
 *                       renders on top of the game frame.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>

#define STSV_IME_MAX 128

static SceWChar16 g_title[SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1];
static SceWChar16 g_initial[STSV_IME_MAX + 1];
static SceWChar16 g_input[STSV_IME_MAX + 1];
static SceWChar16 g_result[STSV_IME_MAX + 1];
static int g_active;

static void copy_u16(SceWChar16 *dst, const uint16_t *src, int cap) {
    int i = 0;
    if (src != NULL) {
        for (; i < cap && src[i] != 0; i++) {
            dst[i] = src[i];
        }
    }
    dst[i] = 0;
}

/* title/initial: NUL-terminated UTF-16LE (Java chars). Returns 0 on success. */
int32_t stsv_ime_open(void *title_u16, void *initial_u16, int32_t max_len) {
    if (g_active) {
        return -1;
    }
    if (max_len <= 0 || max_len > STSV_IME_MAX) {
        max_len = STSV_IME_MAX;
    }
    copy_u16(g_title, (const uint16_t *) title_u16, SCE_IME_DIALOG_MAX_TITLE_LENGTH);
    copy_u16(g_initial, (const uint16_t *) initial_u16, max_len);
    memset(g_input, 0, sizeof(g_input));
    memset(g_result, 0, sizeof(g_result));

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0x0001FFFF;
    param.languagesForced = SCE_TRUE;
    param.type = SCE_IME_TYPE_DEFAULT;
    param.option = 0;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.title = g_title;
    param.maxTextLength = (SceUInt32) max_len;
    param.initialText = g_initial;
    param.inputTextBuffer = g_input;

    SceInt32 rc = sceImeDialogInit(&param);
    if (rc < 0) {
        fprintf(stderr, "[ime] sceImeDialogInit failed 0x%08x\n", (unsigned) rc);
        return rc;
    }
    g_active = 1;
    fprintf(stderr, "[ime] dialog opened (max %d chars)\n", (int) max_len);
    return 0;
}

int32_t stsv_ime_poll(void) {
    if (!g_active) {
        return 0;
    }
    SceCommonDialogStatus status = sceImeDialogGetStatus();
    if (status != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        return 0;
    }
    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);
    int entered = result.button == SCE_IME_DIALOG_BUTTON_ENTER;
    if (entered) {
        memcpy(g_result, g_input, sizeof(g_result));
    }
    sceImeDialogTerm();
    g_active = 0;
    fprintf(stderr, "[ime] dialog closed (%s)\n", entered ? "entered" : "canceled");
    return entered ? 1 : 2;
}

/* Copy the UTF-16 result into out (cap chars incl. terminator); returns length. */
int32_t stsv_ime_result(void *out, int32_t cap) {
    uint16_t *dst = (uint16_t *) out;
    int i = 0;
    for (; i < cap - 1 && g_result[i] != 0; i++) {
        dst[i] = g_result[i];
    }
    dst[i] = 0;
    return i;
}

int32_t stsv_ime_active(void) {
    return g_active;
}
