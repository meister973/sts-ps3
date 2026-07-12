/* Functions @Import-ed by the Java backend (see VitaNatives.java).
 * Keep names in sync — TeaVM resolves them at native link time. */

#include "stsvita_glue.h"

#include <vitaGL.h>
#include <stdio.h>

static int vsync_on = 1;

void stsv_gfx_init(void) {
    /* vitaGL is initialized by main.c before the Java world boots. */
}

void stsv_swap_buffers(void) {
    /* Drain GL errors once per frame so a draw/shader/FBO failure during
     * combat names itself in the log instead of silently rendering blank. */
    GLenum err;
    static int logged;
    while ((err = glGetError()) != GL_NO_ERROR && logged < 200) {
        fprintf(stderr, "[DIAG glerr] frame GL error 0x%04x\n", (unsigned) err);
        logged++;
    }
    /* While the sceImeDialog keyboard is up, the common dialog layer must be
     * composited over the frame (ime_glue.c). */
    vglSwapBuffers(stsv_ime_active() ? GL_TRUE : GL_FALSE);
}

void stsv_set_vsync(int on) {
    vsync_on = on;
}

int32_t stsv_is_host_harness(void) {
    return 0;
}
