#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_JMP_H_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_JMP_H_

#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thread-local: openocd_call_guarded() runs on whichever thread calls it
 * (the calling thread during OpenOcdProvider::Create(), the command-queue
 * thread afterward), and a jmp_buf is only ever valid for a longjmp on the
 * same thread that armed it. Wrapped in a struct so nested guarded calls
 * (a queued task's own guard, running inside server_loop's guard) can save
 * and restore it by plain struct assignment - jmp_buf itself is an array
 * type and can't be copied with '='. */
struct openocd_jmp_state {
    jmp_buf buf;
    int armed;
};

extern _Thread_local struct openocd_jmp_state g_openocd_jmp;

/* Runs fn(arg) with openocd_exit() wired to longjmp back here instead of
 * calling libc exit(). Returns 1 if fn returned normally, 0 if openocd_exit()
 * fired (with the requested code left in *exit_code). No cleanup runs for
 * any C++ object still in scope on the aborted call chain when that
 * happens - by design, this is only ever meant to precede terminating the
 * whole engine. Safe to nest: an inner call's openocd_exit() only unwinds
 * to that inner call, never past it. */
int openocd_call_guarded(void (*fn)(void *), void *arg, int *exit_code);

#ifdef __cplusplus
}
#endif

#endif /* TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_JMP_H_ */
