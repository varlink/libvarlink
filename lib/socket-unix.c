#include "socket.h"
#include "util.h"
#include "varlink.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int varlink_connect_unix(const char *path) {
        _cleanup_(closep) int fd = -1;
        struct sockaddr_un sa = {
                .sun_family = AF_UNIX,
        };
        socklen_t sa_len;
        const int on = 1;
        int r;

        if (strlen(path) == 0 || strlen(path) + 1 > sizeof(sa.sun_path))
                return -VARLINK_ERROR_INVALID_ADDRESS;

        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        strcpy(sa.sun_path, path);
        if (sa.sun_path[0] == '@') {
                sa.sun_path[0] = '\0';
                sa_len = strlen(path);
        } else
                sa_len = strlen(path) + 1;

        if (connect(fd, &sa, offsetof(struct sockaddr_un, sun_path) + sa_len) < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        r = fd;
        fd = -1;

        return r;
}

int varlink_listen_unix(const char *path, char **pathp) {
        _cleanup_(closep) int fd = -1;
        const int on = 1;
        struct sockaddr_un sa = {
                .sun_family = AF_UNIX,
        };
        socklen_t sa_len;
        _cleanup_(freep) char *address = NULL;
        int r;

        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (!path) {
                /* UNIX autobind to kernel-assigned unique abstract address */
                if (bind(fd, (struct sockaddr *)&sa, sizeof(sa_family_t)) < 0)
                        return -VARLINK_ERROR_CANNOT_LISTEN;

                sa_len = sizeof(struct sockaddr_un);
                if (getsockname(fd, &sa, &sa_len) < 0)
                        return -VARLINK_ERROR_CANNOT_LISTEN;

                sa.sun_path[0] = '@';
                address = strndup(sa.sun_path, sa_len - offsetof(struct sockaddr_un, sun_path));

        } else {
                if (strlen(path) == 0 || strlen(path) + 1 > sizeof(sa.sun_path))
                        return -VARLINK_ERROR_INVALID_ADDRESS;

                strcpy(sa.sun_path, path);
                if (sa.sun_path[0] == '@') {
                        sa.sun_path[0] = '\0';
                        sa_len = strlen(path);

                } else {
                        unlink(path);
                        sa_len = strlen(path) + 1;
                }

                if (bind(fd, (struct sockaddr *)&sa, offsetof(struct sockaddr_un, sun_path) + sa_len) < 0)
                        return -VARLINK_ERROR_CANNOT_LISTEN;

                /*
                 * File permissions are not set. Credentials are checked when incoming connections
                 * are accepted, or by the service in every call individually.
                 */
                if (sa.sun_path[0] != '\0')
                        chmod(sa.sun_path, 0666);

                address = strdup(path);
        }

        if (listen(fd, SOMAXCONN) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (pathp) {
                *pathp = address;
                address = NULL;
        }

        r = fd;
        fd = -1;

        return r;
}

static bool check_credentials(mode_t mode, pid_t pid, uid_t uid, gid_t gid) {
        /* World-accessible */
        if (mode & 0002)
                return true;

        /* Always accept connections from root */
        if (uid == 0 || gid == 0)
                return true;

        /* Always accept connnections from the same user */
        if (uid == getuid())
                return true;

        /* Always accept connections from the parent process*/
        if (pid == getppid())
                return true;

        /* Accept connections from the same primary group */
        if ((mode & 00020) && gid == getgid())
                return true;

        return false;
}

int varlink_accept_unix(int listen_fd,
                        mode_t mode,
                        pid_t *pidp, uid_t *uidp, gid_t *gidp) {
        _cleanup_(closep) int fd = -1;
        struct sockaddr_un sa;
        socklen_t sa_len = sizeof(sa);
        struct ucred ucred;
        socklen_t ucred_len = sizeof(struct ucred);
        long r;

        fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &ucred_len) < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (getsockname(listen_fd, &sa, &sa_len) < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (!check_credentials(mode, ucred.pid, ucred.uid, ucred.gid))
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        *pidp = ucred.pid;
        *uidp = ucred.uid;
        *gidp = ucred.gid;

        r = fd;
        fd = -1;

        return r;
}
