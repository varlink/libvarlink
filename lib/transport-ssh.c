#include "transport.h"
#include "varlink.h"
#include "util.h"

#include <stdio.h>
#include <sys/prctl.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <unistd.h>

int varlink_connect_ssh(const char *address, pid_t *pidp) {
        _cleanup_(freep) char *host = NULL;
        _cleanup_(freep) char *end = NULL;
        unsigned int port = 0;
        int sp[2];
        pid_t pid;
        long r;

        /* Extract optional port number */
        r = sscanf(address, "%m[^:]:%d%ms", &host, &port, &end);
        if (r < 1 || r > 2)
                return -VARLINK_ERROR_INVALID_ADDRESS;

        if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0, sp) < 0)
                return -VARLINK_ERROR_PANIC;

        pid = fork();
        if (pid < 0) {
                close(sp[0]);
                close(sp[1]);
                return -VARLINK_ERROR_PANIC;
        }

        if (pid == 0) {
                const char *arg[11];
                long i = 0;
                sigset_t mask;

                sigemptyset(&mask);
                sigprocmask(SIG_SETMASK, &mask, NULL);

                arg[i++] = "ssh";

                /* Disable X11 and pseudo-terminal */
                arg[i++] = "-xT";

                /* Disable passphrase/password querying */
                arg[i++] = "-o";
                arg[i++] = "BatchMode=yes";

                /* Add custom port number */
                if (port > 0) {
                        char p[8];

                        sprintf(p, "%d", port);
                        arg[i++] = "-p";
                        arg[i++] = p;
                }

                arg[i++] = "--";
                arg[i++] = host;
                arg[i++] = "varlink";
                arg[i++] = "bridge";
                arg[i] = NULL;

                close(sp[0]);

                /* Does not set CLOEXEC */
                if (dup2(sp[1], STDIN_FILENO) != STDIN_FILENO ||
                    dup2(sp[1], STDOUT_FILENO) != STDOUT_FILENO)
                        return -VARLINK_ERROR_PANIC;

                if (sp[1] != STDIN_FILENO && sp[1] != STDOUT_FILENO)
                        close(sp[1]);

                if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
                        _exit(EXIT_FAILURE);

                execvp(arg[0], (char **) arg);
                _exit(EXIT_FAILURE);
        }

        close(sp[1]);

        if (pidp)
                *pidp = pid;

        return sp[0];
}
