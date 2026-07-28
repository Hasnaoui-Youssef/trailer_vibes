#include "openocd_exit.h"
#include "openocd_jmp.h"

#include <stdlib.h>

_Thread_local struct openocd_jmp_state g_openocd_jmp;

_Noreturn void openocd_exit(int code) {
    if (g_openocd_jmp.armed) {
        g_openocd_jmp.armed = 0;
        longjmp(g_openocd_jmp.buf, code);
    }
    /* Not inside a guarded call (e.g. fires before Create() arms the guard,
     * or after teardown disarms it) - nothing to longjmp back to. */
    _Exit(code);
}

int openocd_call_guarded(void (*fn)(void *), void *arg, int *exit_code) {
    struct openocd_jmp_state saved = g_openocd_jmp;

    /* longjmp(buf, 0) delivers 1 to this setjmp, not 0, so 0 here always
     * means "returned normally", never "openocd_exit(0) fired". */
    int rc = setjmp(g_openocd_jmp.buf);
    if (rc != 0) {
        g_openocd_jmp = saved;
        *exit_code = rc;
        return 0;
    }
    g_openocd_jmp.armed = 1;
    fn(arg);
    g_openocd_jmp = saved;
    return 1;
}
