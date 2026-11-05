/* Match VitaInput's SceCtrl bit contract; keep native PS3 pad handling here. */
#include <stdio.h>
#include <string.h>
#include <io/pad.h>

#include "stsv_ps3_platform.h"
#include "stsvita_glue.h"

#define PORTS 7
#define SCRIPT_MAX_EVENTS 1024
#define SCRIPT_PATH "/dev_hdd0/tmp/sts-pad-script.txt"
typedef struct { unsigned long long frame; unsigned int buttons; } PadEvent;
static PadEvent script[SCRIPT_MAX_EVENTS];
static int script_count;
static int script_next;
static unsigned int script_buttons;
static unsigned long long poll_frame;
static int polled_this_frame;
static padData cached[PORTS];
static padData selected;
static int current_buttons;
static int initialized;

static void read_pad_script(void)
{
    char line[128];
    unsigned int line_number = 0;
    FILE *file = fopen(SCRIPT_PATH, "r");
    if (file == NULL)
        return;
    while (fgets(line, sizeof(line), file) != NULL) {
        PadEvent event;
        char trailing;
        char *start = line;
        ++line_number;
        while (*start == ' ' || *start == '\t')
            ++start;
        if (*start == '#' || *start == '\n' || *start == '\r' || *start == 0)
            continue;
        if (script_count == SCRIPT_MAX_EVENTS ||
            sscanf(start, "%llu %x %c", &event.frame, &event.buttons, &trailing) != 2 ||
            (script_count && event.frame < script[script_count - 1].frame)) {
            fprintf(stderr, "[ps3 input] invalid pad script at line %u; disabled\n", line_number);
            script_count = 0;
            break;
        }
        script[script_count++] = event;
    }
    fclose(file);
    fprintf(stderr, "[ps3 input] diagnostic pad script loaded: %d events\n", script_count);
}

void stsv_input_init(void)
{
    if (initialized)
        return;
    selected.ANA_L_H = selected.ANA_L_V = 128;
    selected.ANA_R_H = selected.ANA_R_V = 128;
    int result = ioPadInit(PORTS);
    fprintf(stderr, "[ps3] ioPadInit: 0x%08x\n", (unsigned)result);
    read_pad_script();
    initialized = result == 0;
}

static int vita_buttons(const padData *pad)
{
    int bits = 0;
    if (pad->BTN_SELECT) bits |= 0x000001;
    if (pad->BTN_START) bits |= 0x000008;
    if (pad->BTN_UP) bits |= 0x000010;
    if (pad->BTN_RIGHT) bits |= 0x000020;
    if (pad->BTN_DOWN) bits |= 0x000040;
    if (pad->BTN_LEFT) bits |= 0x000080;
    if (pad->BTN_L1) bits |= 0x000100;
    if (pad->BTN_R1) bits |= 0x000200;
    if (pad->BTN_TRIANGLE) bits |= 0x001000;
    if (pad->BTN_CIRCLE) bits |= 0x002000;
    if (pad->BTN_CROSS) bits |= 0x004000;
    if (pad->BTN_SQUARE) bits |= 0x008000;
    return bits;
}

void stsv_ps3_input_poll(void)
{
    padInfo info;
    int selected_port = -1;
    if (polled_this_frame)
        return;
    polled_this_frame = 1;
    ++poll_frame;
    memset(&selected, 0, sizeof(selected));
    selected.ANA_L_H = selected.ANA_L_V = 128;
    selected.ANA_R_H = selected.ANA_R_V = 128;
    current_buttons = 0;
    int have_info = initialized && ioPadGetInfo(&info) == 0;
    for (int port = 0; have_info && port < PORTS; ++port) {
        padData fresh;
        if (info.status[port] == 0) {
            memset(&cached[port], 0, sizeof(cached[port]));
            continue;
        }
        memset(&fresh, 0, sizeof(fresh));
        /* len==0 means unchanged, so retain the last complete sample. */
        if (ioPadGetData(port, &fresh) == 0 && fresh.len > 0)
            cached[port] = fresh;
        if (cached[port].len > 0) {
            current_buttons |= vita_buttons(&cached[port]);
            if (selected_port < 0) {
                selected = cached[port];
                selected_port = port;
            }
        }
    }
    while (script_next < script_count && script[script_next].frame <= poll_frame) {
        script_buttons = script[script_next].buttons;
        fprintf(stderr, "[ps3 input] pollFrame=%llu scripted=0x%06x\n", poll_frame, script_buttons);
        ++script_next;
    }
    current_buttons |= (int)script_buttons;
}

void stsv_ps3_input_next_frame(void)
{
    polled_this_frame = 0;
}

int32_t stsv_ctrl_buttons(void)
{
    stsv_ps3_pump_callbacks();
    stsv_ps3_input_poll();
    return current_buttons;
}

float stsv_ctrl_axis(int32_t axis)
{
    int raw;
    switch (axis) {
        case 0: raw = selected.ANA_L_H; break;
        case 1: raw = selected.ANA_L_V; break;
        case 2: raw = selected.ANA_R_H; break;
        case 3: raw = selected.ANA_R_V; break;
        default: return 0.0f;
    }
    return (raw - 128) / 128.0f;
}

int32_t stsv_touch_count(void) { return 0; }
int32_t stsv_touch_x(int32_t finger) { (void)finger; return 0; }
int32_t stsv_touch_y(int32_t finger) { (void)finger; return 0; }
