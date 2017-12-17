#include "transport.h"
#include "varlink.h"
#include "util.h"

#include <stdio.h>
#include <sys/prctl.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <unistd.h>

int varlink_connect_exec(const char *executable, pid_t *pidp) {
        int fd;
        _cleanup_(freep) char *path = NULL;
        pid_t pid;

        /* An empty path lets the kernel autobind a UNIX abstrace address. */
        fd = varlink_listen_unix(";mode=0600", &path);
        if (fd < 0)
                return -VARLINK_ERROR_PANIC;

        pid = fork();
        if (pid < 0) {
                close(fd);
                return -VARLINK_ERROR_PANIC;
        }

        if (pid == 0) {
                _cleanup_(freep) char *address = NULL;
                sigset_t mask;

                sigemptyset(&mask);
                sigprocmask(SIG_SETMASK, &mask, NULL);

                /* Does not set CLOEXEC */
                if (dup2(fd, 3) != 3)
                        _exit(EXIT_FAILURE);

                if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
                        _exit(EXIT_FAILURE);

                if (asprintf(&address, "unix:%s;mode=0600", path) < 0)
                        _exit(EXIT_FAILURE);

                execlp(executable, executable, address, NULL);
                _exit(EXIT_FAILURE);
        }

        close(fd);

        if (pidp)
                *pidp = pid;

        return varlink_connect_unix(path);
}
