/* iOS forbids subprocesses. Fail closed for desktop-only script paths. */
#include <unistd.h>
#include <errno.h>
static inline pid_t bvpn_no_fork(void) { errno = ENOTSUP; return -1; }
static inline int bvpn_no_execv(const char *path, char *const argv[]) {
    (void)path; (void)argv; errno = ENOTSUP; return -1;
}
static inline int bvpn_no_execl(const char *path, const char *arg, ...) {
    (void)path; (void)arg; errno = ENOTSUP; return -1;
}
#define fork bvpn_no_fork
#define execv bvpn_no_execv
#define execl bvpn_no_execl
