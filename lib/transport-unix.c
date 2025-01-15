// SPDX-License-Identifier: Apache-2.0

#include "transport.h"
#include "util.h"
#include "varlink.h"

#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int strip_parameters(const char *address, char **pathp) {
        char *parm;
        _cleanup_(freep) char *path = NULL;

        parm = strchr(address, ';');
        if (!parm)
                path = strdup(address);
        else
                path = strndup(address, parm - address);
        if (!path)
                return -VARLINK_ERROR_PANIC;

        *pathp = path;
        path = NULL;
        return 0;
}

int varlink_connect_unix(const char *address) {
        _cleanup_(freep) char *path = NULL;
        _cleanup_(closep) int fd = -1;
        struct sockaddr_un sa = {
                .sun_family = AF_UNIX,
        };
        socklen_t sa_len;
        const int on = 1;
        int r;

        r = strip_parameters(address, &path);
        if (r < 0)
                return r;

        if (strlen(path) == 0 || strlen(path) + 1 > sizeof(sa.sun_path))
                return -VARLINK_ERROR_INVALID_ADDRESS;

        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

#ifdef __linux__
        if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;
#endif

        strcpy(sa.sun_path, path);
        if (sa.sun_path[0] == '@') {
                sa.sun_path[0] = '\0';
                sa_len = (socklen_t) strlen(path);
        } else
                sa_len = (socklen_t) (strlen(path) + 1);

        if (connect(fd, (struct sockaddr *)&sa, (socklen_t) (offsetof(struct sockaddr_un, sun_path) + sa_len)) < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        r = fd;
        fd = -1;

        return r;
}

int varlink_listen_unix(const char *address, char **pathp) {
        _cleanup_(closep) int fd = -1;
        const int on = 1;
        _cleanup_(freep) char *path = NULL;
        struct sockaddr_un sa = {
                .sun_family = AF_UNIX,
        };
        socklen_t sa_len;
        int r;

        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

#ifdef __linux__
        if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;
#endif

        r = strip_parameters(address, &path);
        if (r < 0)
                return r;

        if (strlen(path) == 0 || strlen(path) + 1 > sizeof(sa.sun_path))
                return -VARLINK_ERROR_INVALID_ADDRESS;

        strcpy(sa.sun_path, path);
        if (sa.sun_path[0] == '@') {
                sa.sun_path[0] = '\0';
                sa_len = (socklen_t) strlen(path);

        } else {
                unlink(path);
                sa_len = (socklen_t) (strlen(path) + 1);
        }

        if (bind(fd, (struct sockaddr *)&sa, (socklen_t) (offsetof(struct sockaddr_un, sun_path) + sa_len)) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (listen(fd, SOMAXCONN) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (pathp) {
                *pathp = path;
                path = NULL;
        }

        r = fd;
        fd = -1;
        return r;
}

int varlink_accept_unix(int listen_fd) {
        _cleanup_(closep) int fd = -1;
        int r;

        fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        r = fd;
        fd = -1;

        return r;
}
