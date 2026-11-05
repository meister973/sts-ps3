/* Open-source PSL1GHT process entry for the original TeaVM game. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/process.h>

#include "stsv_ps3_platform.h"
#include "stsvita_glue.h"

SYS_PROCESS_PARAM(1000, 1024 * 1024);

extern int stsv_java_main(int argc, char **argv);

int main(int argc, char **argv)
{
    FILE *truncate;

    mkdir("/dev_hdd0/tmp", 0777);
    truncate = fopen(STSV_PS3_LOG_PATH, "w");
    if (truncate != NULL) {
        fclose(truncate);
        /* Append on both descriptors prevents one stream overwriting the other. */
        freopen(STSV_PS3_LOG_PATH, "a", stdout);
        freopen(STSV_PS3_LOG_PATH, "a", stderr);
    }
    /* Preserve exception/abort diagnostics even if the runtime never unwinds. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "[ps3] original Slay the Spire TeaVM startup; pointer=%u long=%u\n",
            (unsigned)sizeof(void *), (unsigned)sizeof(long));
    if (chdir(STSV_PS3_USRDIR) != 0) {
        fprintf(stderr, "[ps3] cannot enter %s: %s\n", STSV_PS3_USRDIR, strerror(errno));
        return 1;
    }
    stsv_ps3_system_init();
    stsv_input_init();
    stsv_audio_init();
    stsv_gfx_init();
    fprintf(stderr, "[ps3] entering original generated Java main\n");
    int result = stsv_java_main(argc, argv);
    fprintf(stderr, "[ps3] Java main returned %d\n", result);
    fflush(NULL);
    return result;
}
