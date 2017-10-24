#include "socket.h"

#include "util.h"
#include "varlink.h"

#include <string.h>

enum {
        VARLINK_ADDRESS_EXEC,
        VARLINK_ADDRESS_IP,
        VARLINK_ADDRESS_SSH,
        VARLINK_ADDRESS_UNIX
};

static long varlink_address_parse(const char *address, char **destinationp) {
        _cleanup_(freep) char *destination = NULL;
        long type;

        if (strncmp(address, "exec:", 5) == 0) {
                type = VARLINK_ADDRESS_EXEC;
                destination = strdup(address + 5);

        } else if (strncmp(address, "ssh://", 6) == 0) {
                type = VARLINK_ADDRESS_SSH;
                destination = strdup(address + 6);

        } else if (strncmp(address, "unix:", 5) == 0) {
                type = VARLINK_ADDRESS_UNIX;

                /*
                 * An empty path asks the kernel to assign a unique
                 * abstract address by autobinding.
                 */
                if (address[5] != '\0')
                        destination = strdup(address + 5);

        } else {
                type = VARLINK_ADDRESS_IP;
                destination = strdup(address);
        }

        if (destinationp) {
                *destinationp = destination;
                destination = NULL;
        }

        return type;
}

int varlink_connect(const char *address, pid_t *pidp) {
        _cleanup_(freep) char *destination = NULL;

        switch (varlink_address_parse(address, &destination)) {
                case VARLINK_ADDRESS_EXEC:
                        return varlink_connect_exec(destination, pidp);

                case VARLINK_ADDRESS_IP:
                        return varlink_connect_ip(destination);

                case VARLINK_ADDRESS_SSH:
                        return varlink_connect_ssh(destination, pidp);

                case VARLINK_ADDRESS_UNIX:
                        return varlink_connect_unix(destination);

                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }
}

int varlink_accept(const char *address,
                   int listen_fd,
                   mode_t mode,
                   pid_t *pidp, uid_t *uidp, gid_t *gidp) {
        int fd;

        switch (varlink_address_parse(address, NULL)) {
                case VARLINK_ADDRESS_IP:
                        fd = varlink_accept_ip(listen_fd);
                        if (fd < 0)
                                return fd;

                        *pidp = (pid_t)-1;
                        *uidp = (uid_t)-1;
                        *gidp = (gid_t)-1;
                        return fd;

                case VARLINK_ADDRESS_UNIX:
                        return varlink_accept_unix(listen_fd,
                                                   mode,
                                                   pidp, uidp, gidp);

                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }
}

_public_ int varlink_listen(const char *address, char **pathp) {
        _cleanup_(freep) char *destination = NULL;
        int fd;

        switch (varlink_address_parse(address, &destination)) {
                case VARLINK_ADDRESS_IP:
                        fd = varlink_listen_ip(destination);
                        if (fd < 0)
                                return fd;
                        break;

                case VARLINK_ADDRESS_UNIX:
                        fd = varlink_listen_unix(destination, pathp);
                        if (fd < 0)
                                return fd;
                        break;

                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        return fd;
}
