#include "socket.h"

#include "util.h"
#include "varlink.h"

#include <string.h>

enum {
        VARLINK_ADDRESS_UNIX,
        VARLINK_ADDRESS_TCP,
        VARLINK_ADDRESS_SSH
};

static long url_decode(char **outp, const char *in, unsigned long len) {
        _cleanup_(freep) char *out = NULL;
        unsigned long j = 0;

        out = malloc(len);

        for (unsigned long i = 0; in[i] != '\0' && i < len; i += 1) {
                if (in[i] == '%') {
                        unsigned int hex;

                        if (i + 3 > len)
                                return -VARLINK_ERROR_INVALID_VALUE;

                        if (sscanf(in + i + 1, "%02x", &hex) != 1)
                                return -VARLINK_ERROR_INVALID_VALUE;

                        out[j] = hex;
                        j += 1;
                        i += 2;

                        continue;
                }

                out[j] = in[i];
                j += 1;
        }

        out[j] = '\0';
        *outp = out;
        out = NULL;

        return j;
}

static long varlink_address_parse(const char *address, char **destinationp) {
        _cleanup_(freep) char *destination = NULL;
        long type;
        long r;

        if (strncmp(address, "ssh://", 6) == 0) {
                type = VARLINK_ADDRESS_SSH;
                destination = strdup(address + 6);

        } else {
                if (strncmp(address, "varlink://", 10) == 0) {
                        r = url_decode(&destination, address + 10, strlen(address + 10));
                        if (r < 0)
                                return -VARLINK_ERROR_INVALID_ADDRESS;
                } else {
                        destination = strdup(address);
                }

                switch (destination[0]) {
                        case '/':
                        case '@':
                                type = VARLINK_ADDRESS_UNIX;
                                break;
                        default:
                                type = VARLINK_ADDRESS_TCP;
                                break;
                }
        }

        if (destinationp) {
                *destinationp = destination;
                destination = NULL;
        }

        return type;
}

int varlink_connect(const char *address) {
        _cleanup_(freep) char *destination = NULL;

        switch (varlink_address_parse(address, &destination)) {
                case VARLINK_ADDRESS_UNIX:
                        return varlink_connect_unix(destination);

                case VARLINK_ADDRESS_TCP:
                        return varlink_connect_tcp(destination);

                case VARLINK_ADDRESS_SSH:
                        return varlink_connect_ssh(destination);

                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }
}

int varlink_accept(const char *address, int listen_fd, pid_t *pidp, uid_t *uidp, gid_t *gidp) {
        int fd;

        switch (varlink_address_parse(address, NULL)) {
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

                case VARLINK_ADDRESS_SSH:
                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }
}

_public_ int varlink_listen(const char *address, char **pathp) {
        _cleanup_(freep) char *destination = NULL;
        int fd;

        switch (varlink_address_parse(address, &destination)) {
                case VARLINK_ADDRESS_UNIX:
                        fd = varlink_listen_unix(destination);
                        if (fd < 0)
                                return fd;

                        if (pathp && destination[0] != '@') {
                                *pathp = destination;
                                destination = NULL;
                        }
                        break;

                case VARLINK_ADDRESS_TCP:
                        fd = varlink_listen_tcp(destination);
                        if (fd < 0)
                                return fd;
                        break;

                case VARLINK_ADDRESS_SSH:
                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        return fd;
}
