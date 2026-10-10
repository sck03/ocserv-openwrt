/* Packet Tunnel extensions never run external scripts or subprocesses. */
#include <unistd.h>
#include <errno.h>
static inline pid_t vpn_no_fork(void) { errno = ENOTSUP; return -1; }
static inline int vpn_no_execv(const char *path, char *const argv[]) {
    (void)path; (void)argv; errno = ENOTSUP; return -1;
}
static inline int vpn_no_execl(const char *path, const char *arg, ...) {
    (void)path; (void)arg; errno = ENOTSUP; return -1;
}
#define fork vpn_no_fork
#define execv vpn_no_execv
#define execl vpn_no_execl
