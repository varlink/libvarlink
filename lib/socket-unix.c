#include "socket.h"
#include "util.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

long varlink_socket_connect_unix(VarlinkSocket *vsocket, const char *path) {
        _cleanup_(closep) int fd = -1;
        struct sockaddr_un sa = {
                .sun_family = AF_UNIX,
        };
        socklen_t sa_len;
        const int on = 1;

        assert(vsocket->fd == -1);

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

        vsocket->fd = fd;
        fd = -1;

        return 0;
}

long varlink_socket_listen_unix(const char *path, int *fdp) {
        _cleanup_(closep) int fd = -1;
        const int on = 1;
        struct sockaddr_un sa = {
                .sun_family = AF_UNIX,
        };
        socklen_t sa_len;

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

        if (sa.sun_path[0] != '@')
                chmod(sa.sun_path, 0666);

        if (listen(fd, SOMAXCONN) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        *fdp = fd;
        fd = -1;

        return 0;
}

long varlink_socket_accept_unix(int listen_fd, VarlinkSocket *socket, VarlinkObject **credentialsp) {
        _cleanup_(closep) int fd = -1;
        _cleanup_(freep) char *address = NULL;
        struct sockaddr_un sa;
        socklen_t sa_len = sizeof(sa);
        struct ucred ucred;
        socklen_t ucred_len = sizeof(struct ucred);

        assert(socket->fd == -1);

        fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (getsockname(fd, &sa, &sa_len) < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (sa.sun_path[0] == '\0') {
                _cleanup_(freep) char  *s = NULL;

                s = strndup(sa.sun_path + 1, sa_len - offsetof(struct sockaddr_un, sun_path) - 1);
                asprintf(&address, "unix:@%s", s);
        } else
                asprintf(&address, "unix:%s", sa.sun_path);

        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &ucred_len) < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (credentialsp) {
                VarlinkObject *credentials;

                varlink_object_new(&credentials);
                varlink_object_set_string(credentials, "address", address);
                varlink_object_set_int(credentials, "pid", ucred.pid);
                varlink_object_set_int(credentials, "uid", ucred.uid);
                varlink_object_set_int(credentials, "gid", ucred.gid);

                *credentialsp = credentials;
        }

        socket->fd = fd;
        fd = -1;

        return 0;
}
