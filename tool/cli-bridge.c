#include "cli.h"
#include "util.h"

#include <stdio.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wordexp.h>

int cli_bridge(const char *command, pid_t *pidp) {
        int sp[2];
        pid_t pid;

        if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0, sp) < 0)
                return -VARLINK_ERROR_PANIC;

        pid = fork();
        if (pid < 0) {
                close(sp[0]);
                close(sp[1]);
                return -VARLINK_ERROR_PANIC;
        }

        if (pid == 0) {
                sigset_t mask;
                wordexp_t p;

                sigemptyset(&mask);
                sigprocmask(SIG_SETMASK, &mask, NULL);

                close(sp[0]);

                /* Does not set CLOEXEC */
                if (dup2(sp[1], STDIN_FILENO) != STDIN_FILENO ||
                    dup2(sp[1], STDOUT_FILENO) != STDOUT_FILENO)
                        return -VARLINK_ERROR_PANIC;

                if (sp[1] != STDIN_FILENO && sp[1] != STDOUT_FILENO)
                        close(sp[1]);

                if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
                        _exit(EXIT_FAILURE);

                if (wordexp(command, &p, WRDE_NOCMD|WRDE_UNDEF) < 0)
                        _exit(EXIT_FAILURE);

                execvp(p.we_wordv[0], p.we_wordv);
                _exit(EXIT_FAILURE);
        }

        close(sp[1]);

        if (pidp)
                *pidp = pid;

        return sp[0];
}
