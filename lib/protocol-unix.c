#include "protocol.h"
#include "util.h"
#include "varlink.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static long parse_parameters(const char *address,
                             char **pathp,
                             mode_t *modep) {
        char *parm;
        _cleanup_(freep) char *path = NULL;
        char *endptr;
        mode_t mode = 0;

        /* An empty path asks the kernel to assign a unique abstract address by autobinding. */
        if (!address) {
                *pathp = NULL;
                return 0;
        }

        parm = strchr(address, ';');
        if (!parm) {
                *pathp = strdup(address);
                return 0;
        }

        /* An empty path asks the kernel to assign a unique abstract address by autobinding. */
        if (parm > address)
                path = strndup(address, parm - address);

        parm += 1;

        if (strncmp(parm, "mode=", 5) != 0)
                return -VARLINK_ERROR_INVALID_ADDRESS;

        mode = strtoul(parm + 5, &endptr, 0);
        if (endptr[0] != '\0')
                return -VARLINK_ERROR_INVALID_ADDRESS;

        *pathp = path;
        path = NULL;

        if (modep)
                *modep = mode;

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

        r = parse_parameters(address, &path, NULL);
        if (r < 0)
                return r;

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

int varlink_listen_unix(const char *address, char **pathp) {
        _cleanup_(closep) int fd = -1;
        const int on = 1;
        _cleanup_(freep) char *path = NULL;
        mode_t mode = 0;
        struct sockaddr_un sa = {
                .sun_family = AF_UNIX,
        };
        socklen_t sa_len;
        int r;

        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        r = parse_parameters(address, &path, &mode);
        if (r < 0)
                return r;

        if (!path) {
                /* UNIX autobind to kernel-assigned unique abstract address */
                if (bind(fd, (struct sockaddr *)&sa, sizeof(sa_family_t)) < 0)
                        return -VARLINK_ERROR_CANNOT_LISTEN;

                sa_len = sizeof(struct sockaddr_un);
                if (getsockname(fd, &sa, &sa_len) < 0)
                        return -VARLINK_ERROR_CANNOT_LISTEN;

                sa.sun_path[0] = '@';
                path = strndup(sa.sun_path, sa_len - offsetof(struct sockaddr_un, sun_path));

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

                path = strdup(path);
        }

        if (mode > 0) {
                /*
                 * Set the filesystem permissions of the socket, access will be checked by
                 * the kernel. Abstrace namespace sockets have no filesystem permissions. */
                if (sa.sun_path[0] != '@')
                        if (chmod(path, mode) < 0)
                                return -VARLINK_ERROR_CANNOT_LISTEN;
                /*
                 * Store the desired permissions at the listen socket's inode. They will be
                 * used to check inside the library when accepting connections. This also
                 * applies to abstract namespace sockets, which have no filesystem permissions.
                 */
                if (fchmod(fd, mode) < 0)
                        return -VARLINK_ERROR_CANNOT_LISTEN;
        }

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

/* Incoming connections are checked against the credentials of the listen socket. */
static bool check_credentials(mode_t listen_mode, uid_t listen_uid, gid_t listen_gid,
                              uid_t connection_uid, gid_t connection_gid) {
        /* World-accessible */
        if (listen_mode & 0002)
                return true;

        /* Always accept connections from root */
        if (connection_uid == 0 || connection_gid == 0)
                return true;

        /* Always accept connnections from the same user */
        if (connection_uid == listen_uid)
                return true;

        /* Accept connections from the same primary group */
        if ((listen_mode & 00020) && connection_gid == listen_gid)
                return true;

        return false;
}

int varlink_accept_unix(int listen_fd) {
        struct stat sb;
        _cleanup_(closep) int fd = -1;
        struct ucred ucred;
        socklen_t ucred_len = sizeof(struct ucred);
        long r;

        fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (fstat(listen_fd, &sb) < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &ucred_len) < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (!check_credentials(sb.st_mode, sb.st_uid, sb.st_gid,
                               ucred.uid, ucred.gid))
                return -VARLINK_ERROR_ACCESS_DENIED;

        r = fd;
        fd = -1;

        return r;
}
