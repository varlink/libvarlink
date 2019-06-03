#include "cli.h"
#include "transport.h"
#include "util.h"

#include <stdio.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wordexp.h>

int cli_activate(const char *command, char **pathp, pid_t *pidp) {
        int fd;
        char template[] = "/tmp/varlink-XXXXXX";
        const char *dir;
        _cleanup_(freep) char *path = NULL;
        pid_t pid;

        dir = mkdtemp(template);
        if (!dir)
                return -VARLINK_ERROR_PANIC;

        if (asprintf(&path, "%s/socket", dir) < 0)
                return -VARLINK_ERROR_PANIC;

        fd = varlink_listen_unix(path, pathp);
        if (fd < 0)
                return -VARLINK_ERROR_PANIC;

        pid = fork();
        if (pid < 0) {
                close(fd);
                return -VARLINK_ERROR_PANIC;
        }

        if (pid == 0) {
                sigset_t mask;
                char s[32];
                _cleanup_(freep) char *address = NULL;
                wordexp_t p;

                sigemptyset(&mask);
                sigprocmask(SIG_SETMASK, &mask, NULL);

                /* Does not set CLOEXEC */
                if (dup2(fd, 3) != 3)
                        _exit(EXIT_FAILURE);

                if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
                        _exit(EXIT_FAILURE);

                sprintf(s, "%d", getpid());
                setenv("LISTEN_PID", s, true);
                setenv("LISTEN_FDS", "1", true);

                /* Export address, wordexp() will expand --varlink=$VARLINK_ADDRESS */
                if (asprintf(&address, "unix:%s", path) < 0)
                        _exit(EXIT_FAILURE);
                setenv("VARLINK_ADDRESS", address, true);

                if (wordexp(command, &p, WRDE_NOCMD|WRDE_UNDEF) < 0)
                        _exit(EXIT_FAILURE);

                execvp(p.we_wordv[0], p.we_wordv);
                _exit(EXIT_FAILURE);
        }

        close(fd);

        if (pidp)
                *pidp = pid;

        return varlink_connect_unix(path);
}
