/* Vita-only libc gap fillers. Compiled solely into the Vita build (never the
 * desktop harness, which has a real libc). */

#include <pwd.h>
#include <unistd.h>

/* TeaVM's file.c UNIX path resolves the user's home directory via
 * getpwuid(getuid()), but Vita's newlib declares getpwuid in <pwd.h> without
 * implementing it (there is no user database). StS never reads the home dir
 * (it uses cwd-relative ux0: and absolute app0: paths), so a stub that points
 * at the writable data directory is sufficient and keeps the link resolved. */
static struct passwd stsv_pw = {
    .pw_name = "vita",
    .pw_dir = "ux0:data/stsvita",
    .pw_shell = "",
};

struct passwd *getpwuid(uid_t uid) {
    (void) uid;
    return &stsv_pw;
}
