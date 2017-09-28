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

int varlink_listen_unix(const char *path) {
        _cleanup_(closep) int fd = -1;
        const int on = 1;
        struct sockaddr_un sa = {
                .sun_family = AF_UNIX,
        };
        socklen_t sa_len;
        int r;

        if (strlen(path) == 0 || strlen(path) + 1 > sizeof(sa.sun_path))
                return -VARLINK_ERROR_INVALID_ADDRESS;

        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

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

        if (sa.sun_path[0] != '\0')
                chmod(sa.sun_path, 0666);

        if (listen(fd, SOMAXCONN) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        r = fd;
        fd = -1;

        return r;
}

int varlink_accept_unix(int listen_fd, pid_t *pidp, uid_t *uidp, gid_t *gidp) {
        _cleanup_(closep) int fd = -1;
        _cleanup_(freep) char *address = NULL;
        struct sockaddr_un sa;
        socklen_t sa_len = sizeof(sa);
        struct ucred ucred;
        socklen_t ucred_len = sizeof(struct ucred);
        long r;

        fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (getsockname(fd, &sa, &sa_len) < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (sa.sun_path[0] == '\0') {
                _cleanup_(freep) char  *s = NULL;

                s = strndup(sa.sun_path + 1, sa_len - offsetof(struct sockaddr_un, sun_path) - 1);
                asprintf(&address, "@%s", s);
        } else
                asprintf(&address, "%s", sa.sun_path);

        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &ucred_len) < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        *pidp = ucred.pid;
        *uidp = ucred.uid;
        *gidp = ucred.gid;

        r = fd;
        fd = -1;

        return r;
}
