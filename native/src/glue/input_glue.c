/* SceCtrl / SceTouch polling for VitaInput (via VitaNatives @Imports). */

#include <psp2/ctrl.h>
#include <psp2/touch.h>

static SceTouchData touch;

/* Must run once at startup: sceTouchPeek returns 0 touches until the front
 * panel is sampling, and the analog sticks read 128 (centre) until ANALOG mode
 * is selected. Without this, VitaInput sees no touch and no stick input. */
void stsv_input_init(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
}

int stsv_ctrl_buttons(void) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    return (int) pad.buttons;
}

float stsv_ctrl_axis(int axis) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned char raw;
    switch (axis) {
        case 0: raw = pad.lx; break;
        case 1: raw = pad.ly; break;
        case 2: raw = pad.rx; break;
        default: raw = pad.ry; break;
    }
    return (raw - 128) / 128.0f;
}

int stsv_touch_count(void) {
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    return touch.reportNum;
}

/* Touch panel coords are 1920x1088 regardless of the 960x544 screen. */
int stsv_touch_x(int finger) {
    if (finger >= touch.reportNum) return 0;
    return touch.report[finger].x / 2;
}

int stsv_touch_y(int finger) {
    if (finger >= touch.reportNum) return 0;
    return touch.report[finger].y / 2;
}
