#include "socket.h"

#include "util.h"
#include "varlink.h"

#include <string.h>

enum {
        VARLINK_ADDRESS_UNIX,
        VARLINK_ADDRESS_TCP
};

static long varlink_address_get_type(const char *address) {
        switch (address[0]) {
                case '\0':
                        return -VARLINK_ERROR_INVALID_ADDRESS;

                case '/':
                case '@':
                        return VARLINK_ADDRESS_UNIX;

                default:
                        return VARLINK_ADDRESS_TCP;
        }
}

int varlink_connect(const char *address) {
        switch (varlink_address_get_type(address)) {
                case VARLINK_ADDRESS_UNIX:
                        return varlink_connect_unix(address);

                case VARLINK_ADDRESS_TCP:
                        return varlink_connect_tcp(address);

                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }
}

int varlink_accept(const char *address, int listen_fd, pid_t *pidp, uid_t *uidp, gid_t *gidp) {
        int fd;

        switch (varlink_address_get_type(address)) {
                case VARLINK_ADDRESS_UNIX:
                        return varlink_accept_unix(listen_fd, pidp, uidp, gidp);

                case VARLINK_ADDRESS_TCP:
                        fd = varlink_accept_tcp(listen_fd);
                        if (fd < 0)
                                return fd;

                        *pidp = (pid_t)-1;
                        *uidp = (uid_t)-1;
                        *gidp = (gid_t)-1;
                        return fd;

                default:
                        return -VARLINK_ERROR_PANIC;
        }
}

_public_ int varlink_listen(const char *address, char **pathp) {
        const char *path = NULL;
        int fd;

        switch (varlink_address_get_type(address)) {
                case VARLINK_ADDRESS_UNIX:
                        /* abstract namespace */
                        if (address[0] != '@')
                                path = address;

                        fd = varlink_listen_unix(address);
                        break;

                case VARLINK_ADDRESS_TCP:
                        fd = varlink_listen_tcp(address);
                        break;

                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        /* CannotListen or InvalidAddress */
        if (fd < 0)
                return fd;

        if (pathp)
                *pathp = path ? strdup(path) : NULL;

        return fd;
}
